/* Dsp.h - small DSP primitives used by the timegrapher core.
 *
 * Two stages:
 *   (1) tg_hpf_t      - single-pole DC blocker (rejects rumble below f_c)
 *   (2) tg_envelope_t - full-wave rectifier + one-pole LPF for envelope
 *
 * The pipeline is: PCM -> hpf -> envelope -> detector.
 * We deliberately do NOT bandpass-filter the audio. A 4th-order bandpass
 * rings for ~3-5 ms and smears the distinct A and C events into a single
 * blob in the envelope. Watch ticks are broadband impulses; removing DC
 * is all the filtering we need.
 */
#ifndef TG_DSP_H
#define TG_DSP_H

#include <stddef.h>

/* Single-pole highpass / DC blocker.
 *   y[n] = x[n] - x[n-1] + a * y[n-1],   a = exp(-2*pi*f_c/fs)
 * Cutoff f_c is typically 100-300 Hz: enough to reject AC hum and rumble,
 * low enough that no tick energy is affected. */
typedef struct {
    double a;       /* pole coefficient                          */
    double x_prev;  /* last input sample                         */
    double y_prev;  /* last output sample                        */
} tg_hpf_t;

void tg_hpf_init   (tg_hpf_t *h, double fs, double f_c);
void tg_hpf_reset  (tg_hpf_t *h);
void tg_hpf_process(tg_hpf_t *h, const float *in, float *out, size_t n);

/* Envelope detector: full-wave rectify + one-pole LPF.
 *   y[n] = y[n-1] + alpha * (|x[n]| - y[n-1])
 * Smoothing time constant controls transient sharpness. 0.15 ms is short
 * enough to preserve A and C as distinct envelope peaks; longer values
 * blur them together. */
typedef struct {
    double alpha;   /* LPF coefficient                           */
    double state;   /* LPF state                                 */
} tg_envelope_t;

void tg_envelope_init   (tg_envelope_t *e, double fs, double smoothing_ms);
void tg_envelope_reset  (tg_envelope_t *e);
void tg_envelope_process(tg_envelope_t *e, const float *in, float *out, size_t n);

#endif
