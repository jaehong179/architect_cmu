/* Detector.h - silence-based onset detector.
 *
 * Segments the envelope into SILENCE and BURST regions and emits two
 * events per accepted burst:
 *
 *   A (is_onset = 1) : silence -> signal threshold CROSSING.
 *       Sub-sample timing by linear interpolation between the two
 *       samples straddling the onset threshold. This is the moment
 *       the envelope first lifts above the ambient noise floor and
 *       corresponds to the escapement unlock.
 *
 *   C (is_onset = 0) : PEAK of the burst envelope.
 *       Sub-sample timing by parabolic interpolation across the max
 *       sample and its two neighbors. Corresponds to the escape
 *       wheel dropping onto the locking face (the loudest noise).
 *
 * Thresholds are scale-invariant and derived from two robust estimators:
 *
 *   noise_floor    = 75th percentile of downsampled silence-region
 *                    envelope samples (a 256-slot ring buffer sampled
 *                    once per millisecond during silence). Tracks the
 *                    TYPICAL ambient level between ticks, not its
 *                    minimum -- crucial for preventing false onsets in
 *                    quiet sections where rumble hovers above the min.
 *
 *   reference_peak = median of the last 16 accepted burst peaks.
 *                    Robust to outliers (one loud bang doesn't move it)
 *                    and to sparse ticks (no decay during long gaps).
 *
 * No per-recording tuning is needed: the same gate fractions (3% onset,
 * 2% release, 20% min-peak) are used for all watches.
 *
 * Both the silence gate (time between beats) and the burst-end gate
 * (time within a beat) are WALL-CLOCK based -- counted in samples
 * since a reference point, not continuous-below-threshold samples.
 * This handles watches whose ring-down oscillates near the release
 * threshold without settling.
 *
 * A is held internally until burst-end so that bursts whose peak fails
 * the min-peak threshold (noise bumps) can be discarded entirely,
 * without leaving an orphan A in the output.
 */
#ifndef TG_DETECTOR_H
#define TG_DETECTOR_H

#include <stddef.h>
#include <stdint.h>

/* Ring-buffer sizes (compile-time constants). */
#define TG_NOISE_HISTORY_N  256  /* ~256 ms at 1 ms/sample                */
#define TG_PEAK_HISTORY_N    16  /* ~2 s at 8 Hz beat rate               */

/* V5.6 regime-change detector constants. */
#define TG_REGIME_RING_N     8   /* short-term burst peak ring (~1.5s at 21600 BPH) */
#define TG_REGIME_RATIO    10.0  /* trip if new peak >= ratio * recent min */
#define TG_REGIME_FLOOR  0.001   /* skip ratio check when both peaks below this */
#define TG_REGIME_COOLDOWN_S 1.0 /* seconds between resets */

/* Raw event emitted by the detector. */
typedef struct {
    uint64_t sample_index;       /* integer absolute sample index         */
    double   sub_sample_offset;  /* in [-0.5, +0.5]                       */
    double   time_seconds;       /* (sample_index + offset) / fs          */
    float    peak_value;         /* burst peak envelope value             */
    int      is_onset;           /* 1 = A (onset), 0 = C (peak)           */

    /* V5.4: C-event onset metadata. Populated by the detector for
     * C events when the backward-walk algorithm finds the rising
     * edge of the C cluster. Zero / 0 for A events and for C events
     * where onset detection failed. The library promotes these to
     * the public tg_event_t.onset_* fields. */
    uint64_t onset_sample_index;
    double   onset_sub_sample_offset;
    double   onset_time_seconds;
    int      onset_valid;
} tg_raw_event_t;

