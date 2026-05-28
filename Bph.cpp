/* Bph.cpp -- BPH (beats-per-hour) detection and sync tracking.
 *
 * This translation unit owns three concerns:
 *
 *   1. The candidate BPH lists exposed via the public API
 *      (TG_AUTO_BPH_LIST and TG_MANUAL_BPH_LIST) and helpers used by
 *      the host application to populate UI dropdowns.
 *
 *   2. The "phase score" routine (tg_phase_score) used during BPH
 *      auto-detection. For each candidate beat period T and the
 *      recent A-event history, we compute the magnitude of the
 *      Rayleigh circular mean of (event_time mod T) -- the strength
 *      of phase alignment to a hypothetical period-T grid. The
 *      candidate with the highest score above the lock threshold wins.
 *
 *      V5.3 fix: this returns R1 (first harmonic) only. Earlier
 *      versions returned R1 + R2 to gain robustness on noisy signals
 *      that emitted both A and C events. After the V5.0 split that
 *      restricted the BPH path to A-only events, R2 became actively
 *      harmful: for a perfectly periodic A-only stream, R2 doubles
 *      the score for half-period candidates (all events project to
 *      phase 0 in the doubled-frequency space). On 18000 BPH watches
 *      with intermittent secondary echoes, this caused mis-detection
 *      as 36000 BPH. See docs/design.md section 4 for the full
 *      derivation.
 *
 *   3. tg_bph_pick_by_phase: orchestrates the candidate sweep and
 *      applies the V5.3 median-A-to-A-interval guard, rejecting any
 *      candidate period less than 0.7 * median(A-to-A) as physically
 *      implausible. The median is a robust lower bound on the true
 *      beat period (spurious extra events can only make consecutive
 *      A-to-A gaps smaller, never larger). Even where R1 still has
 *      half-period aliasing modes for perfectly periodic events, the
 *      median guard breaks the tie in favor of the true period.
 *
 * The sync state machine itself (acquire / track / lose) lives in
 * Timegrapher.cpp -- this file just supplies the period-detection
 * primitive it calls per block.
 *
 * V5.5: file renamed from bph.c to Bph.cpp; compiles under C++17.
 * No algorithmic change. The public symbols (TG_AUTO_BPH_LIST,
 * TG_MANUAL_BPH_LIST, tg_phase_score, tg_bph_pick_by_phase) keep
 * their C-style ABI via extern "C" guards in the headers.
 */
#include "Bph.h"
#include "Timegrapher.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------- BPH lists (exposed via timegrapher.h) ---------- */

const int TG_AUTO_BPH_LIST[] = {
    12000, 14400, 18000, 19800, 21600, 25200, 28800, 36000, 43200
};
const size_t TG_AUTO_BPH_COUNT =
    sizeof(TG_AUTO_BPH_LIST) / sizeof(TG_AUTO_BPH_LIST[0]);

const int TG_MANUAL_BPH_LIST[] = {
     3600,  6000,  7200,  7380,  7440,  7800,  9000,  9100, 10800, 11880,
    12000, 12342, 12480, 12600, 13320, 13440, 13500, 14000, 14040, 14160,
    14200, 14280, 14400, 14520, 14580, 14760, 14850, 15000, 15360, 15600,
    16200, 16320, 16800, 17196, 17258, 17280, 17786, 17897, 18000, 18049,
    18514, 19332, 19440, 19800, 20160, 20222, 20944, 21000, 21031, 21306,
    21600, 25200, 28800, 32400, 36000, 43200
};
const size_t TG_MANUAL_BPH_COUNT =
    sizeof(TG_MANUAL_BPH_LIST) / sizeof(TG_MANUAL_BPH_LIST[0]);

int tg_is_valid_manual_bph(int bph) {
    for (size_t i = 0; i < TG_MANUAL_BPH_COUNT; ++i)
        if (TG_MANUAL_BPH_LIST[i] == bph) return 1;
    return 0;
}

/* ---------- BPH matching ---------- */

int tg_bph_match(double candidate_bph,
                 const int *list, size_t list_len,
                 double tolerance_pct)
{
    if (candidate_bph <= 0.0 || list_len == 0) return 0;

    int best = 0;
    double best_err = 1e30;
    for (size_t i = 0; i < list_len; ++i) {
        double err = fabs(candidate_bph - (double)list[i]) / (double)list[i];
        if (err < best_err) {
            best_err = err;
            best = list[i];
        }
    }
    if (best_err * 100.0 <= tolerance_pct) return best;
    return 0;
}

