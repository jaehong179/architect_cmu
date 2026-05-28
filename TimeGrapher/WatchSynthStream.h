#ifndef WATCH_SYNTH_STREAM_H
#define WATCH_SYNTH_STREAM_H

/*
    watch_synth_stream.h

    Continuous mono float-PCM synthetic mechanical-watch stream generator.

    -------------------------------------------------------------------------
    PURPOSE
    -------------------------------------------------------------------------
    This module synthesizes a continuous stream of mechanical-watch-like sound.
    It is intended for detector and timegrapher display testing where known
    ground-truth timing is valuable.

    The generator is stateful. The caller repeatedly provides a destination
    float buffer and the module fills that buffer with the next contiguous block
    of samples. There is no internal output file dependency and no assumption
    that the caller's block size is constant.

        WatchSynthStreamFillResult r = watch_synth_stream_fill_f32(
            &stream,
            out_pcm,       // caller-owned float buffer
            out_count,     // number of samples requested
            events,        // optional ground-truth event side channel
            event_capacity);

    -------------------------------------------------------------------------
    IMPORTANT UNITS AND NAMING
    -------------------------------------------------------------------------
    pcm_peak_amplitude
        Unit: normalized float PCM full-scale fraction.
        Range: 0.0..1.0.
        Example: 0.70 means the generated digital audio impacts are roughly
        +/-0.70 full scale before noise/variation/clipping.

        This is digital loudness only. It is NOT the mechanical watch amplitude.

    watch_amplitude_degrees
        Unit: degrees.
        Meaning: simulated balance amplitude as a timegrapher would report.
        Typical examples: 180, 220, 270, 300 degrees.

    lift_angle_degrees
        Unit: degrees.
        Meaning: watch lift angle used with watch_amplitude_degrees to derive
        the A-to-C time. Typical values are often around 44..60 degrees.

    beat_error_ms
        Unit: milliseconds, double precision.
        Sign convention:

            beat_error_ms = 0.5 * (interval(Tick -> Tock) - interval(Tock -> Tick))

        Therefore +0.210 means Tick->Tock is 0.420 ms longer than Tock->Tick, and a timegrapher should display about 0.210 ms.

    timing_jitter_us and bph_wander_*_us
        Unit: microseconds.
        These are intentionally kept in microseconds because they are tiny
        perturbations around the nominal beat interval.

        Realistic defaults are deliberately small so rate_error_s_per_day remains
        visible and accurate in short tests. Large wander values intentionally
        make the displayed rate drift above or below the configured rate.

    -------------------------------------------------------------------------
    A-TO-C TIME / WATCH AMPLITUDE MODEL
    -------------------------------------------------------------------------
    If use_watch_amplitude_for_a_to_c is enabled, A-to-C timing is derived from
    BPH, lift angle, and mechanical watch amplitude:

        beat_interval_s = 3600 / BPH

        A_to_C_s =
            (2 * beat_interval_s / pi) *
            asin(lift_angle_degrees / (2 * watch_amplitude_degrees))

    Why this shape:
        A watch balance is approximated as sinusoidal around center.
        The lift angle is swept around the zero-crossing / impulse zone.
        If the balance amplitude is A and lift angle is L, the time spent
        crossing the lift angle is proportional to asin(L / (2A)), not simply
        L / A over the whole beat interval.

    Larger watch amplitude produces a shorter A->C time; smaller watch
    amplitude produces a longer A->C time.

    This is still a simplified synthetic timing model, but it is much closer
    to the usual timegrapher geometry than the older linear formula. The older
    formula was too large by roughly pi for typical amplitudes.

    -------------------------------------------------------------------------
    CLEAN VS REALISTIC DEFAULTS
    -------------------------------------------------------------------------
    watch_synth_stream_clean_config()
        Produces stable, repeatable packets with minimal variation. Useful for
        unit tests and formula verification.

    watch_synth_stream_realistic_config()
        Enables packet-shape variation, amplitude drift, contact/case
        resonance, band-limited noise, BPH wander, and Tick/Tock spectral
        differences. Useful for stress testing real detectors.

    watch_synth_stream_default_config()
        Alias for the realistic configuration.

    -------------------------------------------------------------------------
    STREAMING CONTRACT
    -------------------------------------------------------------------------
    - The stream starts at absolute sample index 0 after init/reset.
    - Each fill call continues exactly where the previous fill call stopped.
    - Any caller block size is valid, including odd sizes such as 257 samples.
    - Packets may begin in one block and continue into later blocks.
    - Resonator and noise filter states persist across blocks.
    - Returned events are optional. Passing events=NULL disables the side channel
      but does not change the generated audio.

    Build example:
      gcc -O2 -std=c99 -Wall -Wextra -pedantic \
          watch_synth_stream.c example_stream_to_float.c -lm -o example_stream
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WATCH_SYNTH_MAX_ACTIVE_PACKETS 16
#define WATCH_SYNTH_MAX_LOBES 12

typedef enum WatchSynthEventKind {
    WATCH_SYNTH_EVENT_TICK = 0,
    WATCH_SYNTH_EVENT_TOCK = 1
} WatchSynthEventKind;

typedef struct WatchSynthStreamConfig {
    uint32_t sample_rate_hz;           /* Hz. Supported: 44100..384000. */
    double bph;                        /* Beats per hour. Supported: 3600..43200. */
    double rate_error_s_per_day;       /* s/day. Positive = fast = shorter intervals. */
    double beat_error_ms;              /* ms. Timegrapher-style displayed beat error. Example: 0.210 ms. */
    double timing_jitter_us;           /* us. Uniform random jitter added to intervals, +/- value. */
    double start_time_s;               /* seconds. Time of first generated beat after reset. */
    uint64_t seed;                     /* deterministic random seed. */

    double pcm_peak_amplitude;         /* normalized float PCM target level, 0..1. */
    double noise_peak_amplitude;       /* normalized float PCM noise level, 0..1. */

    double watch_amplitude_degrees;    /* degrees. Mechanical watch amplitude, e.g. 180..320. */
    double lift_angle_degrees;         /* degrees. Watch lift angle, e.g. 44..60. */
    int use_watch_amplitude_for_a_to_c;/* 1 = derive A->C from watch amplitude/lift angle. */
    double manual_a_to_c_time_s;       /* seconds. Used if use_watch_amplitude_for_a_to_c is 0. */
    double min_a_to_c_time_s;          /* seconds. Safety clamp for A->C. */
    double max_a_to_c_time_s;          /* seconds. Safety clamp for A->C. */

    int enable_realistic_packet;       /* 1 = multi-impact A/B/C packet; 0 = simple packet. */
    int enable_packet_shape_variation; /* 1 = vary lobe delay/frequency/decay/level per beat. */
    int enable_amplitude_drift;        /* 1 = slow PCM packet gain drift. */
    int enable_sensor_resonance;       /* 1 = contact/case resonator model. */
    int enable_bandlimited_noise;      /* 1 = band-limit synthetic mechanical noise. */
    int enable_bph_wander;             /* 1 = tiny random-walk interval wander; keep small for rate-accurate tests. */
    int enable_tick_tock_spectral_diff;/* 1 = Tick and Tock differ spectrally. */

    /*
        C-peak control. The exact C anchor is a narrow Gaussian peak placed at
        the computed A->C time. This keeps timegrapher amplitude readings tied
        to watch_amplitude_degrees even at high sample rates where later ringing
        can otherwise become the largest sample in the C region.
    */
    int enable_c_peak_lock;            /* 1 = make intended C peak dominate later C ringing. */
    double post_c_lobe_scale;          /* unitless. Scale later C/ring lobes when lock is on. */
    double c_peak_anchor_gain;       /* unitless. Dominant exact C anchor gain. */
    double c_peak_anchor_width_s;    /* seconds. Gaussian width for exact C anchor, e.g. 20 us. */

    double packet_tail_after_c_s;      /* seconds. Ringing duration after computed C time. */
    double packet_gain_variation;      /* fraction. Per-packet random PCM gain variation. */
    double shape_delay_jitter_us;      /* us. Random lobe delay perturbation. */
    double shape_frequency_jitter;     /* fraction. Random lobe frequency variation. */
    double shape_decay_jitter;         /* fraction. Random lobe decay variation. */

    double amplitude_drift_depth;      /* fraction. Example 0.08 = +/-8%. */
    double amplitude_drift_period_s;   /* seconds. */
    double bph_wander_depth_us;        /* us. Random-walk clamp. Large values intentionally disturb displayed rate. */
    double bph_wander_step_us;         /* us. Random-walk step per beat. */

    double sensor_resonance1_hz;       /* Hz. */
    double sensor_resonance1_q;        /* unitless Q. */
    double sensor_resonance1_gain;     /* mix gain. */
    double sensor_resonance2_hz;       /* Hz. */
    double sensor_resonance2_q;        /* unitless Q. */
    double sensor_resonance2_gain;     /* mix gain. */

    double noise_low_hz;               /* Hz. Noise high-pass corner. */
    double noise_high_hz;              /* Hz. Noise low-pass corner. */
} WatchSynthStreamConfig;