typedef struct {
    /* ---- configuration ---- */
    double   fs;                 /* sample rate                           */
    double   noise_alpha;        /* LPF coeff for EMA fallback noise floor*/
    double   ceil_alpha;         /* LPF coeff for signal_ceiling bootstrap*/
    uint64_t warmup_samples;     /* skip first N samples                  */

    /* Tunable gate fractions (of noise->reference-peak dynamic range).
     * Both are read every sample; a single-word write is atomic enough
     * for UI-controlled runtime adjustment. Clamped to [0.001, 0.9] by
     * the setters. Defaults:
     *   onset_fraction    = 0.03   (A onset threshold)
     *   min_peak_fraction = 0.20   (noise-bump rejection) */
    double   onset_fraction;
    double   min_peak_fraction;

    /* V4.5: minimum wall-clock A-to-A interval. After a successful A
     * onset, no new A can fire until this many samples have elapsed
     * since that A's onset (independent of the burst-end / silence
     * machinery). Used to reject sub-impulses inside multi-impulse
     * beat clusters once the beat period is known.
     *
     * Pre-sync default: 0 (no enforcement, so BPH detection sees all
     * candidate events). Set by the library to 0.7 * beat_period once
     * sync is acquired. */
    uint64_t min_a_interval_samples;
    uint64_t samples_since_last_a;     /* wall-clock counter */

    /* ---- noise floor: 75th percentile of downsampled silence samples --*/
    double   noise_history[TG_NOISE_HISTORY_N];
    int      noise_history_count;
    int      noise_history_head;
    uint64_t noise_last_sample_idx;
    uint64_t noise_sample_interval;  /* samples between decimated picks   */
    double   noise_percentile_cache;
    double   noise_floor;            /* EMA min-tracker (bootstrap only)  */

    /* ---- reference peak: median of last N accepted burst peaks -------- */
    double   peak_history[TG_PEAK_HISTORY_N];
    int      peak_history_count;
    int      peak_history_head;
    double   median_peak_cache;
    double   signal_ceiling;     /* max-hold fallback used pre-history    */

    /* ---- V5.6: regime-change detector --------------------------------
     *
     * Detects a >= 10x jump in burst peak amplitude relative to the
     * rolling minimum of the last few accepted peaks. Trips when audio
     * crosses from one acoustic regime to another (e.g. quiet
     * ambient noise -> watch on microphone, or removed and replaced).
     *
     * On trip, the library flushes the polluted adaptive state
     * (event history, sync state, noise floor ring, peak median ring)
     * and re-seeds with the triggering peak so the next beat doesn't
     * immediately re-trigger.
     *
     * regime_peak_ring is independent of peak_history -- peak_history
     * gets flushed on reset, but regime_peak_ring keeps the seeded
     * trigger value so the next-beat comparison has a baseline.
     *
     * Tunables:
     *   - ratio threshold: 10x (TG_REGIME_RATIO)
     *   - ring size: 8 entries (TG_REGIME_RING_N) -- ~1.5 s at 21600 BPH
     *   - cooldown: 1 second between resets, gated by sample counter
     *   - absolute floor: 0.001 -- below this, both peaks are noise
     *     and the ratio check is skipped (TG_REGIME_FLOOR) */
    double   regime_peak_ring[TG_REGIME_RING_N];
    int      regime_peak_count;
    int      regime_peak_head;
    uint64_t regime_last_reset_idx;  /* abs sample idx of last reset, 0 = never */
    int      regime_reset_pending;    /* set on trip, cleared by lib after flush */

    /* ---- wall-clock gates ---- */
    uint64_t silence_samples;    /* samples since burst end (capped)      */
    uint64_t min_silence_samples;/* threshold for new A onset             */
    uint64_t burst_end_samples;  /* threshold for C emission              */

    /* ---- state machine ---- */
    int      in_burst;

    /* onset (A) state */
    uint64_t burst_start_idx;
    double   burst_start_time;
    double   burst_start_offset;

    /* peak (C) tracking */
    double   burst_max;
    uint64_t burst_max_idx;
    double   burst_max_y_minus1;
    double   burst_max_y_plus1;
    int      have_peak_plus1;

    /* V5.2: separate C-peak tracking. The "burst_max" above tracks the
     * absolute envelope max within a burst; that's used for the min-peak
     * height check at burst-end. But for C-event TIMING we need to skip
     * the first N samples of the burst, which often contain the A's own
     * impulse spike or intermediate sub-impulses that are NOT the C
     * cluster (especially on multi-impulse watches where one of these
     * earlier peaks can occasionally exceed the height of the actual C).
     *
     * burst_c_idx / burst_c_max track the envelope max ONLY in samples
     * past `c_search_skip_samples` from burst_start. If no peak is
     * found in that region (c_have_peak == 0), C falls back to
     * burst_max_idx (preserving behavior for very short bursts and
     * pre-sync recordings where the skip wasn't tuned).
     *
     * Default skip is 3 ms (covers the common "first sub-impulse
     * spike" case but stays below physical t_AC_min for any
     * reasonable amplitude). The library sets a longer, BPH-tuned
     * skip after sync (~80% of t_AC_min for max plausible amplitude). */
    uint64_t c_search_skip_samples;
    int      c_have_peak;
    double   burst_c_max;
    uint64_t burst_c_idx;
    double   burst_c_y_minus1;
    double   burst_c_y_plus1;
    int      c_have_peak_plus1;

    /* V5.4: C-onset detection via backward walk from burst_c_idx.
     *
     * env_ring is a small ring buffer of recent envelope samples used
     * to walk backward from the C peak at burst-emission time. We
     * walk back looking for the most recent sample where the envelope
     * crossed (rising) the half-of-peak threshold. Safeguards:
     *   - "Avoid small gaps": require min_dwell_samples consecutive
     *     samples below threshold before declaring an onset boundary
     *     (otherwise within-cluster ringing notches register as onsets).
     *   - "Don't go back too far": bound the walk to
     *     c_onset_search_max_samples. Beyond that we risk crossing
     *     into A territory.
     *
     * env_ring_capacity is chosen at init to cover the worst-case
     * search distance plus the burst duration that would precede
     * burst_c_idx in time (10 ms is plenty: at 48 kHz that's 480
     * samples; at 384 kHz it's 3840). */
    float   *env_ring;
    size_t   env_ring_capacity;
    size_t   env_ring_head;          /* next write position; wraps */
    uint64_t env_ring_newest_abs;    /* abs sample index of most-recent write; 0 if empty */
    int      env_ring_has_data;      /* 1 once at least one sample has been written */

    /* C-onset detection parameters. */
    uint64_t c_onset_dwell_samples;        /* default ~0.3 ms */
    uint64_t c_onset_search_max_samples;   /* default 5 ms pre-sync, ~t_AC/2 post-sync */

    /* misc */
    double   prev_sample;
    uint64_t last_event_sample;
    uint64_t total_samples;
} tg_detector_t;

