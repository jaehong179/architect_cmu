/* Timegrapher.cpp -- Public API entry point and pipeline glue.
 *
 * This translation unit owns the public tg_* API and the orchestration
 * that wires the per-stage primitives (Dsp, Detector, Bph) into a
 * single streaming pipeline. The actual per-stage work happens in
 * the corresponding .cpp files.
 *
 * Pipeline (per call to tg_process):
 *
 *   1. DC blocker          -- single-pole HPF (Dsp.cpp), default 200 Hz.
 *                             Removes mic wind, table rumble, mains hum.
 *
 *   2. Envelope            -- full-wave rectify + 0.15 ms one-pole LPF
 *                             (Dsp.cpp). Output is the slow-moving
 *                             envelope of the impulse activity.
 *
 *   3. Detector            -- silence-based burst detector (Detector.cpp).
 *                             Emits A and C events with sub-sample
 *                             timing, plus V5.4 C-onset metadata.
 *
 *   4. BPH detection       -- Rayleigh phase-score over event history
 *                             (Bph.cpp). Once a candidate exceeds the
 *                             lock threshold AND clears the V5.3 median
 *                             A-to-A guard, sync state moves to SYNCED.
 *
 *   5. Sync PLL            -- once locked, a small period-tracking PLL
 *                             refines the beat period to follow drift.
 *                             Sync is lost after a configurable number
 *                             of consecutive missed expected events.
 *
 *   6. Delay-line / public events -- tg_event_t emitted to the caller.
 *                             A delay line is used to align reported
 *                             event times to the input PCM stream
 *                             (envelope LPF introduces a small group
 *                             delay; we compensate so the timestamps
 *                             line up with what the user sees in their
 *                             waveform display).
 *
 * The library only emits timing events here. Per-event amplitude,
 * predicted-time pairing gates, and window-averaged readouts live in
 * libtgan and libtgacf respectively, both built on top of this API.
 *
 * Thread safety: a single tg_context_t is owned by the calling thread.
 * Multiple streams = multiple contexts, no shared global state.
 *
 * V5.4 additions handled here:
 *   - tg_c_placement_t enum and the c_placement field on tg_config_t.
 *   - tg_set_c_placement / tg_get_c_placement runtime control.
 *   - Promotion of raw events (which always carry both peak and onset
 *     timings on C) to public tg_event_t per the c_placement mode.
 *     PEAK -> primary trio = peak; ONSET -> primary trio = onset (with
 *     silent fallback to peak if onset detection failed for that beat).
 *     Onset metadata fields are populated identically in both modes.
 *
 * V5.5: file renamed from timegrapher.c to Timegrapher.cpp; compiles
 * under C++17. The public API in Timegrapher.h is wrapped in
 * extern "C" so the ABI is identical to V5.4 for C, C++, Rust,
 * Python (cffi), and other foreign-function callers.
 *
 * V5.6: regime-change reset coordination. After each detector pass
 * we consume the detector's regime_reset_pending flag (via
 * tg_detector_consume_regime_reset). On a true return we flush our
 * library-level state -- event history, sync/PLL internals, cached
 * BPH/period -- and the detector's own adaptive state (peak median,
 * noise floor), while preserving the detector's sample clock and
 * env_ring abs-index reference so timestamps and the C-onset
 * backward walk stay correct. The triggering peak is stamped into
 * the regime ring so the next beat doesn't immediately re-trigger.
 * Surfaces as tg_result_t.detector_reset_event.
 */

#include "Timegrapher.h"
#include "Dsp.h"
#include "Detector.h"
#include "Bph.h"
#include "PerfInstrumentation.h"   // [PERF 계측 · §B-4] DSP 단계별 처리시간 (docs/PERF_LOG_GUIDE.md)

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TG_INITIAL_BUF      4096
#define TG_EVENT_HISTORY    256

struct tg_context {
    tg_config_t cfg;

    /* DSP chain */
    tg_hpf_t      hpf;
    tg_envelope_t env;
    tg_detector_t det;

    /* BPH and sync tracker */
    tg_sync_t     sync;
    int           current_bph;
    double        current_beat_period;
    double        current_ac_offset;

    /* Working buffers */
    float    *buf_filt;            /* HPF output            */
    float    *buf_env;             /* envelope (pre-delay)  */
    size_t    buf_capacity;

