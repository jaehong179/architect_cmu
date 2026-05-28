#include "WatchSynthStream.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WS_MIN_BPH 3600.0
#define WS_MAX_BPH 43200.0
#define WS_MIN_SR 44100u
#define WS_MAX_SR 384000u

/*
    IMPLEMENTATION OVERVIEW
    -----------------------
    The stream maintains an absolute sample counter and a scheduled next event.
    During watch_synth_stream_fill_f32():

      1. For each requested output sample, check whether the next Tick/Tock
         event is due at or before the current absolute sample index.
      2. If due, create an active packet whose lobes may span many future
         samples and possibly many future fill calls.
      3. Mix all active packets at the current sample.
      4. Apply optional contact/case resonance. Resonator state is stored in the
         stream object, so ringing continues smoothly across buffer boundaries.
      5. Add optional mechanical bed noise. The noise filter states are also
         persistent across buffer boundaries.
      6. Clip to normalized float PCM range [-1.0, +1.0].

    The design deliberately keeps event scheduling separate from packet mixing.
    Event scheduling controls the ground-truth beat timing. Packet mixing only
    turns each scheduled event into acoustic-looking PCM samples.
*/

/* Clamp helper used for all defensive range limits. */
static double ws_clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool watch_synth_stream_is_supported_rate(uint32_t sr) { return sr >= WS_MIN_SR && sr <= WS_MAX_SR; }
const char *watch_synth_stream_event_kind_name(WatchSynthEventKind k) { return k == WATCH_SYNTH_EVENT_TICK ? "Tick" : "Tock"; }