/* Initialize with sample rate. All other parameters are derived or fixed. */
void tg_detector_init (tg_detector_t *d, double fs);

/* V5.4: free heap-allocated state (env ring buffer). Idempotent. */
void tg_detector_destroy(tg_detector_t *d);

void tg_detector_reset(tg_detector_t *d);

/* Process a block of envelope samples. Appends up to max_events - *out_count
 * events to out_events, increments *out_count, and returns the number of
 * events added in this call. */
size_t tg_detector_process(tg_detector_t *d,
                           const float *envelope, size_t n,
                           tg_raw_event_t *out_events,
                           size_t *out_count, size_t max_events);

/* Update the minimum-silence gate once BPH is known. Typical value:
 * 0.4 * beat_period. Until called, a 20 ms pre-sync default is used. */
void tg_detector_set_min_silence(tg_detector_t *d, double min_silence_s);

/* V4.5: set the minimum A-to-A wall-clock interval. Independent of
 * the silence gate (which counts time since burst end), this limits
 * new A onsets based on time since the previous A onset. Use to
 * reject sub-impulses within multi-impulse beat clusters once the
 * beat period is known.
 *
 * min_a_s = 0 disables (default, used pre-sync).
 * Library sets to ~0.7 * beat_period when sync acquires. */
void tg_detector_set_min_a_interval(tg_detector_t *d, double min_a_s);

