/* Timegrapher.h - V5.0 detection layer.
 *
 * libtimegrapher provides ONLY signal detection and BPH/sync tracking
 * for mechanical-watch audio. Per-event amplitude, predicted-time gate,
 * autocorrelation, and other analytics live in companion libraries
 * (libtgan, libtgacf) that consume the events emitted here.
 *
 * Streaming pipeline:
 *
 *     PCM (float32 mono)
 *       -> DC blocker (200 Hz HPF)
 *       -> envelope (full-wave rectify + 0.15 ms LPF)
 *       -> silence-based onset detector
 *       -> A and C event timestamps with sub-sample accuracy
 *       -> Rayleigh phase-score BPH detection + PLL sync tracker
 *
 * Output: tg_event_t carrying type, time, sample index, peak value,
 *         and is_pre_sync flag. NO amplitude, NO predicted time,
 *         NO parity, NO gate decision -- those are analytics.
 */
#ifndef TIMEGRAPHER_H
#define TIMEGRAPHER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== enums ============================= */

typedef enum {
    TG_BPH_MODE_AUTO   = 0,
    TG_BPH_MODE_MANUAL = 1
} tg_bph_mode_t;

typedef enum {
    TG_SYNC_NOT_SYNCED = 0,
    TG_SYNC_SYNCED     = 1,
    TG_SYNC_MISMATCH   = 2     /* manual BPH set but signal doesn't match */
} tg_sync_status_t;

typedef enum {
    TG_EVENT_UNKNOWN = 0,
    TG_EVENT_A       = 1,      /* unlock                             */
    TG_EVENT_C       = 2       /* drop / lock                        */
} tg_event_type_t;

/* V5.4: C-event timing placement.
 *
 * The detector finds a C event in two stages: the C cluster's peak
 * (envelope max past the C-search-skip window) and, optionally, the
 * C cluster's onset (rising-edge crossing of half the peak's height,
 * found by walking back from the peak).
 *
 * c_placement controls which timing fills the primary fields of
 * tg_event_t (sample_index, sub_sample_offset, time_seconds). The
 * other timing is always available as metadata via the onset_*
 * fields when found.
 *
 * PEAK (default, V5.0-V5.3 behavior): primary timing is the C peak.
 * ONSET: primary timing is the C onset; falls back to peak if onset
 *        detection failed for that beat.
 */
typedef enum {
    TG_C_PLACEMENT_PEAK  = 0,
    TG_C_PLACEMENT_ONSET = 1
} tg_c_placement_t;

/* ========================== configuration ========================= */

typedef struct {
    /* Required */
    double         sample_rate;        /* Hz, e.g. 48000              */
    tg_bph_mode_t  bph_mode;
    int            manual_bph;         /* used when bph_mode == MANUAL*/

    /* Optional - 0 = library default */
    double  hpf_cutoff_hz;             /* default 200.0               */
    double  envelope_smooth_ms;        /* default 0.15                */
    double  event_min_separation_ms;   /* refractory, default 2.0     */
    double  sync_tolerance_pct;        /* default 3.0                 */
    double  auto_detect_seconds;       /* default 1.5                 */
    int     sync_loss_misses;          /* default 12                  */
    double  pll_period_gain;           /* default 0.01                */
    double  pll_ac_gain;               /* default 0.05                */

    /* Detector threshold tuning (init-time; runtime via tg_set_*).
     * Both 0 -> use built-in defaults: 0.03 onset, 0.20 min-peak. */
    double  onset_fraction_init;
    double  min_peak_fraction_init;

    /* If 1, drop events emitted before BPH lock from the output. */
    int     suppress_pre_sync_events;

    /* V5.4: C-event timing placement. Default 0 = TG_C_PLACEMENT_PEAK
     * (backward compatible with V5.3 and earlier). Runtime changes
     * via tg_set_c_placement(). */
    tg_c_placement_t c_placement;
} tg_config_t;

void tg_config_default(tg_config_t *cfg);

