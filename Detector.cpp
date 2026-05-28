/* Detector.cpp -- silence-based burst detector.
 *
 * See Detector.h for the full algorithm description and field-by-field
 * struct documentation. In brief:
 *
 *   1. An envelope sample in silence goes into a 256-slot ring buffer
 *      (one sample per ms); the 75th percentile of that buffer is the
 *      effective noise floor. This is robust to occasional spikes from
 *      microphone bumps or background noise.
 *
 *   2. The reference peak level is the median of the last 16 accepted
 *      burst peaks. Median (not mean) so a single anomalously loud or
 *      quiet beat doesn't move the working threshold.
 *
 *   3. Threshold = noise_floor + fraction * (ref_peak - noise_floor)
 *      with fractions 3% (onset), 2% (release), 20% (min-peak). The
 *      onset fraction is intentionally low because watch impulses
 *      have sharp leading edges -- we want to catch them as early as
 *      possible, then the higher min-peak fraction screens out
 *      anything that didn't actually rise into a real impulse.
 *
 *   4. State machine: SILENCE <-> BURST. Transitions gated by
 *      wall-clock time since previous burst ended (silence gate) and
 *      since current burst peak was seen (burst-end gate). This makes
 *      the detector robust to noisy envelopes that wobble across a
 *      threshold many times -- only sustained crossings count.
 *
 *   5. On accepted burst, emits A (onset-crossing time, linear
 *      interpolation between the sample below threshold and the first
 *      above) and C (peak time, parabolic interpolation across the
 *      maximum and its two neighbors). A is deferred until burst end
 *      because we need to know the burst's max amplitude before we
 *      can trust it to be a real beat (rejecting spurious crossings
 *      that never grew into a full impulse).
 *
 *   6. V5.2 C-search-skip: within an accepted burst, the C peak is
 *      sought only past a small skip window from burst start, so we
 *      don't mis-place C onto the A's own decay tail or onto an
 *      intermediate sub-impulse. The skip is 3% of the beat period
 *      once BPH is known, falling back to 3 ms pre-sync.
 *
 *   7. V5.4 C-onset detection: at burst-emission time, walk backward
 *      from the C peak in a small envelope ring buffer to find the
 *      half-height crossing on the C cluster's rising edge. Two
 *      safeguards keep the walk reliable: a minimum below-threshold
 *      dwell (so within-cluster ringing notches don't register as
 *      onsets) and a search bound (so the walk can't cross out of
 *      the C cluster into A territory). The result is attached to
 *      the emitted event in onset_* fields. Whether onset or peak is
 *      reported as the primary timing depends on c_placement mode
 *      handled in Timegrapher.cpp.
 *
 * V5.5: file renamed from detector.c to Detector.cpp; compiles under
 * C++17. No algorithmic change. The detector is plain procedural code
 * (no constructors, no exceptions, no STL) so the C->C++ rename is
 * essentially cosmetic at the source level.
 *
 * V5.6: regime-change detector. A new short ring (regime_peak_ring,
 * 8 entries) tracks recent burst peaks. When a new peak is >= 10x
 * the rolling minimum AND both are above 0.001, the detector sets
 * regime_reset_pending. The library reads-and-clears this flag via
 * tg_detector_consume_regime_reset and flushes adaptive state to
 * recover from polluted preludes (e.g. ambient noise before the
 * watch arrives on the microphone). One reset per second max via
 * an internal cooldown using total_samples + regime_last_reset_idx.
 */
#include "Detector.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void tg_detector_reset(tg_detector_t *d) {
    d->in_burst              = 0;
    d->burst_start_idx       = 0;
    d->burst_start_time      = 0.0;
    d->burst_start_offset    = 0.0;
    d->burst_max             = 0.0;
    d->burst_max_idx         = 0;
    d->burst_max_y_minus1    = 0.0;
    d->burst_max_y_plus1     = 0.0;
    d->have_peak_plus1       = 0;
    /* V5.2 */
    d->c_have_peak           = 0;
    d->burst_c_max           = 0.0;
    d->burst_c_idx           = 0;
    d->burst_c_y_minus1      = 0.0;
    d->burst_c_y_plus1       = 0.0;
    d->c_have_peak_plus1     = 0;
    d->prev_sample           = 0.0;
    d->last_event_sample     = 0;
    d->total_samples         = 0;
    /* Start with a large virtual silence credit so the first real tick
     * isn't blocked by the silence gate. */
    d->silence_samples       = 1ULL << 30;

    /* V4.5: A-to-A counter starts large too so first real tick passes
     * the gate. The min_a_interval_samples *threshold* is preserved
     * (it's a config setting, not runtime state). */
    d->samples_since_last_a  = 1ULL << 30;

    /* Bootstrap fallbacks */
    d->noise_floor           = 1e-6;
    d->signal_ceiling        = 1e-4;

    /* Clear both history buffers and their caches */
    for (int i = 0; i < TG_PEAK_HISTORY_N; ++i)  d->peak_history[i]  = 0.0;
    d->peak_history_count    = 0;
    d->peak_history_head     = 0;
    d->median_peak_cache     = 0.0;
    for (int i = 0; i < TG_NOISE_HISTORY_N; ++i) d->noise_history[i] = 0.0;
    d->noise_history_count   = 0;
    d->noise_history_head    = 0;
    d->noise_last_sample_idx = 0;
    d->noise_percentile_cache = 0.0;

    /* V5.6: regime-change detector. Clear ring and reset state. The
     * last_reset_idx is NOT cleared here (it tracks history that lives
     * across resets to enforce cooldown). Use tg_detector_init for a
     * full wipe of the regime tracker too. */
    for (int i = 0; i < TG_REGIME_RING_N; ++i) d->regime_peak_ring[i] = 0.0;
    d->regime_peak_count    = 0;
    d->regime_peak_head     = 0;
    d->regime_reset_pending = 0;

    /* V5.4: clear envelope ring buffer (don't free; kept across resets). */
    if (d->env_ring && d->env_ring_capacity > 0) {
        for (size_t i = 0; i < d->env_ring_capacity; ++i) d->env_ring[i] = 0.0f;
    }
    d->env_ring_head        = 0;
    d->env_ring_newest_abs  = 0;
    d->env_ring_has_data    = 0;
}