/*
    SplitMix64 pseudo-random generator.
    It is deterministic, tiny, portable C, and adequate for repeatable synthetic
    jitter/noise. It is not intended for cryptography.
*/
static uint64_t ws_next_u64(uint64_t *state) {
    uint64_t z;
    if (*state == 0) *state = 0x9e3779b97f4a7c15ULL;
    z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
static double ws_rand01(uint64_t *state) { return (double)(ws_next_u64(state) >> 11) * (1.0 / 9007199254740992.0); }
static double ws_rand_signed(uint64_t *state) { return 2.0 * ws_rand01(state) - 1.0; }

/*
    Clean default configuration.

    Use this when you want exact, easy-to-debug timing:
      - no packet shape variation
      - no BPH wander
      - no amplitude drift
      - no resonance coloration
      - minimal noise

    The A->C time is still derived from watch amplitude and lift angle by
    default so amplitude-related detector tests remain meaningful.
*/
void watch_synth_stream_clean_config(WatchSynthStreamConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->sample_rate_hz = 48000u;
    cfg->bph = 19800.0;
    cfg->rate_error_s_per_day = 0.0;
    cfg->beat_error_ms = 0.0;
    cfg->timing_jitter_us = 0.0;
    cfg->start_time_s = 0.050;
    cfg->seed = 0x123456789abcdefULL;
    cfg->pcm_peak_amplitude = 0.70;
    cfg->noise_peak_amplitude = 0.0005;
    cfg->watch_amplitude_degrees = 270.0;
    cfg->lift_angle_degrees = 52.0;
    cfg->use_watch_amplitude_for_a_to_c = 1;
    cfg->manual_a_to_c_time_s = 0.0084;
    cfg->min_a_to_c_time_s = 0.0010;
    cfg->max_a_to_c_time_s = 0.2500;
    cfg->enable_realistic_packet = 0;
    cfg->enable_packet_shape_variation = 0;
    cfg->enable_amplitude_drift = 0;
    cfg->enable_sensor_resonance = 0;
    cfg->enable_bandlimited_noise = 0;
    cfg->enable_bph_wander = 0;
    cfg->enable_tick_tock_spectral_diff = 0;

    /* Keep the intended C peak dominant, including at high sample rates. */
    cfg->enable_c_peak_lock = 1;
    cfg->post_c_lobe_scale = 0.22;
    cfg->c_peak_anchor_gain = 3.00;
    cfg->c_peak_anchor_width_s = 0.000020;

    cfg->packet_tail_after_c_s = 0.0080;
    cfg->packet_gain_variation = 0.0;
    cfg->shape_delay_jitter_us = 0.0;
    cfg->shape_frequency_jitter = 0.0;
    cfg->shape_decay_jitter = 0.0;
    cfg->amplitude_drift_depth = 0.0;
    cfg->amplitude_drift_period_s = 10.0;
    cfg->bph_wander_depth_us = 0.0;
    cfg->bph_wander_step_us = 0.0;
    cfg->sensor_resonance1_hz = 3600.0;
    cfg->sensor_resonance1_q = 2.2;
    cfg->sensor_resonance1_gain = 0.0;
    cfg->sensor_resonance2_hz = 9200.0;
    cfg->sensor_resonance2_q = 4.5;
    cfg->sensor_resonance2_gain = 0.0;
    cfg->noise_low_hz = 700.0;
    cfg->noise_high_hz = 18000.0;
}

/*
    Realistic default configuration.

    This starts from the clean configuration and then enables controlled
    acoustic imperfections that make the signal less mathematically perfect:
      - small timing jitter and very gentle BPH random walk
      - per-packet gain and shape differences
      - slow amplitude drift
      - contact/case resonance
      - band-limited noise
      - different Tick/Tock spectral character

    Important: realistic defaults are rate-preserving. Earlier versions used a
    12 us BPH-wander clamp, which could bias short timegrapher rate readings.
    Increase bph_wander_depth_us and bph_wander_step_us manually only when you
    explicitly want short-term rate instability for stress testing.

    These are intentionally still deterministic for a given seed so test runs
    can be reproduced exactly.
*/
void watch_synth_stream_realistic_config(WatchSynthStreamConfig *cfg) {
    watch_synth_stream_clean_config(cfg);
    if (!cfg) return;
    cfg->timing_jitter_us = 1.0;
    cfg->noise_peak_amplitude = 0.0022;
    cfg->enable_realistic_packet = 1;
    cfg->enable_packet_shape_variation = 1;
    cfg->enable_amplitude_drift = 1;
    cfg->enable_sensor_resonance = 1;
    cfg->enable_bandlimited_noise = 1;
    cfg->enable_bph_wander = 1;
    cfg->enable_tick_tock_spectral_diff = 1;
    cfg->packet_tail_after_c_s = 0.0120;
    cfg->packet_gain_variation = 0.040;
    cfg->shape_delay_jitter_us = 35.0;
    cfg->shape_frequency_jitter = 0.045;
    cfg->shape_decay_jitter = 0.18;
    cfg->amplitude_drift_depth = 0.08;
    cfg->amplitude_drift_period_s = 11.0;
    /* Gentle default wander: keeps realistic mode close to configured rate. */
    cfg->bph_wander_depth_us = 0.75;
    cfg->bph_wander_step_us = 0.075;
    /* Modest normalized resonance: enough color without high-rate clipping. */
    cfg->sensor_resonance1_gain = 0.12;
    cfg->sensor_resonance2_gain = 0.06;
}
void watch_synth_stream_default_config(WatchSynthStreamConfig *cfg) { watch_synth_stream_realistic_config(cfg); }

/*
    Convert mechanical watch amplitude to synthetic A->C time.

    The event time is treated as the A-like onset. The strongest C-like lobe is
    placed a_to_c_time_s later. A detector can therefore compare its measured
    A and C timestamps against this known ground truth.

    Formula used when use_watch_amplitude_for_a_to_c is enabled:

        beat_interval_s = 3600 / BPH

        A_to_C_seconds =
            (2 * beat_interval_s / pi) *
            asin(lift_angle_degrees / (2 * watch_amplitude_degrees))

    Why this is different from the previous version:
      The previous version used:

          beat_interval_s * lift_angle_degrees / watch_amplitude_degrees

      That treats the balance as if it moved linearly through the entire beat
      interval. That made A->C too large. A balance is better approximated as
      sinusoidal; it moves fastest around center, so the lift-angle crossing
      takes a much smaller fraction of the beat.

    Important behavior:
      - Higher watch_amplitude_degrees -> shorter A->C time.
      - Lower watch_amplitude_degrees  -> longer A->C time.
      - min/max clamps prevent unrealistic or invalid packet geometry.
*/
double watch_synth_stream_compute_a_to_c_time_s(const WatchSynthStreamConfig *cfg) {
    double t;
    if (!cfg) return 0.0;
    if (cfg->use_watch_amplitude_for_a_to_c) {
        double beat_interval_s = 3600.0 / cfg->bph;
        double ratio;

        /*
            Sinusoidal balance approximation.

            Let:
              A = watch_amplitude_degrees
              L = lift_angle_degrees
              T = beat_interval_s, the time from Tick to Tock

            The balance angular position is approximated by:
              theta(t) = A * sin(pi * t / T)

            Around the zero crossing, the lift zone spans roughly -L/2 to +L/2.
            Solving for the time between those two crossings gives:

              A_to_C = (2T / pi) * asin(L / (2A))

            For small L/(2A), this is approximately:
              A_to_C ~= T * L / (pi * A)

            This correction is why the spacing is roughly 1/pi of the older
            linear formula and is much closer to real timegrapher behavior.
        */
        ratio = cfg->lift_angle_degrees / (2.0 * cfg->watch_amplitude_degrees);
        ratio = ws_clamp(ratio, 0.0, 0.999999);
        t = (2.0 * beat_interval_s / M_PI) * asin(ratio);
    } else {
        t = cfg->manual_a_to_c_time_s;
    }
    return ws_clamp(t, cfg->min_a_to_c_time_s, cfg->max_a_to_c_time_s);
}

/*
    Validate public configuration before a stream starts.

    The checks are intentionally conservative. They catch unit mistakes such as:
      - using 210 instead of 0.210 for beat_error_ms
      - giving PCM amplitude outside normalized range
      - asking for a beat error/jitter/wander combination that could produce a
        non-positive interval at high BPH
*/
bool watch_synth_stream_validate_config(const WatchSynthStreamConfig *cfg, char *err, size_t err_size) {
    double adjusted, beat_error_offset_s;
    if (!cfg) { snprintf(err, err_size, "cfg is NULL"); return false; }
    if (!watch_synth_stream_is_supported_rate(cfg->sample_rate_hz)) { snprintf(err, err_size, "sample_rate_hz must be %u..%u Hz", WS_MIN_SR, WS_MAX_SR); return false; }
    if (cfg->bph < WS_MIN_BPH || cfg->bph > WS_MAX_BPH) { snprintf(err, err_size, "bph must be %.0f..%.0f", WS_MIN_BPH, WS_MAX_BPH); return false; }
    if (cfg->pcm_peak_amplitude < 0.0 || cfg->pcm_peak_amplitude > 1.0) { snprintf(err, err_size, "pcm_peak_amplitude must be 0..1 normalized PCM"); return false; }
    if (cfg->noise_peak_amplitude < 0.0 || cfg->noise_peak_amplitude > 1.0) { snprintf(err, err_size, "noise_peak_amplitude must be 0..1 normalized PCM"); return false; }
    if (cfg->watch_amplitude_degrees < 90.0 || cfg->watch_amplitude_degrees > 450.0) { snprintf(err, err_size, "watch_amplitude_degrees must be 90..450 degrees"); return false; }
    if (cfg->lift_angle_degrees <= 0.0 || cfg->lift_angle_degrees > 90.0) { snprintf(err, err_size, "lift_angle_degrees must be >0 and <=90 degrees"); return false; }
    if (cfg->packet_tail_after_c_s <= 0.0 || cfg->packet_tail_after_c_s > 0.100) { snprintf(err, err_size, "packet_tail_after_c_s must be >0 and <=100 ms"); return false; }
    if (cfg->post_c_lobe_scale < 0.0 || cfg->post_c_lobe_scale > 2.0) { snprintf(err, err_size, "post_c_lobe_scale must be 0..2"); return false; }
    if (cfg->c_peak_anchor_gain < 0.0 || cfg->c_peak_anchor_gain > 10.0) { snprintf(err, err_size, "c_peak_anchor_gain must be 0..10"); return false; }
    if (cfg->c_peak_anchor_width_s <= 0.0 || cfg->c_peak_anchor_width_s > 0.0010) { snprintf(err, err_size, "c_peak_anchor_width_s must be >0 and <=1 ms"); return false; }
    if (cfg->min_a_to_c_time_s <= 0.0 || cfg->max_a_to_c_time_s <= cfg->min_a_to_c_time_s) { snprintf(err, err_size, "A-to-C clamp range is invalid"); return false; }
    if (cfg->start_time_s < 0.0) { snprintf(err, err_size, "start_time_s must be >=0"); return false; }
    adjusted = (3600.0 / cfg->bph) / (1.0 + cfg->rate_error_s_per_day / 86400.0);
    beat_error_offset_s = fabs(cfg->beat_error_ms) * 1.0e-3;
    if (adjusted <= beat_error_offset_s + cfg->timing_jitter_us * 1.0e-6 + cfg->bph_wander_depth_us * 1.0e-6) { snprintf(err, err_size, "beat error/jitter/wander too large for BPH"); return false; }
    return true;
}

/*
    Initialize a caller-owned stream object.

    No output buffer is retained by the stream. The stream only stores timing,
    active packet, resonator, noise, and RNG state needed to continue generation
    on the next fill call.
*/
bool watch_synth_stream_init(WatchSynthStream *s, const WatchSynthStreamConfig *cfg, char *err, size_t err_size) {
    if (!s) { snprintf(err, err_size, "stream is NULL"); return false; }
    if (!watch_synth_stream_validate_config(cfg, err, err_size)) return false;
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->rng_state = cfg->seed ? cfg->seed : 0x123456789abcdefULL;
    s->adjusted_interval_s = (3600.0 / cfg->bph) / (1.0 + cfg->rate_error_s_per_day / 86400.0);
    watch_synth_stream_reset(s);
    return true;
}

/*
    Reset to the beginning of the synthetic stream.

    Absolute sample index returns to 0 and the first Tick is re-scheduled at
    cfg.start_time_s. The RNG is also reset to cfg.seed, which makes the stream
    exactly repeatable after reset.
*/
void watch_synth_stream_reset(WatchSynthStream *s) {
    uint64_t seed;
    if (!s) return;
    seed = s->cfg.seed ? s->cfg.seed : 0x123456789abcdefULL;
    memset(s->active_packets, 0, sizeof(s->active_packets));
    s->absolute_sample_index = 0;
    s->beat_index = 0;
    s->next_event_time_s = s->cfg.start_time_s;
    s->next_event_sample_index = (uint64_t)llround(s->next_event_time_s * (double)s->cfg.sample_rate_hz);
    s->last_event_time_s = 0.0;
    s->current_bph_wander_us = 0.0;
    s->next_interval_offset_us = 0.0;
    s->next_interval_jitter_us = 0.0;
    s->rng_state = seed;
    s->resonator1_s1 = s->resonator1_s2 = 0.0;
    s->resonator2_s1 = s->resonator2_s2 = 0.0;
    s->noise_lp_state = s->noise_hp_low_state = 0.0;
}
void watch_synth_stream_destroy(WatchSynthStream *s) { if (s) memset(s, 0, sizeof(*s)); }
uint64_t watch_synth_stream_current_sample_index(const WatchSynthStream *s) { return s ? s->absolute_sample_index : 0; }
double watch_synth_stream_current_time_s(const WatchSynthStream *s) { return s ? (double)s->absolute_sample_index / (double)s->cfg.sample_rate_hz : 0.0; }

/*
    Allocate an active packet slot.

    A packet begins at a Tick/Tock event and then rings for some time. Since the
    packet can extend across block boundaries, it is kept in active_packets until
    the current absolute sample index passes its end_sample_index.
*/
static WatchSynthActivePacket *ws_alloc_packet(WatchSynthStream *s) {
    size_t i;
    for (i = 0; i < WATCH_SYNTH_MAX_ACTIVE_PACKETS; ++i) if (!s->active_packets[i].active) return &s->active_packets[i];
    return &s->active_packets[0];
}
static void ws_add_lobe(WatchSynthActivePacket *p, double delay_s, double amp, double freq, double tau) {
    WatchSynthLobe *l;
    if (p->lobe_count >= WATCH_SYNTH_MAX_LOBES) return;
    l = &p->lobes[p->lobe_count++];
    l->delay_s = delay_s; l->rel_amp = amp; l->freq_hz = freq; l->tau_s = tau;
}

static void ws_set_c_anchor(WatchSynthActivePacket *p, double delay_s, double gain, double width_s) {
    p->c_anchor_enabled = 1;
    p->c_anchor_delay_s = delay_s;
    p->c_anchor_gain = gain;
    p->c_anchor_width_s = width_s;
}
/*
    Add one damped sinusoidal lobe to a packet, with optional variation.

    A lobe is the building block for the synthetic acoustic packet:
      delay_s : time after A/onset when the lobe starts
      amp     : relative contribution before packet_gain scaling
      freq    : ringing frequency in Hz
      tau     : exponential decay time constant in seconds

    Realistic mode perturbs delay/frequency/decay/level per packet so every tick
    is not an exact copy of the previous one.
*/
static void ws_add_varied_lobe(WatchSynthStream *s, WatchSynthActivePacket *p, double packet_duration_s, double delay_s, double amp, double freq, double tau, double freq_scale) {
    const WatchSynthStreamConfig *cfg = &s->cfg;
    freq *= freq_scale;
    if (cfg->enable_packet_shape_variation) {
        delay_s += cfg->shape_delay_jitter_us * 1.0e-6 * ws_rand_signed(&s->rng_state);
        amp *= 1.0 + 0.16 * ws_rand_signed(&s->rng_state);
        freq *= 1.0 + cfg->shape_frequency_jitter * ws_rand_signed(&s->rng_state);
        tau *= 1.0 + cfg->shape_decay_jitter * ws_rand_signed(&s->rng_state);
    }
    delay_s = ws_clamp(delay_s, 0.0, packet_duration_s - 0.0001);
    amp = ws_clamp(amp, 0.0, 2.0);
    freq = ws_clamp(freq, 500.0, 0.45 * (double)cfg->sample_rate_hz);
    tau = ws_clamp(tau, 0.00015, 0.020);
    ws_add_lobe(p, delay_s, amp, freq, tau);
}

/*
    Schedule the next Tick/Tock event.

    The just-emitted event's kind determines which half interval follows:
      Tick -> Tock uses +beat_error_ms
      Tock -> Tick uses -beat_error_ms

    beat_error_ms is converted to seconds here. The code also adds optional
    independent timing jitter and optional slow BPH wander.
*/
static void ws_schedule_next_event(WatchSynthStream *s, WatchSynthEventKind kind) {
    const WatchSynthStreamConfig *cfg = &s->cfg;
    double beat_error_offset_s = cfg->beat_error_ms * 1.0e-3;
    double offset_s = (kind == WATCH_SYNTH_EVENT_TICK) ? +beat_error_offset_s : -beat_error_offset_s;
    double jitter_s = cfg->timing_jitter_us * 1.0e-6 * ws_rand_signed(&s->rng_state);
    if (cfg->enable_bph_wander) {
        s->current_bph_wander_us += cfg->bph_wander_step_us * ws_rand_signed(&s->rng_state);
        s->current_bph_wander_us = ws_clamp(s->current_bph_wander_us, -cfg->bph_wander_depth_us, cfg->bph_wander_depth_us);
    } else s->current_bph_wander_us = 0.0;
    s->next_interval_offset_us = offset_s * 1.0e6;
    s->next_interval_jitter_us = jitter_s * 1.0e6;
    s->next_event_time_s += s->adjusted_interval_s + offset_s + jitter_s + s->current_bph_wander_us * 1.0e-6;
    s->next_event_sample_index = (uint64_t)llround(s->next_event_time_s * (double)cfg->sample_rate_hz);
}

/*
    Start a new acoustic packet and return its ground-truth event record.

    The generated event time is the A/onset time. The packet includes early
    A-like lobes, optional middle impacts, and one or more C-like lobes near the
    computed A->C time. In realistic mode, the C zone is a small cluster rather
    than a single perfect sinusoid.
*/
static WatchSynthStreamEvent ws_start_packet(WatchSynthStream *s) {
    const WatchSynthStreamConfig *cfg = &s->cfg;
    WatchSynthActivePacket *p = ws_alloc_packet(s);
    WatchSynthStreamEvent e;
    WatchSynthEventKind kind = (s->beat_index & 1u) ? WATCH_SYNTH_EVENT_TOCK : WATCH_SYNTH_EVENT_TICK;
    double a_to_c_s = watch_synth_stream_compute_a_to_c_time_s(cfg);
    double packet_duration_s = a_to_c_s + cfg->packet_tail_after_c_s;
    uint64_t packet_samples = (uint64_t)ceil(packet_duration_s * (double)cfg->sample_rate_hz) + 2u;
    double gain = cfg->pcm_peak_amplitude;
    double freq_scale = 1.0, c_gain = 1.0;

    if (cfg->packet_gain_variation > 0.0) gain *= 1.0 + cfg->packet_gain_variation * ws_rand_signed(&s->rng_state);
    if (cfg->enable_amplitude_drift) {
        double phase = 2.0 * M_PI * s->next_event_time_s / cfg->amplitude_drift_period_s;
        gain *= 1.0 + cfg->amplitude_drift_depth * sin(phase);
    }
    gain = ws_clamp(gain, 0.0, 1.5);

    memset(p, 0, sizeof(*p));
    p->active = 1;
    p->start_sample_index = s->next_event_sample_index;
    p->end_sample_index = p->start_sample_index + packet_samples;
    p->kind = kind;
    p->polarity = kind == WATCH_SYNTH_EVENT_TICK ? 1.0 : -0.94;
    p->packet_gain = gain;

    if (cfg->enable_tick_tock_spectral_diff) {
        if (kind == WATCH_SYNTH_EVENT_TICK) { freq_scale = 1.04; c_gain = 1.00; }
        else { freq_scale = 0.93; c_gain = 0.91; }
    }
    if (cfg->enable_realistic_packet) {
        /* A-like onset cluster: early impulse/fork activity near event time. */
        ws_add_varied_lobe(s,p,packet_duration_s,0.00000,0.22,2300.0,0.00085,freq_scale);
        ws_add_varied_lobe(s,p,packet_duration_s,0.00028,0.18,4100.0,0.00070,freq_scale);
        ws_add_varied_lobe(s,p,packet_duration_s,0.00072,0.13,6800.0,0.00055,freq_scale);

        /* Middle low-energy impacts/rattle before the main C zone. */
        ws_add_varied_lobe(s,p,packet_duration_s,0.00215,0.16,5200.0,0.00085,freq_scale);
        ws_add_varied_lobe(s,p,packet_duration_s,0.00305,0.10,8800.0,0.00060,freq_scale);

        /*
            C-like locking/banking cluster.

            With C-peak lock enabled, a narrow Gaussian C anchor is placed
            exactly at the computed A->C time. The surrounding ringing lobes
            remain present but are deliberately lower, so a later C-ring peak
            should not steal the amplitude measurement at 192 kHz or higher.
        */
        if (cfg->enable_c_peak_lock) {
            double post = cfg->post_c_lobe_scale;
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s-0.00105,0.12*c_gain,4700.0,0.00065,freq_scale);
            ws_set_c_anchor(p, a_to_c_s, cfg->c_peak_anchor_gain * c_gain, cfg->c_peak_anchor_width_s);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s+0.00072,0.18*post*c_gain,10300.0,0.00065,freq_scale);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s+0.00175,0.10*post*c_gain,6100.0,0.00090,freq_scale);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s+0.00320,0.05*post*c_gain,2900.0,0.00150,freq_scale);
        } else {
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s-0.00105,0.34*c_gain,4700.0,0.00110,freq_scale);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s,1.00*c_gain,7600.0,0.00185,freq_scale);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s+0.00062,0.53*c_gain,10300.0,0.00120,freq_scale);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s+0.00170,0.24*c_gain,6100.0,0.00180,freq_scale);
            ws_add_varied_lobe(s,p,packet_duration_s,a_to_c_s+0.00320,0.12*c_gain,2900.0,0.00300,freq_scale);
        }
    } else {
        ws_add_lobe(p, 0.0000, 0.38, 2800.0, 0.0013);
        if (cfg->enable_c_peak_lock) {
            ws_set_c_anchor(p, a_to_c_s, cfg->c_peak_anchor_gain, cfg->c_peak_anchor_width_s);
            ws_add_lobe(p, a_to_c_s + 0.0008, 0.10 * cfg->post_c_lobe_scale, 9800.0, 0.0008);
        } else {
            ws_add_lobe(p, a_to_c_s, 1.00, 7600.0, 0.0018);
            ws_add_lobe(p, a_to_c_s + 0.0007, 0.36, 9800.0, 0.0010);
        }
    }

    memset(&e, 0, sizeof(e));
    e.beat_index = s->beat_index;
    e.kind = kind;
    e.time_s = s->next_event_time_s;
    e.sample_index = s->next_event_sample_index;
    e.packet_gain = gain;
    e.a_to_c_time_s = a_to_c_s;
    e.watch_amplitude_degrees = cfg->watch_amplitude_degrees;
    e.lift_angle_degrees = cfg->lift_angle_degrees;
    e.bph_wander_us = s->current_bph_wander_us;
    if (s->beat_index > 0) {
        e.interval_from_previous_us = (s->next_event_time_s - s->last_event_time_s) * 1.0e6;
        e.applied_interval_offset_us = s->next_interval_offset_us;
        e.timing_jitter_us = s->next_interval_jitter_us;
    }
    s->last_event_time_s = s->next_event_time_s;
    ++s->beat_index;
    ws_schedule_next_event(s, kind);
    return e;
}