typedef struct WatchSynthStreamEvent {
    uint64_t beat_index;              /* 0-based event number: Tick0, Tock1, Tick2... */
    WatchSynthEventKind kind;         /* Tick for even beat_index, Tock for odd beat_index. */
    double time_s;                    /* seconds. Ground-truth packet onset time. */
    uint64_t sample_index;            /* absolute sample index corresponding to time_s. */

    double interval_from_previous_us; /* us. 0 for the first event after reset. */
    double applied_interval_offset_us;/* us. +/- beat_error_ms converted to us; raw interval difference is 2x. */
    double timing_jitter_us;          /* us. Actual random jitter contribution for this interval. */
    double bph_wander_us;             /* us. Smooth random-walk contribution for this interval. */

    double packet_gain;               /* normalized PCM gain used for this packet. */
    double a_to_c_time_s;             /* seconds. Ground-truth A/onset to C-like lobe time. */
    double watch_amplitude_degrees;   /* degrees. Copied from config for traceability. */
    double lift_angle_degrees;        /* degrees. Copied from config for traceability. */
} WatchSynthStreamEvent;

typedef struct WatchSynthStreamFillResult {
    uint64_t first_sample_index;      /* absolute index of output sample 0 in this block. */
    uint64_t next_sample_index;       /* absolute index that will be generated next. */
    size_t samples_written;           /* normally equals requested out_count. */
    size_t events_written;            /* number of events copied to caller's event buffer. */
    size_t events_dropped;            /* events not returned because event buffer was full. */
} WatchSynthStreamFillResult;