void tg_detector_init(tg_detector_t *d, double fs) {
    d->fs = fs;

    /* EMA time constants used only for bootstrap fallbacks (until the
     * percentile and median buffers have enough samples). */
    d->noise_alpha = 1.0 - exp(-1.0 / (0.3 * fs));  /* tau 0.3 s */
    d->ceil_alpha  = 1.0 - exp(-1.0 / (1.5 * fs));  /* tau 1.5 s */

    d->warmup_samples = (uint64_t)(0.2 * fs);  /* skip first 200 ms */

    /* Pre-sync defaults:
     *   min_silence = 20 ms (minimum time between beats before lock)
     *   burst_end   = 10 ms (minimum time past peak before emitting C)
     * These get tightened once BPH is locked; see
     * tg_detector_set_min_silence(). */
    d->min_silence_samples = (uint64_t)(0.020 * fs);
    d->burst_end_samples   = (uint64_t)(0.010 * fs);

    /* Sample the envelope into the noise-percentile buffer once per ~1 ms
     * during silence. 256 slots -> ~250 ms of recent silence history. */
    d->noise_sample_interval = (uint64_t)(0.001 * fs);
    if (d->noise_sample_interval < 1) d->noise_sample_interval = 1;

    /* Default gate fractions. These are the scale-invariant knobs that
     * determine detection sensitivity. */
    d->onset_fraction    = 0.03;
    d->min_peak_fraction = 0.20;

    /* V4.5: A-to-A interval gate disabled at init / pre-sync. Library
     * sets it once BPH is known via tg_detector_set_min_a_interval. */
    d->min_a_interval_samples = 0;

    /* V5.2: C-search skip default. 3 ms covers the common multi-impulse
     * "first sub-impulse spike" cases while staying below physical
     * t_AC_min for any reasonable amplitude. Library may tighten this
     * with tg_detector_set_c_search_skip() once BPH is known. */
    d->c_search_skip_samples = (uint64_t)(0.003 * fs);

    /* V5.4: C-onset detection defaults.
     * Search bound 5 ms pre-sync (library tightens to t_AC_min/2 once
     * BPH is known). Dwell 0.3 ms below half-height to skip
     * within-cluster ringing notches. */
    d->c_onset_search_max_samples = (uint64_t)(0.005 * fs);
    d->c_onset_dwell_samples      = (uint64_t)(0.0003 * fs);
    if (d->c_onset_dwell_samples < 2) d->c_onset_dwell_samples = 2;

    /* Allocate the recent-envelope ring buffer. Size to cover:
     *   - burst_end_samples (~25 ms post-peak before emit) — this
     *     is when find_c_onset runs, so peak is already that old
     *   - the backward search window itself (up to 5 ms)
     *   - margin for the burst's pre-peak portion
     *
     * 50 ms covers all known watch BPHs with comfortable headroom.
     * Cost: ~2.4 KB at 48 kHz, ~19 KB at 384 kHz. */
    if (d->env_ring) {
        free(d->env_ring);
        d->env_ring = NULL;
    }
    d->env_ring_capacity = (size_t)(0.050 * fs);
    if (d->env_ring_capacity < 256) d->env_ring_capacity = 256;
    d->env_ring          = (float*)calloc(d->env_ring_capacity, sizeof(float));
    d->env_ring_head     = 0;
    d->env_ring_newest_abs = 0;
    d->env_ring_has_data   = 0;

    /* V5.6: regime-change detector. last_reset_idx is set once at init
     * to 0 (meaning "no reset has happened yet"). tg_detector_reset
     * does not clear this -- it persists across resets to enforce
     * the cooldown gate, so a reset can't be triggered again
     * immediately. */
    d->regime_last_reset_idx = 0;

    tg_detector_reset(d);
}

void tg_detector_destroy(tg_detector_t *d) {
    if (!d) return;
    if (d->env_ring) {
        free(d->env_ring);
        d->env_ring = NULL;
    }
    d->env_ring_capacity   = 0;
    d->env_ring_head       = 0;
    d->env_ring_newest_abs = 0;
    d->env_ring_has_data   = 0;
}

void tg_detector_set_c_search_skip(tg_detector_t *d, double skip_s) {
    if (skip_s < 0.0) skip_s = 0.0;
    d->c_search_skip_samples = (uint64_t)(skip_s * d->fs);
}