/* ---------- median helper ---------- */

static int dcmp(const void *a, const void *b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

double tg_estimate_beat_period(const double *event_times, size_t n) {
    if (n < 4) return 0.0;
    /* For events ACACAC..., t[i+2] - t[i] == beat period. */
    size_t m = n - 2;
    double *deltas = (double*)malloc(m * sizeof(double));
    if (!deltas) return 0.0;
    for (size_t i = 0; i < m; ++i) {
        deltas[i] = event_times[i + 2] - event_times[i];
    }
    qsort(deltas, m, sizeof(double), dcmp);
    double med = deltas[m / 2];
    free(deltas);
    return med;
}

/* Phase score: fold event times mod period and measure how concentrated
 * the resulting phases are using the standard Rayleigh statistic.
 *
 *   ph_i  = 2*pi * (t_i mod T) / T
 *   score = | mean(e^{i*ph}) |       (R1, in [0,1])
 *
 * For uniformly random event times R1 ≈ 1/sqrt(n) → small.
 *
 * Only A events are pushed to the BPH history (one per beat), so a
 * pure first-harmonic Rayleigh score correctly peaks at the true
 * beat period.
 *
 * Earlier versions of this function returned R1+R2 (sum of first and
 * second harmonics), motivated by an A+C event stream where two
 * peaks per period maximized the second harmonic. Once the BPH path
 * was changed to A-only events, R2 became actively harmful: it
 * doubles the score for HALF-period candidates because all events
 * project to phase 0 in the doubled-frequency space. That caused
 * 18000 BPH watches with intermittent secondary echoes to be
 * mis-detected as 36000 BPH. Reverted to R1-only in V5.3. */
double tg_phase_score(const double *event_times, size_t n, double period) {
    if (n < 6 || period <= 0.0) return 0.0;
    double s1 = 0.0, c1 = 0.0;
    double inv_T = 1.0 / period;
    for (size_t i = 0; i < n; ++i) {
        double ph = 2.0 * M_PI * (event_times[i] - floor(event_times[i] * inv_T) * period) * inv_T;
        c1 += cos(ph);  s1 += sin(ph);
    }
    double inv_n = 1.0 / (double)n;
    return sqrt(c1*c1 + s1*s1) * inv_n;
}

int tg_bph_pick_by_phase(const double *event_times, size_t n,
                         const int *list, size_t list_len,
                         double min_score,
                         double *out_score,
                         double *out_period)
{
    if (out_score)  *out_score  = 0.0;
    if (out_period) *out_period = 0.0;
    if (n < 6 || !list || list_len == 0) return 0;

    /* Compute median A-to-A interval. The true beat period is at
     * least this large (one A per beat means consecutive A's are
     * separated by ~beat_period; spurious extra events can only
     * make the median smaller, never larger).
     *
     * Test periods less than 0.7 * median are physically implausible
     * and rejected. This breaks the period/half-period aliasing in
     * the Rayleigh score (R1 alone can't distinguish T from T/2 for
     * perfectly periodic events; same for any T/k). */
    double aa[256];
    int    n_aa = 0;
    for (size_t i = 1; i < n && n_aa < 256; ++i) {
        double d = event_times[i] - event_times[i-1];
        if (d > 0.0) aa[n_aa++] = d;
    }
    /* simple insertion sort for small n_aa */
    for (int i = 1; i < n_aa; ++i) {
        double v = aa[i];
        int j = i - 1;
        while (j >= 0 && aa[j] > v) { aa[j+1] = aa[j]; j--; }
        aa[j+1] = v;
    }
    double median_aa = (n_aa > 0) ? aa[n_aa / 2] : 0.0;
    double min_period = 0.7 * median_aa;

    int best_bph = 0;
    double best_score = -1.0;
    double best_period = 0.0;
    for (size_t i = 0; i < list_len; ++i) {
        double T = 3600.0 / (double)list[i];
        if (T < min_period) continue;   /* implausibly short */
        double s = tg_phase_score(event_times, n, T);
        if (s > best_score) {
            best_score  = s;
            best_bph    = list[i];
            best_period = T;
        }
    }
    if (out_score)  *out_score  = best_score;
    if (out_period) *out_period = best_period;
    if (best_score >= min_score) return best_bph;
    return 0;
}

/* ---------- sync tracker ---------- */

void tg_sync_init(tg_sync_t *s) {
    memset(s, 0, sizeof(*s));
}

void tg_sync_reset(tg_sync_t *s) {
    s->synced             = 0;
    s->consecutive_misses = 0;
}

void tg_sync_lock(tg_sync_t *s,
                  int bph,
                  double beat_period,
                  double ac_offset,
                  double first_a_time,
                  double tolerance_s,
                  int    max_misses,
                  double period_gain,
                  double ac_gain)
{
    s->bph                 = bph;
    s->beat_period         = beat_period;
    s->ac_offset           = ac_offset;
    s->next_a_time         = first_a_time + beat_period;
    s->synced              = 1;
    s->consecutive_misses  = 0;
    s->tolerance_s         = tolerance_s;
    s->max_misses          = max_misses;
    s->period_gain         = period_gain;
    s->ac_gain             = ac_gain;
    s->last_match_time     = first_a_time;
}

int tg_sync_update(tg_sync_t *s, double event_time) {
    if (!s->synced) return 0;

    /* Phase-based matching: each event should fall near one of two phases
     * within the beat period - phase 0 (reference event) and phase |ac_offset|
     * (companion event). The A/C direction (which came first) doesn't matter
     * to the PLL - it just needs the 2-event-per-beat pattern to hold. */

    /* Compute phase in [0, T). Reference phase is the last matched A's
     * absolute time mod T (we use next_a_time - beat_period). */
    double ref = s->next_a_time - s->beat_period;    /* last A time */
    double phase = event_time - ref;
    /* Bring into principal range [-T/2, +T/2) for easier comparison. */
    double T = s->beat_period;
    while (phase >  0.5 * T) phase -= T;
    while (phase < -0.5 * T) phase += T;

    double g = s->ac_offset;   /* magnitude of within-beat gap */
    /* Expected phases: 0 (next A) or +/- g (C within this or next beat).
     * ac_offset may be signed; take it as given but accept both signs. */
    double err_a      = fabs(phase);
    double err_c_pos  = fabs(phase - g);
    double err_c_neg  = fabs(phase + g);
    double err_c      = (err_c_pos < err_c_neg) ? err_c_pos : err_c_neg;

    if (err_a <= s->tolerance_s) {
        /* Matched main phase - update reference */
        s->next_a_time = event_time + s->beat_period;
        s->beat_period += s->period_gain * phase;
        s->consecutive_misses = 0;
        s->last_match_time = event_time;
        return 1;
    }
    if (err_c <= s->tolerance_s) {
        /* Matched companion phase. Don't advance next_a_time, but refine g. */
        if (g > 0) {
            double nudge = (err_c_pos < err_c_neg) ? (phase - g) : -(phase + g);
            s->ac_offset += s->ac_gain * nudge;
        }
        s->consecutive_misses = 0;
        s->last_match_time = event_time;
        return 1;
    }

    /* Event didn't match either expected phase. If we're far past the
     * predicted next A, advance the window (handles bursts of missed beats). */
    while (event_time > s->next_a_time + 1.5 * s->beat_period) {
        s->next_a_time += s->beat_period;
    }

    s->consecutive_misses++;
    if (s->consecutive_misses >= s->max_misses) {
        s->synced = 0;
    }
    return 0;
}

int tg_sync_check_timeout(tg_sync_t *s, double stream_time) {
    if (!s->synced) return 0;
    /* Silence timeout: if the current stream position is more than a few
     * beat periods past the last matched event, declare sync lost.
     * Silence is a stronger signal than "events arrive but don't match"
     * (which gets the full max_misses budget), so we use a tighter
     * timeout of max(3, max_misses/2) beat periods. */
    int timeout_beats = s->max_misses / 2;
    if (timeout_beats < 3) timeout_beats = 3;
    double timeout = (double)timeout_beats * s->beat_period;
    if (stream_time - s->last_match_time > timeout) {
        s->synced = 0;
        return 1;
    }
    return 0;
}