typedef struct WatchSynthLobe {
    double delay_s;
    double rel_amp;
    double freq_hz;
    double tau_s;
} WatchSynthLobe;

typedef struct WatchSynthActivePacket {
    int active;
    uint64_t start_sample_index;
    uint64_t end_sample_index;
    WatchSynthEventKind kind;
    double polarity;
    double packet_gain;
    int c_anchor_enabled;
    double c_anchor_delay_s;
    double c_anchor_gain;
    double c_anchor_width_s;
    size_t lobe_count;
    WatchSynthLobe lobes[WATCH_SYNTH_MAX_LOBES];
} WatchSynthActivePacket;

typedef struct WatchSynthStream {
    WatchSynthStreamConfig cfg;
    uint64_t absolute_sample_index;
    uint64_t next_event_sample_index;
    uint64_t beat_index;
    double next_event_time_s;
    double last_event_time_s;
    double adjusted_interval_s;
    double current_bph_wander_us;
    double next_interval_offset_us;
    double next_interval_jitter_us;
    uint64_t rng_state;
    WatchSynthActivePacket active_packets[WATCH_SYNTH_MAX_ACTIVE_PACKETS];
    double resonator1_s1, resonator1_s2;
    double resonator2_s1, resonator2_s2;
    double noise_lp_state, noise_hp_low_state;
} WatchSynthStream;

/*
    Fill cfg with a low-variation test configuration.
    This is best for verifying timing equations because jitter, wander, drift,
    resonance, and spectral Tick/Tock differences are disabled.
*/
void watch_synth_stream_clean_config(WatchSynthStreamConfig *cfg);

/*
    Fill cfg with a more watch-like configuration.
    This enables packet-shape variation, amplitude drift, contact/case
    resonance, band-limited noise, small BPH wander, and Tick/Tock spectral
    differences.
*/
void watch_synth_stream_realistic_config(WatchSynthStreamConfig *cfg);

/* Default is currently the realistic configuration. */
void watch_synth_stream_default_config(WatchSynthStreamConfig *cfg);

bool watch_synth_stream_is_supported_rate(uint32_t sample_rate_hz);
bool watch_synth_stream_validate_config(const WatchSynthStreamConfig *cfg, char *err, size_t err_size);

/*
    Compute the A->C time in seconds from the config.
    If use_watch_amplitude_for_a_to_c is 1, this uses the amplitude/lift-angle
    sinusoidal amplitude/lift-angle formula. Otherwise it returns manual_a_to_c_time_s after clamping.
*/
double watch_synth_stream_compute_a_to_c_time_s(const WatchSynthStreamConfig *cfg);

bool watch_synth_stream_init(WatchSynthStream *s, const WatchSynthStreamConfig *cfg, char *err, size_t err_size);
void watch_synth_stream_reset(WatchSynthStream *s);
void watch_synth_stream_destroy(WatchSynthStream *s);

/*
    Fill the caller-provided mono float PCM buffer.

    out_pcm:
        Caller-owned output buffer. Receives out_count normalized float PCM
        samples in the range [-1.0, +1.0].

    out_count:
        Number of samples requested. Any value is valid.

    events / event_capacity:
        Optional event side-channel. Pass events=NULL and event_capacity=0 when
        only audio is needed. If too many events occur inside the block,
        events_dropped is incremented and audio generation continues normally.
*/
WatchSynthStreamFillResult watch_synth_stream_fill_f32(
    WatchSynthStream *s,
    float *out_pcm,
    size_t out_count,
    WatchSynthStreamEvent *events,
    size_t event_capacity);

uint64_t watch_synth_stream_current_sample_index(const WatchSynthStream *s);
double watch_synth_stream_current_time_s(const WatchSynthStream *s);
const char *watch_synth_stream_event_kind_name(WatchSynthEventKind kind);

#ifdef __cplusplus
}
#endif

#endif