void tg_detector_set_c_onset_search_max(tg_detector_t *d, double max_s) {
    if (!d) return;
    if (max_s < 0.0) max_s = 0.0;
    uint64_t s = (uint64_t)(max_s * d->fs);
    /* Cap to ring capacity - dwell margin so the walk can't run off
     * the buffer's tail. */
    if (s + d->c_onset_dwell_samples >= d->env_ring_capacity) {
        s = (d->env_ring_capacity > d->c_onset_dwell_samples + 1)
                ? (d->env_ring_capacity - d->c_onset_dwell_samples - 1)
                : 0;
    }
    d->c_onset_search_max_samples = s;
}

void tg_detector_set_c_onset_dwell(tg_detector_t *d, double dwell_s) {
    if (!d) return;
    if (dwell_s < 0.0) dwell_s = 0.0;
    uint64_t s = (uint64_t)(dwell_s * d->fs);
    if (s < 2) s = 2;
    d->c_onset_dwell_samples = s;
}

void tg_detector_set_min_silence(tg_detector_t *d, double min_silence_s) {
    uint64_t s = (uint64_t)(min_silence_s * d->fs);
    if (s < 2) s = 2;
    d->min_silence_samples = s;
    /* burst_end is half of min_silence: long enough to declare the tick
     * over, short enough that the next beat's A is always well outside. */
    d->burst_end_samples = s / 2;
    if (d->burst_end_samples < 2) d->burst_end_samples = 2;
}

void tg_detector_set_min_a_interval(tg_detector_t *d, double min_a_s) {
    if (!d) return;
    if (min_a_s <= 0.0) {
        d->min_a_interval_samples = 0;  /* disable */
    } else {
        d->min_a_interval_samples = (uint64_t)(min_a_s * d->fs);
    }
}

static inline double parabolic_offset(double y_m1, double y_0, double y_p1) {
    double denom = (y_m1 - 2.0 * y_0 + y_p1);
    if (fabs(denom) < 1e-20) return 0.0;
    double off = 0.5 * (y_m1 - y_p1) / denom;
    if (off < -0.5) off = -0.5;
    if (off >  0.5) off =  0.5;
    return off;
}

/* V5.4: read the envelope sample at absolute index `abs` from the
 * ring buffer. The ring stores the most recent `env_ring_capacity`
 * samples ending at `env_ring_newest_abs` (the abs index of the last
 * sample written). Returns 0 if `abs` is out of range. */
static inline double env_ring_at(const tg_detector_t *d, uint64_t abs) {
    if (!d->env_ring || d->env_ring_capacity == 0) return 0.0;
    if (!d->env_ring_has_data)                     return 0.0;
    uint64_t newest_abs = d->env_ring_newest_abs;
    if (abs > newest_abs) return 0.0;
    uint64_t age = newest_abs - abs;
    if (age >= d->env_ring_capacity) return 0.0;
    /* The most-recent slot is at (head - 1) mod cap. */
    size_t newest_slot = (d->env_ring_head + d->env_ring_capacity - 1)
                            % d->env_ring_capacity;
    size_t idx = (newest_slot + d->env_ring_capacity - age)
                            % d->env_ring_capacity;
    return (double)d->env_ring[idx];
}

/* V5.4: find the C-cluster onset by walking backward from the C peak.
 *
 * Algorithm:
 *   threshold = 0.5 * c_peak_value
 *   walk backward up to search_max samples
 *     count consecutive samples with envelope < threshold
 *     when count >= dwell_samples, the onset is the last sample
 *       (in time) where envelope was >= threshold
 *
 * Returns 1 with onset_idx_out / onset_sub_off_out filled in on
 * success, 0 if onset wasn't found (envelope never dipped sufficiently
 * before peak, or search ran past the bound).
 *
 * onset_sub_off_out is a linear-interpolation refinement on the
 * threshold crossing, in [-0.5, +0.5].
 */