    /* Envelope delay line (50 ms, fixed) for event/PCM alignment */
    float    *delay_buf;
    size_t    delay_capacity;
    size_t    delay_samples;       /* effective delay       */
    size_t    delay_write_idx;
    size_t    delay_filled;
    uint64_t  total_env_samples_in;

    /* User-facing delayed envelope output buffer */
    float    *buf_env_out;
    size_t    buf_env_out_capacity;

    /* Raw events out of the detector for this batch */
    tg_raw_event_t *raw_events;
    size_t          raw_events_capacity;

    /* User-facing tg_event_t array for the current call */
    tg_event_t     *out_events;
    size_t          out_events_capacity;

    /* Rolling history of recent A event times for BPH detection */
    double   ev_history[TG_EVENT_HISTORY];
    size_t   ev_history_head;
    size_t   ev_history_count;

    uint64_t total_samples_processed;
};

/* ----- buffer helpers ------------------------------------------------ */

static int ensure_buf(tg_context_t *ctx, size_t n) {
    if (n <= ctx->buf_capacity) return 0;
    size_t new_cap = ctx->buf_capacity ? ctx->buf_capacity : TG_INITIAL_BUF;
    while (new_cap < n) new_cap *= 2;
    float *nf = (float*)realloc(ctx->buf_filt, new_cap * sizeof(float));
    if (!nf) return -1;
    ctx->buf_filt = nf;
    float *ne = (float*)realloc(ctx->buf_env, new_cap * sizeof(float));
    if (!ne) return -1;
    ctx->buf_env = ne;
    ctx->buf_capacity = new_cap;
    return 0;
}

static int ensure_env_out(tg_context_t *ctx, size_t n) {
    if (n <= ctx->buf_env_out_capacity) return 0;
    size_t new_cap = ctx->buf_env_out_capacity ? ctx->buf_env_out_capacity : TG_INITIAL_BUF;
    while (new_cap < n) new_cap *= 2;
    float *p = (float*)realloc(ctx->buf_env_out, new_cap * sizeof(float));
    if (!p) return -1;
    ctx->buf_env_out = p;
    ctx->buf_env_out_capacity = new_cap;
    return 0;
}

static int ensure_raw_events(tg_context_t *ctx, size_t n) {
    if (n <= ctx->raw_events_capacity) return 0;
    size_t new_cap = ctx->raw_events_capacity ? ctx->raw_events_capacity : 64;
    while (new_cap < n) new_cap *= 2;
    tg_raw_event_t *p = (tg_raw_event_t*)realloc(
            ctx->raw_events, new_cap * sizeof(tg_raw_event_t));
    if (!p) return -1;
    ctx->raw_events = p;
    ctx->raw_events_capacity = new_cap;
    return 0;
}

static int ensure_out_events(tg_context_t *ctx, size_t n) {
    if (n <= ctx->out_events_capacity) return 0;
    size_t new_cap = ctx->out_events_capacity ? ctx->out_events_capacity : 64;
    while (new_cap < n) new_cap *= 2;
    tg_event_t *p = (tg_event_t*)realloc(
            ctx->out_events, new_cap * sizeof(tg_event_t));
    if (!p) return -1;
    ctx->out_events = p;
    ctx->out_events_capacity = new_cap;
    return 0;
}

/* ----- envelope delay line ------------------------------------------- */

/* Implements a FIFO of fixed length D = ctx->delay_samples. Each input
 * sample is enqueued; if the queue has more than D samples, the oldest
 * is popped and written to `out`. */
static size_t delay_push_pop(tg_context_t *ctx,
                             const float *in, size_t n,
                             float *out)
{
    size_t cap = ctx->delay_capacity;
    size_t D   = ctx->delay_samples;
    if (cap == 0 || n == 0) return 0;
    size_t produced = 0;
    for (size_t i = 0; i < n; ++i) {
        if (ctx->delay_filled >= D) {
            size_t read_idx = (ctx->delay_write_idx + cap - D) % cap;
            out[produced++] = ctx->delay_buf[read_idx];
        } else {
            ctx->delay_filled++;
        }
        ctx->delay_buf[ctx->delay_write_idx] = in[i];
        ctx->delay_write_idx = (ctx->delay_write_idx + 1) % cap;
        ctx->total_env_samples_in++;
    }
    return produced;
}