static double ws_packet_sample(WatchSynthActivePacket *p, uint64_t abs_sample, double sr) {
    size_t i; double y = 0.0;
    if (!p->active) return 0.0;
    if (abs_sample < p->start_sample_index) return 0.0;
    if (abs_sample >= p->end_sample_index) { p->active = 0; return 0.0; }
    for (i = 0; i < p->lobe_count; ++i) {
        const WatchSynthLobe *l = &p->lobes[i];
        double rel_s = (double)(abs_sample - p->start_sample_index) / sr - l->delay_s;
        if (rel_s >= 0.0) {
            double attack = rel_s < 0.00012 ? rel_s / 0.00012 : 1.0;
            double env = exp(-rel_s / l->tau_s);
            y += p->polarity * p->packet_gain * l->rel_amp * attack * env * sin(2.0 * M_PI * l->freq_hz * rel_s);
        }
    }
    if (p->c_anchor_enabled) {
        double rel_s = (double)(abs_sample - p->start_sample_index) / sr - p->c_anchor_delay_s;
        double w = p->c_anchor_width_s > 0.0 ? p->c_anchor_width_s : 0.000020;
        double g = exp(-0.5 * (rel_s / w) * (rel_s / w));
        y += p->polarity * p->packet_gain * p->c_anchor_gain * g;
    }
    return y;
}