static int find_c_onset(const tg_detector_t *d,
                        uint64_t  c_peak_idx,
                        double    c_peak_value,
                        uint64_t *onset_idx_out,
                        double   *onset_sub_off_out)
{
    if (!d->env_ring || d->env_ring_capacity == 0) return 0;
    if (c_peak_idx == 0)                            return 0;

    double threshold = 0.5 * c_peak_value;
    if (threshold <= 0.0) return 0;

    uint64_t search_max = d->c_onset_search_max_samples;
    if (search_max == 0) return 0;

    /* Don't walk past where the burst started + skip window: that's
     * by definition before the C-search region. */
    uint64_t earliest_idx;
    {
        uint64_t skip_end = d->burst_start_idx + d->c_search_skip_samples;
        uint64_t window_lo = (c_peak_idx > search_max)
                                 ? (c_peak_idx - search_max) : 0;
        earliest_idx = (skip_end > window_lo) ? skip_end : window_lo;
    }
    /* Also bounded by what the ring buffer holds. */
    {
        uint64_t ring_oldest = (d->env_ring_newest_abs >= d->env_ring_capacity - 1)
                                   ? (d->env_ring_newest_abs - (d->env_ring_capacity - 1))
                                   : 0;
        if (earliest_idx < ring_oldest) earliest_idx = ring_oldest;
    }
    if (earliest_idx >= c_peak_idx) return 0;

    /* Walk back: i is the candidate sample index. Track consecutive
     * below-threshold samples. The "last >= threshold" tracker holds
     * the index of the most recent sample we've seen with env >= thr,
     * which becomes the onset boundary when dwell is reached. */
    uint64_t consecutive_below = 0;
    int      have_above        = 0;
    uint64_t last_above_idx    = 0;
    double   last_above_val    = 0.0;

    /* Start from one sample earlier than the peak (peak itself is
     * above threshold by construction). Walk down toward earliest_idx. */
    for (uint64_t i = c_peak_idx; i > earliest_idx; ) {
        --i;
        double v = env_ring_at(d, i);
        if (v >= threshold) {
            have_above        = 1;
            last_above_idx    = i;
            last_above_val    = v;
            consecutive_below = 0;
        } else {
            consecutive_below++;
            if (have_above && consecutive_below >= d->c_onset_dwell_samples) {
                /* Onset is the threshold crossing between the sample
                 * just before last_above_idx (env < thr) and
                 * last_above_idx (env >= thr). Linear interp on
                 * threshold-crossing. */
                double v_prev = (last_above_idx > 0)
                                    ? env_ring_at(d, last_above_idx - 1)
                                    : 0.0;
                double frac = 0.0;
                if (last_above_val > v_prev) {
                    /* monotonic rise across the crossing */
                    frac = (threshold - v_prev) / (last_above_val - v_prev);
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                }
                /* sample_index convention: integer index just at or
                 * after the crossing; sub_off in [-0.5, +0.5]. */
                *onset_idx_out     = last_above_idx;
                *onset_sub_off_out = frac - 1.0;  /* in [-1, 0] */
                /* Renormalize to canonical [-0.5, +0.5] range by
                 * shifting integer index when fraction < -0.5. */
                if (*onset_sub_off_out < -0.5) {
                    *onset_sub_off_out += 1.0;
                    if (*onset_idx_out > 0) (*onset_idx_out)--;
                }
                return 1;
            }
        }
    }
    /* Reached the search bound without finding a clean dwell. */
    return 0;
}

/* Insertion-sort a small array in place. N <= 16 so this is fine. */
static void insertion_sort(double *a, int n) {
    for (int i = 1; i < n; ++i) {
        double x = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > x) { a[j+1] = a[j]; --j; }
        a[j+1] = x;
    }
}

/* Recompute cached median after inserting a peak. Called infrequently. */
static void update_median_cache(tg_detector_t *d) {
    int n = d->peak_history_count;
    if (n < 1) { d->median_peak_cache = 0.0; return; }
    double tmp[TG_PEAK_HISTORY_N];
    for (int i = 0; i < n; ++i) tmp[i] = d->peak_history[i];
    insertion_sort(tmp, n);
    if (n & 1) {
        d->median_peak_cache = tmp[n/2];
    } else {
        d->median_peak_cache = 0.5 * (tmp[n/2 - 1] + tmp[n/2]);
    }
}

/* V5.6: regime-change detector helpers.
 *
 * The regime ring tracks recent burst peak values. When a new peak
 * arrives that's at least TG_REGIME_RATIO (10x) larger than the
 * minimum of the ring, we conclude the acoustic regime has changed
 * (e.g. quiet ambient noise -> watch on microphone) and trip a
 * reset request. The library checks the reset_pending flag after
 * each detector_process and, if set, flushes adaptive state.
 *
 * Suppression gates:
 *   - Need >= 4 prior peaks in the ring before considering a trip
 *     (otherwise the first few peaks of a fresh stream would always
 *     trip on themselves).
 *   - Both old min and new peak must be above TG_REGIME_FLOOR
 *     (default 0.001). Below that we're comparing noise to noise.
 *   - Cooldown: TG_REGIME_COOLDOWN_S (1 s) between resets, tracked
 *     by sample-index distance. Stops consecutive loud impulses
 *     from each triggering their own reset and preventing sync
 *     from ever settling.
 */
static double regime_ring_min(const tg_detector_t *d) {
    if (d->regime_peak_count <= 0) return 0.0;
    double m = d->regime_peak_ring[0];
    for (int i = 1; i < d->regime_peak_count; ++i) {
        if (d->regime_peak_ring[i] < m) m = d->regime_peak_ring[i];
    }
    return m;
}

static void regime_push_peak(tg_detector_t *d, double peak, uint64_t abs_idx) {
    /* Check for regime trip BEFORE storing the new peak: we want to
     * compare against the prior baseline, not include the trigger in
     * the comparison. */
    if (d->regime_peak_count >= 4) {
        double prev_min = regime_ring_min(d);
        int above_floor = (peak       >= TG_REGIME_FLOOR &&
                           prev_min   >= TG_REGIME_FLOOR);
        int abs_floor_jump = (peak >= TG_REGIME_FLOOR &&
                              prev_min < TG_REGIME_FLOOR);
        /* Trip if both peaks are above the floor and the ratio is
         * large enough, OR if the new peak is above the floor and
         * the old min was below it (jump from noise to signal). */
        int ratio_ok = above_floor && (peak >= TG_REGIME_RATIO * prev_min);
        int trip = ratio_ok || abs_floor_jump;
        /* Apply cooldown: skip the trip if we recently reset. */
        if (trip && d->regime_last_reset_idx > 0) {
            uint64_t since = abs_idx - d->regime_last_reset_idx;
            uint64_t cooldown = (uint64_t)(TG_REGIME_COOLDOWN_S * d->fs);
            if (since < cooldown) trip = 0;
        }
        if (trip) {
            d->regime_reset_pending = 1;
            d->regime_last_reset_idx = abs_idx;
        }
    }
    /* Always store the new peak into the ring (regardless of trip). */
    d->regime_peak_ring[d->regime_peak_head] = peak;
    d->regime_peak_head = (d->regime_peak_head + 1) % TG_REGIME_RING_N;
    if (d->regime_peak_count < TG_REGIME_RING_N) d->regime_peak_count++;
}