/* ----- event history ring -------------------------------------------- */

static void push_event_history(tg_context_t *ctx, double t) {
    ctx->ev_history[ctx->ev_history_head] = t;
    ctx->ev_history_head = (ctx->ev_history_head + 1) % TG_EVENT_HISTORY;
    if (ctx->ev_history_count < TG_EVENT_HISTORY) ctx->ev_history_count++;
}

static void copy_history_linear(const tg_context_t *ctx, double *out) {
    size_t cnt = ctx->ev_history_count;
    if (cnt == 0) return;
    if (cnt < TG_EVENT_HISTORY) {
        memcpy(out, ctx->ev_history, cnt * sizeof(double));
    } else {
        size_t head = ctx->ev_history_head;
        size_t tail_len = TG_EVENT_HISTORY - head;
        memcpy(out,            ctx->ev_history + head, tail_len * sizeof(double));
        memcpy(out + tail_len, ctx->ev_history,        head     * sizeof(double));
    }
}

/* ============================ public API ============================ */

void tg_config_default(tg_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->sample_rate              = 48000.0;
    cfg->bph_mode                 = TG_BPH_MODE_AUTO;
    cfg->manual_bph               = 0;
    cfg->hpf_cutoff_hz            = 200.0;
    cfg->envelope_smooth_ms       = 0.15;
    cfg->event_min_separation_ms  = 2.0;
    cfg->sync_tolerance_pct       = 3.0;
    cfg->auto_detect_seconds      = 1.5;
    cfg->sync_loss_misses         = 12;
    cfg->pll_period_gain          = 0.01;
    cfg->pll_ac_gain              = 0.05;
    cfg->onset_fraction_init      = 0.0;
    cfg->min_peak_fraction_init   = 0.0;
    cfg->suppress_pre_sync_events = 0;
    cfg->c_placement              = TG_C_PLACEMENT_PEAK;  /* V5.4 default */
}

