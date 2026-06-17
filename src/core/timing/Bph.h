/* Bph.h - BPH lists, phase-score auto-detection, and sync state.
 *
 * Three concerns covered by this header:
 *
 *   1. Candidate BPH lists. TG_AUTO_BPH_LIST is the small set used by
 *      auto-detection (the BPHs we'll actually try to lock onto);
 *      TG_MANUAL_BPH_LIST is the wider set the host UI shows for
 *      user-driven manual selection (covers many antique rates).
 *
 *   2. Phase-score period detection. tg_phase_score computes the
 *      magnitude of the Rayleigh circular mean of (event_time mod T)
 *      for a candidate period T, returning a 0..1 measure of phase
 *      alignment. tg_bph_pick_by_phase sweeps the candidate list and
 *      picks the best-scoring period, returning 0 if no candidate
 *      crosses the lock threshold.
 *
 *      V5.3 fix: tg_phase_score returns R1 (first harmonic) only.
 *      Earlier versions returned R1+R2 which caused half-period
 *      mis-detection on signals where pre-sync events came in pairs
 *      (the second harmonic doubled the score for half-period
 *      candidates). See Bph.cpp and docs/design.md sec 4 for the
 *      full discussion.
 *
 *   3. tg_sync_t -- a small PLL-ish tracker that, once a BPH is
 *      picked, predicts when each next A and C should arrive based
 *      on the locked beat period and within-beat A->C offset. Each
 *      incoming event is matched against the prediction window;
 *      misses are counted, and after max_misses consecutive misses
 *      sync is declared lost. Both the period and the A->C offset
 *      adapt slowly (PLL-style) to track real drift over the life
 *      of the recording.
 *
 * V5.5: file renamed from bph.h to Bph.h and used by Bph.cpp;
 * compiles under C++17. extern "C" in Timegrapher.h keeps the public
 * symbols ABI-stable for non-C++ callers. */
#ifndef TG_BPH_H
#define TG_BPH_H

#include <stddef.h>
#include <stdint.h>
#include "Detector.h"
#include "Timegrapher.h"

/* Auto-detect BPH list (common rates: 12000, 14400, 18000, 19800, 21600,
 * 25200, 28800, 36000, 43200). */
extern const int    TG_AUTO_BPH_LIST[];
extern const size_t TG_AUTO_BPH_COUNT;

/* Manual-mode BPH list (covers a wider range of antique rates). */
extern const int    TG_MANUAL_BPH_LIST[];
extern const size_t TG_MANUAL_BPH_COUNT;

/* Returns 1 if bph is in the manual list, 0 otherwise. */
int tg_is_valid_manual_bph(int bph);
/* Returns the closest BPH from list[] within tolerance_pct of candidate_bph,
 * or 0 if nothing matches. */
int tg_bph_match(double candidate_bph,
                 const int *list, size_t list_len,
                 double tolerance_pct);

/* Estimate beat period (seconds) from a sequence of event times.
 * Uses median of t[i+2] - t[i] which equals one full beat period when
 * events alternate A,C,A,C,...
 * Returns 0.0 if not enough events. */
double tg_estimate_beat_period(const double *event_times, size_t n);

/* Score how well event times fit a candidate beat period using the 1st and
 * 2nd-harmonic Rayleigh statistics on the phase histogram (events folded
 * mod T). Returns 0..2; > ~0.7 indicates a real fit. Robust to missed
 * events because each event contributes independently. */
double tg_phase_score(const double *event_times, size_t n, double period);

/* From a list of candidate BPH values, pick the one whose period maximizes
 * the phase score on the events. Returns 0 if best score below threshold or
 * not enough events. min_score recommended ~0.7. */
int tg_bph_pick_by_phase(const double *event_times, size_t n,
                         const int *list, size_t list_len,
                         double min_score,
                         double *out_score,
                         double *out_period);

/* Sync state tracker. Tracks expected next event time using the current beat
 * period and within-beat A-C offset. */
typedef struct {
    int      bph;             /* current locked BPH                         */
    double   beat_period;     /* seconds                                    */
    double   ac_offset;       /* mean A->C delta (seconds)                  */

    /* PLL-ish tracking */
    double   next_a_time;     /* predicted absolute time of next A          */
    int      synced;
    int      consecutive_misses;

    /* tolerance */
    double   tolerance_s;     /* +/- around predicted event                 */
    int      max_misses;

    /* PLL gains */
    double   period_gain;     /* default 0.01                               */
    double   ac_gain;         /* default 0.05                               */

    /* timestamp of the most recent matched event - used for time-based
     * sync loss (if current stream time advances far beyond this + miss
     * budget * beat_period, declare loss). */
    double   last_match_time;
} tg_sync_t;

void tg_sync_init  (tg_sync_t *s);
void tg_sync_reset (tg_sync_t *s);

/* Set the lock parameters once BPH is known. */
void tg_sync_lock  (tg_sync_t *s,
                    int bph,
                    double beat_period,
                    double ac_offset,
                    double first_a_time,
                    double tolerance_s,
                    int    max_misses,
                    double period_gain,
                    double ac_gain);

/* Feed a single event. Updates internal model.
 * Returns:
 *   1 if it matches the predicted A or C window (sync maintained)
 *   0 if it doesn't match (a miss)
 * If consecutive_misses >= max_misses, sets synced=0; caller checks. */
int tg_sync_update(tg_sync_t *s, double event_time);

/* Check if the current stream time (latest processed sample time) is far
 * enough past last_match_time to declare sync loss. Call this each
 * tg_process() iteration after feeding all batch events. Returns 1 if it
 * cleared synced to 0, else 0. */
int tg_sync_check_timeout(tg_sync_t *s, double stream_time);

#endif