/* Push a newly-observed peak into the ring buffer and refresh median.
 * V5.6: also feed the regime-change detector. */
static void push_peak(tg_detector_t *d, double peak, uint64_t abs_idx) {
    d->peak_history[d->peak_history_head] = peak;
    d->peak_history_head = (d->peak_history_head + 1) % TG_PEAK_HISTORY_N;
    if (d->peak_history_count < TG_PEAK_HISTORY_N) d->peak_history_count++;
    update_median_cache(d);
    regime_push_peak(d, peak, abs_idx);
}

/* Compute 75th percentile of noise_history using partial sort. */
static void update_noise_percentile_cache(tg_detector_t *d) {
    int n = d->noise_history_count;
    if (n < 1) { d->noise_percentile_cache = 0.0; return; }
    /* Copy & sort. N <= 256, O(N^2) insertion sort is fine (~65k ops
     * worst case, once per millisecond of silence). */
    double tmp[TG_NOISE_HISTORY_N];
    for (int i = 0; i < n; ++i) tmp[i] = d->noise_history[i];
    insertion_sort(tmp, n);
    /* 75th percentile index */
    int idx = (3 * (n - 1)) / 4;
    d->noise_percentile_cache = tmp[idx];
}

/* Record one downsampled silence-region envelope sample. Called at most
 * once per d->noise_sample_interval samples, only when not in_burst. */
static void push_noise_sample(tg_detector_t *d, double e) {
    d->noise_history[d->noise_history_head] = e;
    d->noise_history_head = (d->noise_history_head + 1) % TG_NOISE_HISTORY_N;
    if (d->noise_history_count < TG_NOISE_HISTORY_N) d->noise_history_count++;
    update_noise_percentile_cache(d);
}

/* Return the current "effective noise floor" for threshold computation.
 *
 * Bootstrap behavior: until we have at least 32 silence samples in the
 * history (~32 ms), fall back to the EMA-tracked min-like noise floor in
 * d->noise_floor. Once the percentile buffer has enough data, use the
 * 75th percentile instead -- this tracks the typical ambient level
 * between ticks rather than its minimum, so the onset threshold sits
 * ABOVE ambient noise instead of within it. */
static double effective_noise(const tg_detector_t *d) {
    if (d->noise_history_count < 32) {
        return d->noise_floor;
    }
    /* Floor the percentile at the EMA noise to avoid pathology if
     * history gets filled with zeros during warmup. */
    double p = d->noise_percentile_cache;
    double f = d->noise_floor;
    return (p > f) ? p : f;
}

/* Compute the current reference peak level used to derive thresholds.
 *
 * Phases:
 *   1. No peaks yet (count == 0):      use signal_ceiling (max-hold).
 *      Needed so initial thresholds are meaningful before any peak has
 *      been committed to history.
 *   2. Bootstrapping (count 1..3):     use max of peak_history.
 *      Avoids median-of-small-sample bias; still robust to single
 *      spurious-first-burst as we've accepted at most a few.
 *   3. Normal operation (count >= 4):  use median of peak_history.
 *      Robust to outliers and sparse-period drift.
 *
 * Always clamped to at least 10 * noise_floor so the threshold calc
 * doesn't collapse below the noise envelope. */
static double reference_peak(const tg_detector_t *d) {
    double floor_ref = 10.0 * d->noise_floor;
    double r;
    if (d->peak_history_count == 0) {
        r = d->signal_ceiling;
    } else if (d->peak_history_count < 4) {
        r = 0.0;
        for (int i = 0; i < d->peak_history_count; ++i) {
            if (d->peak_history[i] > r) r = d->peak_history[i];
        }
    } else {
        r = d->median_peak_cache;
    }
    return (r > floor_ref) ? r : floor_ref;
}

/* Compute the current thresholds from the noise floor and reference
 * peak. Used both inside the main loop and by the public getter. */
static void compute_thresholds(const tg_detector_t *d,
                               double *eff_noise,
                               double *ref_peak,
                               double *span,
                               double *onset_thr,
                               double *min_peak_thr)
{
    double n  = effective_noise(d);
    double r  = reference_peak(d);
    double sp = r - n;
    if (sp < n * 2.0) sp = n * 2.0;
    if (eff_noise)    *eff_noise    = n;
    if (ref_peak)     *ref_peak     = r;
    if (span)         *span         = sp;
    if (onset_thr)    *onset_thr    = n + d->onset_fraction    * sp;
    if (min_peak_thr) *min_peak_thr = n + d->min_peak_fraction * sp;
}

void tg_detector_get_thresholds(const tg_detector_t *d,
                                double *onset_thr,
                                double *min_peak_thr,
                                double *eff_noise,
                                double *ref_peak)
{
    compute_thresholds(d, eff_noise, ref_peak, NULL, onset_thr, min_peak_thr);
}