/* ============================ events ============================== */

typedef struct {
    /* Primary timing (chosen per c_placement for C events; for A
     * events this is always the onset). */
    double           time_seconds;     /* sub-sample accurate            */
    uint64_t         sample_index;     /* integer absolute sample index  */
    double           sub_sample_offset;/* in [-0.5, +0.5]                */
    float            peak_value;       /* envelope value at peak         */
    tg_event_type_t  type;
    int              is_pre_sync;      /* 1 if before BPH lock           */

    /* V5.4: C-event onset metadata. Populated for C events when the
     * backward-walk algorithm successfully locates the C cluster's
     * rising edge. Zero / 0 for A events and for C events where
     * onset detection failed (e.g. very noisy signals).
     *
     * onset_time_seconds == (onset_sample_index + onset_sub_sample_offset) / sample_rate.
     *
     * In TG_C_PLACEMENT_ONSET mode, the primary timing fields above
     * carry the same value as the onset_* fields here (i.e. onset is
     * promoted to primary; the original peak is no longer reported).
     * In TG_C_PLACEMENT_PEAK mode, the primary fields carry the peak
     * and these onset_* fields are populated as metadata. */
    uint64_t         onset_sample_index;
    double           onset_sub_sample_offset;
    double           onset_time_seconds;
    int              onset_valid;      /* 1 if onset fields are populated */
} tg_event_t;

/* ============================ result ============================== */

typedef struct {
    /* Sync state */
    tg_sync_status_t  sync_status;
    int               detected_bph;
    double            measured_period_s;   /* PLL-tracked beat period   */

    /* Events emitted in THIS call. Pointer is owned by the context;
     * remains valid until the next tg_process / tg_flush / tg_destroy. */
    tg_event_t       *events;
    size_t            num_events;

    /* Envelope, delayed for alignment with events. processed_pcm[0]
     * corresponds to absolute input sample processed_pcm_start_sample. */
    float            *processed_pcm;
    size_t            processed_pcm_len;
    uint64_t          processed_pcm_start_sample;

    /* One-shot edge flags */
    int               sync_lost_event;
    int               sync_acquired_event;
    /* V5.6: set for one tg_process call when the detector observed
     * a large amplitude jump (>= 10x baseline) and the library
     * flushed adaptive state to recover from a polluted prelude
     * (e.g. ambient noise before the watch arrives on the mic). */
    int               detector_reset_event;

    /* Detector state (instantaneous) for diagnostics / UI */
    float             onset_threshold;
    float             min_peak_threshold;
    float             noise_floor;
    float             reference_peak;
} tg_result_t;

/* ============================== API =============================== */

typedef struct tg_context tg_context_t;

tg_context_t *tg_init   (const tg_config_t *cfg);
void          tg_destroy(tg_context_t *ctx);

/* Feed a chunk of float32 mono PCM. Returns 0 on success. */
int tg_process(tg_context_t *ctx,
               const float  *pcm,
               size_t        num_samples,
               tg_result_t  *result);

/* Drain the envelope delay line at end-of-stream. */
int tg_flush  (tg_context_t *ctx, tg_result_t *result);

/* Clear all runtime state. Configuration is preserved. */
void tg_reset (tg_context_t *ctx);

/* Runtime threshold tuning (clamped [0.001, 0.9]; effective next sample). */
double tg_get_onset_fraction   (const tg_context_t *ctx);
double tg_get_min_peak_fraction(const tg_context_t *ctx);
void   tg_set_onset_fraction   (tg_context_t *ctx, double frac);
void   tg_set_min_peak_fraction(tg_context_t *ctx, double frac);

/* V5.4: runtime C-event placement control. Effective on the next
 * burst. */
tg_c_placement_t tg_get_c_placement(const tg_context_t *ctx);
void             tg_set_c_placement(tg_context_t *ctx, tg_c_placement_t mode);

#ifdef __cplusplus
}
#endif

#endif /* TIMEGRAPHER_H */