/* V5.2: set the C-search skip window. Time since burst-start that
 * the C-peak tracker ignores before considering envelope samples as
 * candidate C peaks. Excludes the A's own impulse spike from being
 * mis-identified as C on multi-impulse watches.
 *
 * Default: 3 ms (a reasonable value below physical t_AC_min for any
 * watch that won't be knocking). Library can call this with a
 * BPH-tuned value once sync is acquired (e.g. 80% of theoretical
 * t_AC_min for the maximum plausible amplitude).
 *
 * If set to 0 or larger than the burst, the C-peak tracker won't
 * fire and C falls back to burst_max (V5.0 / V5.1 behavior). */
void tg_detector_set_c_search_skip(tg_detector_t *d, double skip_s);

/* V5.4: C-onset detection parameters.
 *
 * tg_detector_set_c_onset_search_max sets the backward-search bound
 * (seconds). If the half-height crossing isn't found within this
 * window before the C peak, onset_valid stays 0 on the emitted event
 * and the primary fields fall back to peak in TG_C_PLACEMENT_ONSET
 * mode.
 *
 * Library calls this at sync acquisition with t_AC_min/2 (post-sync)
 * and falls back to 5 ms pre-sync.
 *
 * tg_detector_set_c_onset_dwell sets the minimum below-threshold
 * dwell to avoid within-cluster ringing notches firing as onsets.
 * Default 0.3 ms. */
void tg_detector_set_c_onset_search_max(tg_detector_t *d, double max_s);
void tg_detector_set_c_onset_dwell     (tg_detector_t *d, double dwell_s);

/* Query the current threshold values (valid after processing has begun). */
void tg_detector_get_thresholds(const tg_detector_t *d,
                                double *onset_thr,
                                double *min_peak_thr,
                                double *eff_noise,
                                double *ref_peak);

/* Get/set the tunable gate fractions (clamped to [0.001, 0.9]).
 *
 * onset_fraction    : A is fired when envelope crosses
 *                     noise + onset_fraction * (ref_peak - noise).
 *                     Default 0.03.  Raise for noisy recordings where
 *                     ambient noise sits above the 75th-percentile
 *                     floor; lower for very quiet movements.
 *
 * min_peak_fraction : A burst whose peak is below
 *                     noise + min_peak_fraction * (ref_peak - noise)
 *                     is discarded as a noise bump.
 *                     Default 0.20.
 */
double tg_detector_get_onset_fraction   (const tg_detector_t *d);
double tg_detector_get_min_peak_fraction(const tg_detector_t *d);
void   tg_detector_set_onset_fraction   (tg_detector_t *d, double frac);
void   tg_detector_set_min_peak_fraction(tg_detector_t *d, double frac);

/* V5.6: regime-change reset coordination.
 *
 * The detector internally tracks burst peak history (separate from
 * the adaptive median ring). When a new peak arrives that's >=
 * TG_REGIME_RATIO (10x) times the rolling minimum of recent peaks
 * AND both are above TG_REGIME_FLOOR, the regime_reset_pending flag
 * is set. This is the detector's signal to the library that
 * "acoustic regime has changed -- the adaptive state is now stale."
 *
 * The library queries this via tg_detector_consume_regime_reset,
 * which atomically reads-and-clears the flag. On a true return,
 * the library flushes its higher-level state (event history,
 * sync/PLL state) and calls tg_detector_reset on the detector to
 * flush the adaptive thresholds.
 *
 * Returns 1 if a reset was pending (and clears the flag), 0 otherwise. */
int tg_detector_consume_regime_reset(tg_detector_t *d);

#endif