double tg_detector_get_onset_fraction(const tg_detector_t *d) {
    return d ? d->onset_fraction : 0.0;
}

double tg_detector_get_min_peak_fraction(const tg_detector_t *d) {
    return d ? d->min_peak_fraction : 0.0;
}

static double clamp_fraction(double f) {
    if (f < 0.001) f = 0.001;
    if (f > 0.9)   f = 0.9;
    return f;
}

void tg_detector_set_onset_fraction(tg_detector_t *d, double frac) {
    if (d) d->onset_fraction = clamp_fraction(frac);
}

void tg_detector_set_min_peak_fraction(tg_detector_t *d, double frac) {
    if (d) d->min_peak_fraction = clamp_fraction(frac);
}

size_t tg_detector_process(tg_detector_t *d,
                           const float *envelope, size_t n,
                           tg_raw_event_t *out_events,
                           size_t *out_count, size_t max_events)
{
    size_t produced = 0;

    for (size_t i = 0; i < n; ++i) {
        double e = (double)envelope[i];
        uint64_t abs_idx = d->total_samples + i;

        /* V5.4: write current envelope sample into the ring buffer.
         * Used by the C-onset backward walk at burst-emission time. */
        if (d->env_ring && d->env_ring_capacity > 0) {
            d->env_ring[d->env_ring_head] = (float)e;
            d->env_ring_head = (d->env_ring_head + 1) % d->env_ring_capacity;
            d->env_ring_newest_abs = abs_idx;
            d->env_ring_has_data   = 1;
        }

        /* EMA-tracked noise floor: snap-down min-tracker, used only as
         * bootstrap fallback before the percentile buffer fills. */
        if (!d->in_burst) {
            if (e < d->noise_floor) {
                d->noise_floor = e;
            } else {
                d->noise_floor += d->noise_alpha * (e - d->noise_floor);
            }
            if (d->noise_floor < 1e-9) d->noise_floor = 1e-9;

            /* Record one silence sample per ~1ms into the noise percentile
             * history. These are our observations of the typical ambient
             * level between ticks. Only collected during silence so burst
             * energy doesn't leak in. */
            if (abs_idx >= d->noise_last_sample_idx + d->noise_sample_interval) {
                push_noise_sample(d, e);
                d->noise_last_sample_idx = abs_idx;
            }
        }

        /* Keep the legacy signal_ceiling updated (for diagnostics). */
        if (e > d->signal_ceiling) {
            d->signal_ceiling = e;
        } else {
            d->signal_ceiling += d->ceil_alpha * (e - d->signal_ceiling);
        }
        if (d->signal_ceiling < d->noise_floor * 3.0) {
            d->signal_ceiling = d->noise_floor * 3.0;
        }

        if (abs_idx < d->warmup_samples) {
            d->prev_sample = e;
            continue;
        }

        /* Thresholds anchored to median of recent peak heights.
         *
         * reference_peak() returns either:
         *   - 10 * noise_floor (no history yet)
         *   - 0.3 * recent_max (1..3 peaks: bootstrapping)
         *   - median of last N peaks (>=4 peaks: normal operation)
         *
         * Unlike the old max-hold ceiling, this is robust to both single
         * loud outliers (median doesn't move) and long quiet stretches
         * (median doesn't decay). After a loud bang, the onset threshold
         * stays anchored to the typical tick peak, not the bang, so
         * subsequent quieter real ticks still clear it. After a gap with
         * no ticks, the reference stays at the typical tick level so
         * background noise can't cross the onset threshold.
         *
         * Onset = 3% above effective_noise : catches true silence->signal.
         *         Because effective_noise is the 75th-percentile of recent
         *         silence samples (not the minimum), this sits ABOVE
         *         typical ambient noise instead of inside it. So 3% is
         *         enough headroom to catch real ticks reliably without
         *         tripping on background rumble.
         * Min-peak = 20% of dynamic range   : rejects noise bumps */
        double eff_noise, ref_peak, span, onset_thr, min_peak;
        compute_thresholds(d, &eff_noise, &ref_peak, &span,
                           &onset_thr, &min_peak);

        /* Silence gate is now wall-clock based: time since the last burst
         * ended, not continuous-below-release count. The purpose of the
         * gate is "enough time has passed for this to be a new beat",
         * which is independent of whether the envelope has continuously
         * stayed below a threshold. On watches with long ring-down tails
         * (NH35) the envelope can hover near release_thr for many ms
         * after the impact, which would reset a count-based gate and
         * block the next legitimate tick.
         *
         * We increment silence_samples every sample we're NOT in a burst,
         * unconditionally -- it represents "samples since last burst
         * ended", i.e. time elapsed. */
        if (!d->in_burst) {
            d->silence_samples++;
        }
        /* V4.5: count wall-clock samples since last A onset, regardless
         * of in-burst state. Used to gate sub-impulses that arrive
         * inside a multi-impulse beat cluster. Saturate at a large
         * value to avoid overflow on long silent stretches. */
        if (d->samples_since_last_a < (1ULL << 60)) {
            d->samples_since_last_a++;
        }

        if (!d->in_burst) {
            /* Silence -> burst transition.
             * Two gates that must BOTH pass:
             *   1) silence_samples >= min_silence_samples
             *      (time since previous burst ended)
             *   2) samples_since_last_a >= min_a_interval_samples
             *      (time since previous A onset; V4.5 sub-impulse gate)
             *
             * Pre-sync min_a_interval_samples is 0 (gate disabled, so
             * BPH detection sees all candidate events). Once sync is
             * locked, the library sets this to ~70% of beat_period to
             * suppress sub-impulses inside multi-impulse beat clusters. */
            uint64_t min_silence_samples = d->min_silence_samples;
            if (e > onset_thr && d->prev_sample <= onset_thr &&
                d->silence_samples >= min_silence_samples &&
                d->samples_since_last_a >= d->min_a_interval_samples)
            {
                d->in_burst = 1;
                d->silence_samples = 0;
                /* NOTE: samples_since_last_a is NOT reset here. The
                 * burst may still fail the min-peak test at burst-end,
                 * in which case no A is emitted and the counter must
                 * not be reset. Reset happens after successful A emit. */

                /* Sub-sample crossing time: linear interpolation between
                 * prev_sample (below threshold) and current sample (above). */
                double frac = 0.0;
                double denom = e - d->prev_sample;
                if (denom > 1e-20) {
                    frac = (onset_thr - d->prev_sample) / denom;
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                }
                /* Crossing is at time (abs_idx - 1 + frac). Encode as
                 * integer sample index + offset in [-0.5, +0.5]. */
                uint64_t idx = abs_idx - 1;
                double sub = frac;
                if (sub > 0.5) { idx += 1; sub -= 1.0; }
                d->burst_start_idx    = idx;
                d->burst_start_offset = sub;
                d->burst_start_time   = ((double)idx + sub) / d->fs;

                /* Reset burst max tracking (height check / fallback for C) */
                d->burst_max          = e;
                d->burst_max_idx      = abs_idx;
                d->burst_max_y_minus1 = d->prev_sample;
                d->have_peak_plus1    = 0;

                /* V5.2: reset C-peak tracker. C-peak only updates after
                 * c_search_skip_samples have elapsed since burst start. */
                d->c_have_peak        = 0;
                d->burst_c_max        = 0.0;
                d->burst_c_idx        = 0;
                d->c_have_peak_plus1  = 0;

                /* DON'T emit A yet -- defer until burst ends and we know
                 * the peak height. This lets us discard noise bumps that
                 * cross the low onset threshold but never reach a real
                 * peak. A is emitted at burst-end time using the saved
                 * burst_start_* fields. */
            }
        } else {
            /* Inside a burst. Track running max for the C event.
             *
             * Burst-end detection is WALL-CLOCK based: enough time since
             * the peak was observed. Wall-clock rather than "continuous
             * below release" because long ring-down tails oscillate
             * around release threshold without ever continuously staying
             * below for the required duration -- that would trap us
             * inside a burst indefinitely and miss all subsequent beats.
             * Once the peak has happened and enough time has passed, the
             * tick is effectively over. If a higher sample appears, the
             * peak is updated and the timer resets. */
            if (e > d->burst_max) {
                d->burst_max          = e;
                d->burst_max_idx      = abs_idx;
                d->burst_max_y_minus1 = d->prev_sample;
                d->have_peak_plus1    = 0;
            } else if (!d->have_peak_plus1 && abs_idx == d->burst_max_idx + 1) {
                d->burst_max_y_plus1  = e;
                d->have_peak_plus1    = 1;
            }

            /* V5.2: independently track C-peak only past the skip
             * window (counted from burst start). burst_max may have
             * latched on a sub-impulse near burst start; the C-peak
             * tracker ignores those and finds the true C cluster
             * peak. */
            uint64_t samples_since_burst = abs_idx - d->burst_start_idx;
            if (samples_since_burst >= d->c_search_skip_samples) {
                if (!d->c_have_peak || e > d->burst_c_max) {
                    d->burst_c_max        = e;
                    d->burst_c_idx        = abs_idx;
                    d->burst_c_y_minus1   = d->prev_sample;
                    d->c_have_peak        = 1;
                    d->c_have_peak_plus1  = 0;
                } else if (!d->c_have_peak_plus1 && abs_idx == d->burst_c_idx + 1) {
                    d->burst_c_y_plus1    = e;
                    d->c_have_peak_plus1  = 1;
                }
            }

            uint64_t samples_since_peak = abs_idx - d->burst_max_idx;

            if (samples_since_peak >= d->burst_end_samples) {
                /* Minimum tick height: fraction of the dynamic range. A
                 * real tick's peak is typically at or near reference_peak,
                 * so 20% (default) is well below what we'd expect but well
                 * above any noise-bump that crossed the onset threshold.
                 * Adjustable via tg_detector_set_min_peak_fraction(). */
                double min_peak = effective_noise(d) + d->min_peak_fraction * span;
                int is_real_tick = (d->burst_max >= min_peak);

                if (is_real_tick) {
                    /* Emit A first (at burst start onset time) */
                    if (produced + *out_count < max_events) {
                        tg_raw_event_t *ev = &out_events[*out_count + produced];
                        ev->sample_index      = d->burst_start_idx;
                        ev->sub_sample_offset = d->burst_start_offset;
                        ev->time_seconds      = d->burst_start_time;
                        ev->peak_value        = (float)d->burst_max;
                        ev->is_onset          = 1;
                        /* V5.4: A events don't carry onset metadata
                         * (the A's primary timing already IS the onset). */
                        ev->onset_sample_index      = 0;
                        ev->onset_sub_sample_offset = 0.0;
                        ev->onset_time_seconds      = 0.0;
                        ev->onset_valid             = 0;
                        produced++;
                    }
                    /* V4.5: now that an A has actually been emitted,
                     * reset the A-to-A interval counter. Failed bursts
                     * (above) do NOT reset it -- they're noise that
                     * passed the onset gate but failed min-peak.
                     *
                     * BUG-FIX: the counter must measure A-to-A, not
                     * burst-end-to-next-A. We're at burst-end now (which
                     * is ~25 ms after the C peak, ~30 ms after the A
                     * onset for typical 28800 BPH). If we reset to 0
                     * here, the next A is gated by 0.7*period from
                     * THIS moment, which effectively requires
                     * (0.7 * period + ~30 ms) between A onsets. For
                     * watches with high beat error (alternating long
                     * and short A-to-A intervals), the short ones can
                     * be just barely too short -- the small first
                     * impulse gets missed and A is mis-placed at the
                     * tail or on the C onset.
                     *
                     * Correct: reset to (current - burst_start) so
                     * the counter reflects time since A onset. */
                    uint64_t burst_dur_samples = abs_idx - d->burst_start_idx;
                    d->samples_since_last_a = burst_dur_samples;
                    /* V5.2: pick C-peak (post-skip) if available; else
                     * fall back to burst_max for backward compat (very
                     * short bursts, or skip > burst duration). */
                    double   c_max_v;
                    uint64_t c_max_idx;
                    double   c_y_m1, c_y_p1;
                    int      c_have_p1;
                    if (d->c_have_peak) {
                        c_max_v   = d->burst_c_max;
                        c_max_idx = d->burst_c_idx;
                        c_y_m1    = d->burst_c_y_minus1;
                        c_y_p1    = d->burst_c_y_plus1;
                        c_have_p1 = d->c_have_peak_plus1;
                    } else {
                        c_max_v   = d->burst_max;
                        c_max_idx = d->burst_max_idx;
                        c_y_m1    = d->burst_max_y_minus1;
                        c_y_p1    = d->burst_max_y_plus1;
                        c_have_p1 = d->have_peak_plus1;
                    }

                    /* Emit C (at burst peak, with parabolic interp) */
                    double sub_off = 0.0;
                    if (c_have_p1) {
                        sub_off = parabolic_offset(c_y_m1, c_max_v, c_y_p1);
                    }

                    /* V5.4: find C-cluster onset by walking back from the
                     * C peak in the recent envelope ring buffer. Always
                     * computed (cheap); the public-API layer chooses
                     * whether to promote it to primary timing or keep
                     * it as metadata. */
                    uint64_t onset_idx     = 0;
                    double   onset_sub_off = 0.0;
                    int      onset_valid   =
                        find_c_onset(d, c_max_idx, c_max_v,
                                     &onset_idx, &onset_sub_off);

                    if (produced + *out_count < max_events) {
                        tg_raw_event_t *ev = &out_events[*out_count + produced];
                        ev->sample_index      = c_max_idx;
                        ev->sub_sample_offset = sub_off;
                        ev->time_seconds      = ((double)c_max_idx + sub_off) / d->fs;
                        ev->peak_value        = (float)c_max_v;
                        ev->is_onset          = 0;
                        /* V5.4: onset metadata */
                        ev->onset_sample_index      = onset_idx;
                        ev->onset_sub_sample_offset = onset_sub_off;
                        ev->onset_time_seconds      =
                            onset_valid
                                ? ((double)onset_idx + onset_sub_off) / d->fs
                                : 0.0;
                        ev->onset_valid             = onset_valid;
                        produced++;
                    }
                    d->last_event_sample   = c_max_idx;
                    /* Credit the wall-clock time since peak toward the
                     * next silence gate. Use samples_since_peak (from
                     * burst_max_idx) -- this preserves V5.1 behavior
                     * for the silence-gate sizing, even when C is
                     * placed at a different (later) sample via the
                     * V5.2 skip mechanism. */
                    d->silence_samples     = samples_since_peak;
                    /* Record this real peak in the height history. Use
                     * burst_max (the loudest sample in the burst) so
                     * peak history reflects burst loudness independent
                     * of where C was placed. abs_idx for V5.6 regime
                     * detector is the current sample index. */
                    push_peak(d, d->burst_max, abs_idx);
                } else {
                    /* Noise bump rejected - don't penalize next real tick
                     * by zeroing silence counter. Pretend the whole episode
                     * was quiet so the silence-gate is open for next onset.
                     * Do NOT add to peak history -- history should only
                     * contain real ticks. */
                    d->silence_samples = 1000000;
                }
                d->in_burst          = 0;
            }
        }

        d->prev_sample = e;
    }

    d->total_samples += n;
    *out_count += produced;
    return produced;
}

/* V5.6: atomic read-and-clear of the regime reset flag. Called by
 * the library (Timegrapher.cpp) after each tg_detector_process to
 * see if the detector observed a regime change during that batch.
 * Returns 1 once per detected change; subsequent calls return 0
 * until the next change. */
int tg_detector_consume_regime_reset(tg_detector_t *d) {
    if (!d || !d->regime_reset_pending) return 0;
    d->regime_reset_pending = 0;
    return 1;
}