static double ws_resonator(double x, double *s1, double *s2, double sr, double f, double q, double gain) {
    double bw, r, w, a1, a2, st;
    if (gain == 0.0 || f <= 0.0 || q <= 0.0) return x;
    f = ws_clamp(f, 20.0, 0.45 * sr); bw = f / q; r = exp(-M_PI * bw / sr); w = 2.0 * M_PI * f / sr;
    a1 = 2.0 * r * cos(w); a2 = -(r * r); st = x + a1 * (*s1) + a2 * (*s2); *s2 = *s1; *s1 = st;

    /*
        Normalize resonator contribution by (1-r). Without this, high sample
        rates make r very close to 1 and the recursive state can grow enough to
        clip, creating artificial late peaks.
    */
    return x + gain * (1.0 - r) * st;
}
static double ws_lp(double x, double *state, double cutoff, double sr) { double a = exp(-2.0 * M_PI * ws_clamp(cutoff, 1.0, 0.45*sr) / sr); *state = (1.0-a)*x + a*(*state); return *state; }
static double ws_hp(double x, double *lp_state, double cutoff, double sr) { return x - ws_lp(x, lp_state, cutoff, sr); }
static double ws_noise(WatchSynthStream *s) {
    const WatchSynthStreamConfig *cfg = &s->cfg; double n;
    if (cfg->noise_peak_amplitude <= 0.0) return 0.0;
    n = cfg->noise_peak_amplitude * ws_rand_signed(&s->rng_state);
    if (cfg->enable_bandlimited_noise) { n = ws_hp(n, &s->noise_hp_low_state, cfg->noise_low_hz, (double)cfg->sample_rate_hz); n = ws_lp(n, &s->noise_lp_state, cfg->noise_high_hz, (double)cfg->sample_rate_hz); }
    return n;
}