tg_context_t *tg_init(const tg_config_t *cfg_in) {
    if (!cfg_in || cfg_in->sample_rate <= 0.0) return NULL;

    if (cfg_in->bph_mode == TG_BPH_MODE_MANUAL &&
        !tg_is_valid_manual_bph(cfg_in->manual_bph)) {
        return NULL;
    }

    tg_context_t *ctx = (tg_context_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->cfg = *cfg_in;

    /* Apply zero-defaults at runtime */
    if (ctx->cfg.hpf_cutoff_hz           == 0.0) ctx->cfg.hpf_cutoff_hz           = 200.0;
    if (ctx->cfg.envelope_smooth_ms      == 0.0) ctx->cfg.envelope_smooth_ms      = 0.15;
    if (ctx->cfg.event_min_separation_ms == 0.0) ctx->cfg.event_min_separation_ms = 2.0;
    if (ctx->cfg.sync_tolerance_pct      == 0.0) ctx->cfg.sync_tolerance_pct      = 3.0;
    if (ctx->cfg.auto_detect_seconds     == 0.0) ctx->cfg.auto_detect_seconds     = 1.5;
    if (ctx->cfg.sync_loss_misses        == 0)   ctx->cfg.sync_loss_misses        = 12;
    if (ctx->cfg.pll_period_gain         == 0.0) ctx->cfg.pll_period_gain         = 0.01;
    if (ctx->cfg.pll_ac_gain             == 0.0) ctx->cfg.pll_ac_gain             = 0.05;

    tg_hpf_init     (&ctx->hpf, ctx->cfg.sample_rate, ctx->cfg.hpf_cutoff_hz);
    tg_envelope_init(&ctx->env, ctx->cfg.sample_rate, ctx->cfg.envelope_smooth_ms);
    tg_detector_init(&ctx->det, ctx->cfg.sample_rate);

    /* Init-time threshold fractions */
    if (ctx->cfg.onset_fraction_init    > 0.0)
        tg_detector_set_onset_fraction   (&ctx->det, ctx->cfg.onset_fraction_init);
    if (ctx->cfg.min_peak_fraction_init > 0.0)
        tg_detector_set_min_peak_fraction(&ctx->det, ctx->cfg.min_peak_fraction_init);

    tg_sync_init(&ctx->sync);

    /* Envelope delay line: 50 ms, sample-rate dependent. */
    ctx->delay_capacity = (size_t)(0.050 * ctx->cfg.sample_rate);
    if (ctx->delay_capacity < 16) ctx->delay_capacity = 16;
    ctx->delay_samples = ctx->delay_capacity;
    ctx->delay_buf = (float*)calloc(ctx->delay_capacity, sizeof(float));
    if (!ctx->delay_buf) { tg_destroy(ctx); return NULL; }

    if (ensure_buf(ctx, TG_INITIAL_BUF) || ensure_raw_events(ctx, 64) ||
        ensure_out_events(ctx, 64))
    {
        tg_destroy(ctx);
        return NULL;
    }
    return ctx;
}

void tg_destroy(tg_context_t *ctx) {
    if (!ctx) return;
    tg_detector_destroy(&ctx->det);   /* V5.4: free env_ring */
    free(ctx->buf_filt);
    free(ctx->buf_env);
    free(ctx->buf_env_out);
    free(ctx->delay_buf);
    free(ctx->raw_events);
    free(ctx->out_events);
    free(ctx);
}

int tg_process(tg_context_t *ctx,
               const float  *pcm, size_t num_samples,
               tg_result_t  *result)
{
    if (!ctx || !result) return -1;
    memset(result, 0, sizeof(*result));

    // ── [PERF 계측 · §B-4 · QA-RT-01] DSP 단계별 처리시간 측정 (측정 전용) ──
    //  HPF / 엔벨로프 / 검출 / 동기(BPH·sync·출력) 각 단계의 소요시간을 ms로 잰다.
    //  Pi에서 "신호처리 지연의 주범이 어느 단계인가"를 분리해 보기 위함.
    double pdHpf = 0.0, pdEnv = 0.0, pdDet = 0.0, pdSync = 0.0; // 각 단계 소요(ms)
    double pdSyncStart = 0.0;                                   // 동기 단계 시작 시각

    /* DSP -> envelope -> delay line.
     *
     * Detector reads the un-delayed envelope (so events have absolute
     * timing in the caller's input stream). The 50 ms delay line is
     * applied separately to produce the user-facing processed_pcm,
     * which lags events by ~50 ms but is aligned for plotting (each
     * event's sample_index, when subtracted from processed_pcm_start_sample,
     * gives the offset into the buffer where the impulse will appear).
     */
    size_t env_out_len = 0;
    if (num_samples > 0) {
        if (ensure_buf(ctx, num_samples) != 0) return -2;
        if (ensure_env_out(ctx, num_samples) != 0) return -2;
        double _pH0 = Perf::nowMs();                                            // [PERF §B-4] HPF 시작
        tg_hpf_process     (&ctx->hpf, pcm,            ctx->buf_filt, num_samples);
        double _pH1 = Perf::nowMs(); pdHpf = _pH1 - _pH0;                        // [PERF §B-4] HPF 소요
        tg_envelope_process(&ctx->env, ctx->buf_filt,  ctx->buf_env,  num_samples);
        env_out_len = delay_push_pop(ctx, ctx->buf_env, num_samples,
                                     ctx->buf_env_out);
        pdEnv = Perf::nowMs() - _pH1;                                           // [PERF §B-4] 엔벨로프+지연 소요
    }

    /* Compute absolute index of first sample of the delayed envelope */
    uint64_t input_end   = ctx->total_env_samples_in;
    uint64_t output_end  = (input_end > ctx->delay_samples)
                         ? (input_end - ctx->delay_samples) : 0;
    uint64_t output_start = output_end - env_out_len;

    result->processed_pcm              = ctx->buf_env_out;
    result->processed_pcm_len          = env_out_len;
    result->processed_pcm_start_sample = output_start;

    /* Detector reads the un-delayed envelope. Events emitted have
     * timing in the caller's absolute stream (in seconds since the
     * first input sample given to tg_init). */
    size_t raw_count = 0;
    if (num_samples > 0) {
        size_t window = (size_t)ctx->det.min_silence_samples;
        if (window < 16) window = 16;
        size_t worst = (2 * num_samples) / window + 4;
        if (ensure_raw_events(ctx, worst) != 0) return -2;
        size_t got = 0;
        double _pD0 = Perf::nowMs();                                            // [PERF §B-4] 검출 시작
        tg_detector_process(&ctx->det,
                            ctx->buf_env, num_samples,
                            ctx->raw_events,
                            &got, ctx->raw_events_capacity);
        pdDet = Perf::nowMs() - _pD0;                                           // [PERF §B-4] 검출 소요
        raw_count = got;
    }
    pdSyncStart = Perf::nowMs();   // [PERF §B-4] 이후 = 동기(BPH·sync·이벤트출력) 단계 시작

    /* V5.6: regime-change reset.
     *
     * The detector flags a regime change when a new burst peak is
     * >= 10x the rolling min of recent peaks (typical case: quiet
     * ambient prelude -> watch arriving on microphone). When that
     * happens the adaptive state (event history, sync, noise floor,
     * peak median ring) is polluted with values from the previous
     * regime and would take many seconds to recover.
     *
     * The fix: on a flagged change, flush all polluted adaptive
     * state, then surface a one-shot detector_reset_event so the
     * caller can update UI. The raw events emitted this batch are
     * kept (the triggering event is genuine -- the detector found
     * something loud enough to fire), but the BPH/sync state starts
     * fresh from this event onward.
     *
     * Note: tg_detector_reset internally re-seeds the regime ring
     * with the triggering peak so the next beat won't re-trigger. */
    if (tg_detector_consume_regime_reset(&ctx->det)) {
        result->detector_reset_event = 1;

        /* Capture the triggering peak (the most recent value pushed
         * into the regime ring) before flushing the detector, so we
         * can re-seed the ring after the reset. */
        double seed_peak = ctx->det.burst_max;

        /* Flush detector adaptive state. This clears peak_history,
         * noise_history, sync-internal state in the detector itself.
         * The env_ring is also cleared but stays allocated.
         *
         * IMPORTANT: tg_detector_reset zeros several pieces of
         * streaming state that are correct to zero at stream start
         * but wrong when called mid-stream from here:
         *
         *   - total_samples: the detector's sample clock. If reset,
         *     subsequent events would get stamped with absolute sample
         *     indices starting from 0, breaking timestamp continuity.
         *
         *   - env_ring_newest_abs / env_ring_has_data: the absolute
         *     sample-index reference for the C-onset backward-walk
         *     ring. If reset, the backward walk would compute wrong
         *     ring offsets (or skip the walk entirely until refilled).
         *
         * Save and restore these around the reset. */
        uint64_t saved_total = ctx->det.total_samples;
        uint64_t saved_env_newest = ctx->det.env_ring_newest_abs;
        int      saved_env_has   = ctx->det.env_ring_has_data;
        tg_detector_reset(&ctx->det);
        ctx->det.total_samples       = saved_total;
        ctx->det.env_ring_newest_abs = saved_env_newest;
        ctx->det.env_ring_has_data   = saved_env_has;

        /* Re-seed the regime ring so the next beat doesn't re-trip
         * the comparator. Without this, the first post-reset beat
         * would have an empty ring (min would be 0), and the second
         * beat would trip again. */
        ctx->det.regime_peak_ring[0] = seed_peak;
        ctx->det.regime_peak_count   = 1;
        ctx->det.regime_peak_head    = 1;

        /* Flush library-level state too: event history, sync state,
         * cached BPH and beat period. Use tg_sync_reset to fully
         * clear the PLL internals (next_a_time, period_est, etc.),
         * not just the synced flag -- otherwise the post-reset
         * watchdog uses stale predictions and immediately unsyncs. */
        ctx->current_bph         = 0;
        ctx->current_beat_period = 0.0;
        ctx->current_ac_offset   = 0.0;
        ctx->ev_history_count    = 0;
        ctx->ev_history_head     = 0;
        tg_sync_reset(&ctx->sync);

        /* Suppress the raw events from THIS batch. They were emitted
         * on stale thresholds just before the regime change was
         * detected; better to start fresh from the next batch. */
        raw_count = 0;
    }

    /* Push A events into BPH history */
    for (size_t i = 0; i < raw_count; ++i) {
        if (ctx->raw_events[i].is_onset)
            push_event_history(ctx, ctx->raw_events[i].time_seconds);
    }

    /* BPH detection: if not already synced, see if we have enough history. */
    int try_detect = 0;
    if (!ctx->sync.synced && ctx->ev_history_count >= 8) {
        double tmp[TG_EVENT_HISTORY];
        copy_history_linear(ctx, tmp);
        double earliest = tmp[0];
        double latest   = tmp[ctx->ev_history_count - 1];
        if (latest - earliest >= ctx->cfg.auto_detect_seconds) try_detect = 1;
    }

    if (!ctx->sync.synced && try_detect) {
        double tmp[TG_EVENT_HISTORY];
        copy_history_linear(ctx, tmp);
        int    matched = 0;
        double phase_score = 0.0;
        double matched_period = 0.0;
        if (ctx->cfg.bph_mode == TG_BPH_MODE_AUTO) {
            matched = tg_bph_pick_by_phase(tmp, ctx->ev_history_count,
                                           TG_AUTO_BPH_LIST, TG_AUTO_BPH_COUNT,
                                           0.7, &phase_score, &matched_period);
        } else {
            int u = ctx->cfg.manual_bph;
            double T = 3600.0 / (double)u;
            phase_score = tg_phase_score(tmp, ctx->ev_history_count, T);
            if (phase_score >= 0.7) {
                matched = u; matched_period = T;
            }
        }
        if (matched) {
            double half = 0.5 * matched_period;
            double sum_small = 0.0; size_t cnt_small = 0;
            for (size_t i = 1; i < ctx->ev_history_count; ++i) {
                double d = tmp[i] - tmp[i-1];
                if (d > 0.0 && d < half) { sum_small += d; cnt_small++; }
            }
            double ac = (cnt_small > 0) ? sum_small / (double)cnt_small
                                        : matched_period * 0.05;
            double tol = matched_period * ctx->cfg.sync_tolerance_pct * 0.01;
            double last_ev = tmp[ctx->ev_history_count - 1];
            tg_sync_lock(&ctx->sync, matched, matched_period, ac,
                         last_ev, tol, ctx->cfg.sync_loss_misses,
                         ctx->cfg.pll_period_gain, ctx->cfg.pll_ac_gain);
            ctx->current_bph         = matched;
            ctx->current_beat_period = matched_period;
            ctx->current_ac_offset   = ac;
            result->sync_acquired_event = 1;

            /* Tighten the silence and A-to-A gates now that BPH is known. */
            tg_detector_set_min_silence(&ctx->det, 0.4 * matched_period);
            tg_detector_set_min_a_interval(&ctx->det, 0.7 * matched_period);

            /* V5.2: tune C-search skip from beat period. The default
             * 3 ms handles the common "first sub-impulse" case. Once
             * BPH is known, scale to ~3% of beat period: long enough
             * to skip past common intermediate sub-impulses (which
             * appear within the first ~5 ms of a burst), short enough
             * to stay safely below the C peak at t_AC even for
             * knocking-amplitude watches.
             *
             * Approximate skip values (3% of period):
             *   18000 BPH (period 200 ms) -> 6.0 ms
             *   21600 BPH (period 167 ms) -> 5.0 ms
             *   28800 BPH (period 125 ms) -> 3.8 ms
             *   36000 BPH (period 100 ms) -> 3.0 ms
             *
             * For comparison, t_AC at 360 deg amplitude (knocking):
             *   18000 BPH lift 50 -> 9.2 ms
             *   21600 BPH lift 50 -> 7.4 ms
             *   28800 BPH lift 52 -> 5.7 ms */
            double skip_s = 0.03 * matched_period;
            tg_detector_set_c_search_skip(&ctx->det, skip_s);
        }
    }

    /* Run sync tracker and detect loss */
    int prev_synced = ctx->sync.synced;
    if (ctx->sync.synced) {
        for (size_t i = 0; i < raw_count; ++i) {
            tg_sync_update(&ctx->sync, ctx->raw_events[i].time_seconds);
            if (!ctx->sync.synced) break;
        }
    }
    /* Time-based sync loss watchdog */
    if (ctx->sync.synced) {
        double stream_t = (double)(input_end) / ctx->cfg.sample_rate;
        double tol = ctx->current_beat_period * ctx->cfg.sync_tolerance_pct * 0.01;
        if (stream_t > ctx->sync.next_a_time
            + ctx->cfg.sync_loss_misses * ctx->current_beat_period + tol)
        {
            ctx->sync.synced = 0;
        }
    }
    if (prev_synced && !ctx->sync.synced) {
        result->sync_lost_event = 1;
        ctx->current_bph = 0;
        ctx->current_beat_period = 0.0;
        ctx->current_ac_offset = 0.0;
        ctx->ev_history_count = 0;
        ctx->ev_history_head  = 0;
        tg_detector_set_min_silence  (&ctx->det, 0.020);
        tg_detector_set_min_a_interval(&ctx->det, 0.0);
        tg_detector_set_c_search_skip(&ctx->det, 0.003);  /* back to default */
    }

    /* Build sync_status */
    if (ctx->cfg.bph_mode == TG_BPH_MODE_MANUAL) {
        if (ctx->sync.synced) {
            result->sync_status  = TG_SYNC_SYNCED;
            result->detected_bph = ctx->cfg.manual_bph;
        } else if (try_detect) {
            result->sync_status  = TG_SYNC_MISMATCH;
            result->detected_bph = ctx->cfg.manual_bph;
        } else {
            result->sync_status  = TG_SYNC_NOT_SYNCED;
            result->detected_bph = ctx->cfg.manual_bph;
        }
    } else {
        if (ctx->sync.synced) {
            result->sync_status  = TG_SYNC_SYNCED;
            result->detected_bph = ctx->current_bph;
        } else {
            result->sync_status  = TG_SYNC_NOT_SYNCED;
            result->detected_bph = 0;
        }
    }
    result->measured_period_s = ctx->current_beat_period;

    /* Emit events: copy timing, set is_pre_sync, optionally drop. */
    size_t emit_count = 0;
    if (raw_count > 0) {
        if (ensure_out_events(ctx, raw_count) != 0) return -2;
        int pre_sync = (ctx->current_bph <= 0);
        for (size_t i = 0; i < raw_count; ++i) {
            if (pre_sync && ctx->cfg.suppress_pre_sync_events) continue;
            tg_event_t *ev = &ctx->out_events[emit_count++];
            const tg_raw_event_t *r = &ctx->raw_events[i];
            int is_a = r->is_onset;

            ev->type        = is_a ? TG_EVENT_A : TG_EVENT_C;
            ev->is_pre_sync = pre_sync;
            ev->peak_value  = r->peak_value;

            /* V5.4: choose primary timing per c_placement (C only).
             * For A events the raw timing IS the onset; nothing to
             * choose. For C events:
             *   PEAK  -> primary = peak (raw event's main fields)
             *   ONSET -> primary = onset, falling back to peak if
             *            onset detection failed for this beat */
            int use_onset_primary =
                (!is_a)
                && (ctx->cfg.c_placement == TG_C_PLACEMENT_ONSET)
                && (r->onset_valid != 0);

            if (use_onset_primary) {
                ev->sample_index      = r->onset_sample_index;
                ev->sub_sample_offset = r->onset_sub_sample_offset;
                ev->time_seconds      = r->onset_time_seconds;
            } else {
                ev->sample_index      = r->sample_index;
                ev->sub_sample_offset = r->sub_sample_offset;
                ev->time_seconds      = r->time_seconds;
            }

            /* V5.4: always populate onset metadata fields. For A
             * events these are zeroed at the detector. For C events
             * they reflect the onset detection result. */
            ev->onset_sample_index      = r->onset_sample_index;
            ev->onset_sub_sample_offset = r->onset_sub_sample_offset;
            ev->onset_time_seconds      = r->onset_time_seconds;
            ev->onset_valid             = r->onset_valid;
        }
    }
    result->events     = ctx->out_events;
    result->num_events = emit_count;

    /* Detector state for diagnostics */
    {
        double onset_thr, min_peak_thr, eff_noise, ref_peak;
        tg_detector_get_thresholds(&ctx->det, &onset_thr, &min_peak_thr,
                                   &eff_noise, &ref_peak);
        result->onset_threshold    = (float)onset_thr;
        result->min_peak_threshold = (float)min_peak_thr;
        result->noise_floor        = (float)eff_noise;
        result->reference_peak     = (float)ref_peak;
    }

    ctx->total_samples_processed += num_samples;

    // ── [PERF 계측 · §B-4 · QA-RT-01] DSP 단계별 시간 누적 → 1초마다 평균/최대 기록 ──
    //  per-call 로 다 남기면 양이 많아, 1초 윈도우로 평균+최대만 emit (저오버헤드).
    //  단일 스레드(메인 ProcessSamples)에서만 호출되므로 static 누적 안전.
    pdSync = Perf::nowMs() - pdSyncStart;   // 동기 단계 소요(이벤트 출력·임계 포함)
    {
        static double aH=0,aE=0,aD=0,aS=0, mH=0,mE=0,mD=0,mS=0;
        static long   aN=0; static double lastEmit=0; static int initE=0;
        aH+=pdHpf; aE+=pdEnv; aD+=pdDet; aS+=pdSync; aN++;
        if(pdHpf>mH)mH=pdHpf; if(pdEnv>mE)mE=pdEnv; if(pdDet>mD)mD=pdDet; if(pdSync>mS)mS=pdSync;
        double now = Perf::nowMs();
        if(!initE){ lastEmit=now; initE=1; }
        if(now-lastEmit >= 1000.0 && aN>0){
            Perf::log("B-4","QA-RT-01","dsp_hpf_ms",    aH/aN, "ms", QString("max=%1;n=%2").arg(mH,0,'f',3).arg(aN));
            Perf::log("B-4","QA-RT-01","dsp_env_ms",    aE/aN, "ms", QString("max=%1").arg(mE,0,'f',3));
            Perf::log("B-4","QA-RT-01","dsp_detect_ms", aD/aN, "ms", QString("max=%1").arg(mD,0,'f',3));
            Perf::log("B-4","QA-RT-01","dsp_sync_ms",   aS/aN, "ms", QString("max=%1").arg(mS,0,'f',3));
            Perf::log("B-4","QA-RT-01","dsp_total_ms",  (aH+aE+aD+aS)/aN, "ms", QString("n=%1").arg(aN));
            aH=aE=aD=aS=0; mH=mE=mD=mS=0; aN=0; lastEmit=now;
        }
    }
    return 0;
}

int tg_flush(tg_context_t *ctx, tg_result_t *result) {
    if (!ctx || !result) return -1;
    size_t n = ctx->delay_samples;
    if (ctx->det.burst_end_samples > n) n = ctx->det.burst_end_samples;
    n += 32;
    if (n == 0) { memset(result, 0, sizeof(*result)); return 0; }
    float *silent = (float*)calloc(n, sizeof(float));
    if (!silent) return -2;
    int rc = tg_process(ctx, silent, n, result);
    free(silent);
    return rc;
}

void tg_reset(tg_context_t *ctx) {
    if (!ctx) return;
    tg_hpf_reset     (&ctx->hpf);
    tg_envelope_reset(&ctx->env);
    tg_detector_reset(&ctx->det);
    tg_detector_set_min_silence  (&ctx->det, 0.020);
    tg_detector_set_min_a_interval(&ctx->det, 0.0);
    tg_sync_init(&ctx->sync);
    ctx->current_bph         = 0;
    ctx->current_beat_period = 0.0;
    ctx->current_ac_offset   = 0.0;
    ctx->ev_history_count    = 0;
    ctx->ev_history_head     = 0;
    if (ctx->delay_buf && ctx->delay_capacity)
        memset(ctx->delay_buf, 0, ctx->delay_capacity * sizeof(float));
    ctx->delay_write_idx       = 0;
    ctx->delay_filled          = 0;
    ctx->total_env_samples_in  = 0;
    ctx->total_samples_processed = 0;
}

double tg_get_onset_fraction   (const tg_context_t *ctx) {
    return ctx ? tg_detector_get_onset_fraction(&ctx->det) : 0.0;
}
double tg_get_min_peak_fraction(const tg_context_t *ctx) {
    return ctx ? tg_detector_get_min_peak_fraction(&ctx->det) : 0.0;
}
void   tg_set_onset_fraction   (tg_context_t *ctx, double frac) {
    if (ctx) tg_detector_set_onset_fraction(&ctx->det, frac);
}
void   tg_set_min_peak_fraction(tg_context_t *ctx, double frac) {
    if (ctx) tg_detector_set_min_peak_fraction(&ctx->det, frac);
}

/* V5.4 */
tg_c_placement_t tg_get_c_placement(const tg_context_t *ctx) {
    return ctx ? ctx->cfg.c_placement : TG_C_PLACEMENT_PEAK;
}
void tg_set_c_placement(tg_context_t *ctx, tg_c_placement_t mode) {
    if (!ctx) return;
    if (mode != TG_C_PLACEMENT_PEAK && mode != TG_C_PLACEMENT_ONSET) return;
    ctx->cfg.c_placement = mode;
}
