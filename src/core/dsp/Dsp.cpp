/* Dsp.cpp -- Front-end DSP primitives feeding the burst detector.
 *
 * Two single-pole IIR filters live here, both designed for streaming
 * use (tiny per-sample state, no buffering, no look-ahead):
 *
 *   tg_hpf : single-pole high-pass / DC blocker. Removes microphone
 *            wind, table rumble, and AC mains hum that would otherwise
 *            bias the envelope follower below. Default cutoff 200 Hz
 *            sits above most low-frequency noise and well below the
 *            spectral content of the watch ticks themselves (which
 *            peak in the 1-5 kHz range).
 *
 *            Implementation: y[n] = a * (y[n-1] + x[n] - x[n-1])
 *            where a = exp(-2*pi*fc/fs). This is the canonical
 *            DC-blocker form; pole at a (just inside unit circle on
 *            the real axis), zero at +1.
 *
 *   tg_envelope : full-wave rectifier followed by a single-pole
 *                 low-pass smoother. The output approximates the
 *                 instantaneous envelope of the signal -- positive,
 *                 slow-moving, with sharp rises on impulse onsets.
 *
 *                 Default smoothing time constant 0.15 ms. This is
 *                 short enough that A and C clusters (which are
 *                 milliseconds apart even on the fastest watches) stay
 *                 well-separated in the envelope, but long enough to
 *                 collapse the high-frequency carrier inside each
 *                 impulse into a single hump for the detector to
 *                 measure.
 *
 *                 Implementation: env[n] = env[n-1] + alpha * (|x[n]| - env[n-1])
 *                 where alpha = 1 - exp(-1/(tau*fs)).
 *
 * Neither block is sample-rate-dependent in its API; everything is
 * specified in seconds / Hz and computed against the configured fs at
 * init time.
 *
 * V5.5: file renamed from dsp.c to Dsp.cpp; compiles under C++17.
 * No algorithmic change.
 */
#include "Dsp.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------- single-pole highpass (DC blocker) ---------- */

void tg_hpf_init(tg_hpf_t *h, double fs, double f_c) {
    if (f_c < 1.0)           f_c = 1.0;
    if (f_c > 0.25 * fs)     f_c = 0.25 * fs;
    h->a = exp(-2.0 * M_PI * f_c / fs);
    tg_hpf_reset(h);
}

void tg_hpf_reset(tg_hpf_t *h) {
    h->x_prev = 0.0;
    h->y_prev = 0.0;
}

void tg_hpf_process(tg_hpf_t *h, const float *in, float *out, size_t n) {
    double a  = h->a;
    double xp = h->x_prev;
    double yp = h->y_prev;
    for (size_t i = 0; i < n; ++i) {
        double x = (double)in[i];
        double y = x - xp + a * yp;
        out[i]   = (float)y;
        xp = x;
        yp = y;
    }
    h->x_prev = xp;
    h->y_prev = yp;
}

/* ---------- envelope detector ---------- */

void tg_envelope_init(tg_envelope_t *e, double fs, double smoothing_ms) {
    if (smoothing_ms <= 0.0) smoothing_ms = 0.15;
    double tau_samples = (smoothing_ms * 1e-3) * fs;
    if (tau_samples < 1.0) tau_samples = 1.0;
    e->alpha = 1.0 - exp(-1.0 / tau_samples);
    tg_envelope_reset(e);
}

void tg_envelope_reset(tg_envelope_t *e) {
    e->state = 0.0;
}

void tg_envelope_process(tg_envelope_t *e, const float *in, float *out, size_t n) {
    double s = e->state;
    double a = e->alpha;
    for (size_t i = 0; i < n; ++i) {
        double x = (double)in[i];
        if (x < 0.0) x = -x;
        s += a * (x - s);
        out[i] = (float)s;
    }
    e->state = s;
}