WatchSynthStreamFillResult watch_synth_stream_fill_f32(WatchSynthStream *s, float *out_pcm, size_t out_count, WatchSynthStreamEvent *events, size_t event_capacity) {
    WatchSynthStreamFillResult r; size_t i; double sr;
    memset(&r, 0, sizeof(r));
    if (!s || !out_pcm || out_count == 0) return r;
    r.first_sample_index = s->absolute_sample_index; sr = (double)s->cfg.sample_rate_hz;
    for (i = 0; i < out_count; ++i) {
        uint64_t abs_sample = s->absolute_sample_index; double y = 0.0; size_t pidx;
        while (s->next_event_sample_index <= abs_sample) {
            WatchSynthStreamEvent e = ws_start_packet(s);
            if (events) {
                if (r.events_written < event_capacity) events[r.events_written++] = e;
                else r.events_dropped++;
            }
        }
        for (pidx = 0; pidx < WATCH_SYNTH_MAX_ACTIVE_PACKETS; ++pidx) y += ws_packet_sample(&s->active_packets[pidx], abs_sample, sr);
        if (s->cfg.enable_sensor_resonance) {
            y = ws_resonator(y, &s->resonator1_s1, &s->resonator1_s2, sr, s->cfg.sensor_resonance1_hz, s->cfg.sensor_resonance1_q, s->cfg.sensor_resonance1_gain);
            y = ws_resonator(y, &s->resonator2_s1, &s->resonator2_s2, sr, s->cfg.sensor_resonance2_hz, s->cfg.sensor_resonance2_q, s->cfg.sensor_resonance2_gain);
        }
        y += ws_noise(s);
        out_pcm[i] = (float)ws_clamp(y, -1.0, 1.0);
        ++s->absolute_sample_index;
    }
    r.samples_written = out_count; r.next_sample_index = s->absolute_sample_index;
    return r;
}
