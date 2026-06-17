#!/usr/bin/env python3
"""
Synthetic mechanical-watch beat-noise generator for the fault categories shown
in Witschi-Training-Course.pdf, pages 14-15 ("Error detection with graphical
charts"). Pure standard library, no numpy/scipy required.

Physical model (manual pages 5-8):
    Each beat produces three acoustic pulses:
      pulse 1 - impulse pin strikes the pallet fork. Precise; used for rate
                and beat-error timing.
      pulse 2 - escape-wheel tooth meets the pallet stone / fork touches the
                impulse pin. Irregular; not used for measurement.
      pulse 3 - escape-wheel tooth meets the locking plane, lever hits the
                banking pin. Most powerful; its delay after pulse 1 (the
                "lift time") encodes the balance amplitude.

The lift time (A-to-C time) is derived from amplitude and lift angle with the
same sinusoidal-balance approximation already used in WatchSynthStream.cpp:

    beat_interval_s = 3600 / bph
    A_to_C_s = (2 * beat_interval_s / pi) * asin(lift_angle_deg / (2 * amplitude_deg))

Output format:
    Mono, 32-bit IEEE-float PCM (WAVE_FORMAT_IEEE_FLOAT / audioFormat=3),
    canonical 44-byte header, matching TimeGrapherTestFilesWeishiMic/*.wav and
    the format this app's TWaveHeader (WaveHeader.h) and MainWindow::OpenFile /
    TPlaybackWorker accept. The sample rate must be one of the rates the app's
    SampleRatesComboBox offers (MainWindow.cpp PopulateSampleRates): 48000,
    96000, 192000 or 384000. Any other rate makes SetAudioRate() fail silently,
    which is reported back as "Invalid PCM Wave File".

Usage:
    python3 tools/generate_fault_wavs.py                 # generate all 14 files
    python3 tools/generate_fault_wavs.py --list           # show categories
    python3 tools/generate_fault_wavs.py --only knock     # filter by key substring
    python3 tools/generate_fault_wavs.py --outdir out --seconds-scale 0.5
"""

import argparse
import math
import os
import random
import struct
import sys
from array import array
from dataclasses import dataclass, field
from typing import Any, Callable, List, Tuple, Union

SAMPLE_RATE = 48000
SUPPORTED_SAMPLE_RATES = (48000, 96000, 192000, 384000)

Number = Union[int, float]
TimeFn = Callable[[float], float]


def as_fn(value: Union[Number, TimeFn]) -> TimeFn:
    return value if callable(value) else (lambda t, v=value: v)


def compute_a_to_c_time_s(beat_interval_s: float, lift_angle_deg: float,
                           amplitude_deg: float, lo: float = 0.0008, hi: float = 0.05) -> float:
    ratio = lift_angle_deg / (2.0 * amplitude_deg)
    ratio = min(max(ratio, 0.0), 0.999999)
    t = (2.0 * beat_interval_s / math.pi) * math.asin(ratio)
    return min(max(t, lo), hi)


def make_random_walk(seed: int, step: float, clip: float, start: float = 0.0) -> TimeFn:
    rng = random.Random(seed)
    state = [start]

    def fn(_t: float) -> float:
        state[0] = max(-clip, min(clip, state[0] + rng.gauss(0.0, step)))
        return state[0]

    return fn


@dataclass
class WatchProfile:
    key: str
    title: str
    action: str
    bph: float = 28800.0
    lift_angle_deg: float = 52.0
    duration_s: float = 20.0
    rate_error_s_per_day: Union[Number, TimeFn] = 8.0
    beat_error_ms: Union[Number, TimeFn] = 0.2
    amplitude_deg: Union[Number, TimeFn] = 280.0
    timing_jitter_us: float = 4.0
    position_segments: List[Tuple[float, float]] = field(default_factory=list)
    knock_prob: float = 0.0
    entry_pallet_fault: bool = False
    scratch_prob: float = 0.0
    noise_level: float = 0.004
    seed: int = 0


def rate_at(t: float, profile: WatchProfile, rate_fn: TimeFn) -> float:
    if not profile.position_segments:
        return rate_fn(t)
    elapsed = 0.0
    for seg_dur, seg_rate in profile.position_segments:
        if t < elapsed + seg_dur:
            return seg_rate
        elapsed += seg_dur
    return profile.position_segments[-1][1]


def add_damped_sine(buf: array, sr: int, onset_sample: int, delay_s: float, amp: float,
                     freq_hz: float, tau_s: float, polarity: float, n_total: int,
                     threshold: float = 1e-4) -> None:
    if amp == 0.0:
        return
    max_t = tau_s * math.log(abs(amp) / threshold)
    n_samples = int(max_t * sr) + 1
    start = onset_sample + int(round(delay_s * sr))
    omega = 2.0 * math.pi * freq_hz
    lo = max(0, -start)
    hi = min(n_samples, n_total - start)
    for n in range(lo, hi):
        tt = n / sr
        buf[start + n] += polarity * amp * math.exp(-tt / tau_s) * math.sin(omega * tt)


def add_scratch_burst(buf: array, sr: int, start_sample: int, rng: random.Random,
                       n_total: int, amp: float = 0.16, dur_s: float = 0.009) -> None:
    if start_sample < 0 or start_sample >= n_total:
        return
    n = int(dur_s * sr)
    hi = min(n, n_total - start_sample)
    freq = rng.uniform(2500.0, 7000.0)
    for i in range(hi):
        tt = i / sr
        env = math.sin(math.pi * i / n) if n > 0 else 0.0
        buf[start_sample + i] += amp * env * (0.5 * rng.uniform(-1.0, 1.0) + math.sin(2.0 * math.pi * freq * tt))


def render_beat(buf: array, sr: int, onset_sample: int, a_to_c_s: float, is_tick: bool,
                 profile: WatchProfile, rng: random.Random, n_total: int) -> None:
    polarity = 1.0 if is_tick else -0.94

    # Pulse 1: impulse pin / pallet fork. Sharp and consistent (used for timing).
    add_damped_sine(buf, sr, onset_sample, 0.0000, 0.85, 2350.0, 0.00090, polarity, n_total)

    # Pulse 2: escape-wheel tooth / pallet stone contact. Inherently irregular per the manual.
    p2_delay = a_to_c_s * rng.uniform(0.25, 0.55)
    add_damped_sine(buf, sr, onset_sample, p2_delay, 0.28 * rng.uniform(0.7, 1.3),
                     5200.0 * rng.uniform(0.9, 1.1), 0.00075, polarity, n_total)

    # Pulse 3 cluster: locking plane / banking pin. Drives the amplitude reading.
    c_amp_scale, c_tau_scale = 1.0, 1.0
    if profile.entry_pallet_fault and is_tick:
        # The entry pallet only engages every other beat; "clips poorly / smeared"
        # shows up as a weaker, longer (less defined) third pulse on those beats only.
        c_amp_scale, c_tau_scale = 0.35, 1.8

    add_damped_sine(buf, sr, onset_sample, a_to_c_s - 0.00080, 0.45 * c_amp_scale, 4500.0,
                     0.00090 * c_tau_scale, polarity, n_total)
    add_damped_sine(buf, sr, onset_sample, a_to_c_s, 1.00 * c_amp_scale, 7200.0,
                     0.00160 * c_tau_scale, polarity, n_total)
    add_damped_sine(buf, sr, onset_sample, a_to_c_s + 0.00070, 0.50 * c_amp_scale, 9800.0,
                     0.00110 * c_tau_scale, polarity, n_total)
    add_damped_sine(buf, sr, onset_sample, a_to_c_s + 0.00180, 0.22 * c_amp_scale, 6000.0,
                     0.00200 * c_tau_scale, polarity, n_total)

    if profile.knock_prob > 0.0 and rng.random() < profile.knock_prob:
        # Over-banked balance: fork horn hits the banking pin a second time -> "double tic-tac".
        add_damped_sine(buf, sr, onset_sample, a_to_c_s + 0.00350, 0.85, 3300.0, 0.00100,
                         polarity, n_total)


def synthesize(profile: WatchProfile, sample_rate: int = SAMPLE_RATE) -> array:
    sr = sample_rate
    n_total = int(profile.duration_s * sr)
    buf = array('d', bytes(n_total * 8))
    rng = random.Random(profile.seed)

    rate_fn = as_fn(profile.rate_error_s_per_day)
    beat_err_fn = as_fn(profile.beat_error_ms)
    amp_fn = as_fn(profile.amplitude_deg)
    nominal_interval = 3600.0 / profile.bph

    t = 0.05
    beat_index = 0
    while t < profile.duration_s:
        is_tick = (beat_index % 2 == 0)
        r = rate_at(t, profile, rate_fn)
        adjusted_interval = nominal_interval / (1.0 + r / 86400.0)

        be_ms = beat_err_fn(t)
        offset_s = (be_ms * 1e-3) if is_tick else -(be_ms * 1e-3)
        jitter_s = rng.uniform(-1.0, 1.0) * profile.timing_jitter_us * 1e-6

        a_to_c_s = compute_a_to_c_time_s(nominal_interval, profile.lift_angle_deg, amp_fn(t))
        onset_sample = int(round(t * sr))
        render_beat(buf, sr, onset_sample, a_to_c_s, is_tick, profile, rng, n_total)

        if profile.scratch_prob > 0.0 and rng.random() < profile.scratch_prob:
            burst_offset_s = rng.uniform(0.01, max(0.02, adjusted_interval - 0.01))
            add_scratch_burst(buf, sr, onset_sample + int(burst_offset_s * sr), rng, n_total)

        t += adjusted_interval + offset_s + jitter_s
        beat_index += 1

    peak = 0.0
    if profile.noise_level > 0.0:
        for i in range(n_total):
            v = buf[i] + rng.gauss(0.0, profile.noise_level)
            buf[i] = v
            av = v if v >= 0.0 else -v
            if av > peak:
                peak = av
    else:
        peak = max((v if v >= 0.0 else -v) for v in buf)

    scale = 0.9 / peak if peak > 0.0 else 1.0
    for i in range(n_total):
        buf[i] *= scale
    return buf


def write_wav_float32(path: str, sr: int, samples: array) -> None:
    """Write mono 32-bit IEEE-float PCM, matching TWaveHeader (WaveHeader.h):
    audioFormat=3, bitsPerSample=32, canonical 44-byte header (fmt size 16,
    no extra chunks) -- the exact layout MainWindow::OpenFile validates.
    """
    channels = 1
    bits_per_sample = 32
    block_align = channels * bits_per_sample // 8
    byte_rate = sr * block_align
    data_size = len(samples) * block_align
    riff_size = 36 + data_size

    header = struct.pack(
        '<4sI4s4sIHHIIHH4sI',
        b'RIFF', riff_size, b'WAVE',
        b'fmt ', 16, 3, channels, sr, byte_rate, block_align, bits_per_sample,
        b'data', data_size,
    )

    body = array('f', samples)
    if sys.byteorder != 'little':
        body.byteswap()

    with open(path, 'wb') as f:
        f.write(header)
        f.write(body.tobytes())


def build_profiles() -> List[WatchProfile]:
    return [
        WatchProfile(
            key="01_normal", title="Watch movement ok",
            action="Reference baseline: no action needed.",
            rate_error_s_per_day=8.0, beat_error_ms=0.2, amplitude_deg=280.0,
            duration_s=20.0, seed=1,
        ),
        WatchProfile(
            key="02_beat_error_high", title="Beat error too high (~3 ms)",
            action="Adjust the beat error first, then readjust the rate accuracy.",
            rate_error_s_per_day=8.0, beat_error_ms=3.0, amplitude_deg=280.0,
            duration_s=20.0, seed=2,
        ),
        WatchProfile(
            key="03_rate_fast", title="Movement runs too fast (+90 s/d)",
            action="Readjust the rate, +2 up to 15 s/d.",
            rate_error_s_per_day=90.0, beat_error_ms=0.2, amplitude_deg=280.0,
            duration_s=20.0, seed=3,
        ),
        WatchProfile(
            key="04_rate_slow", title="Movement runs too slow (-90 s/d)",
            action="Readjust the rate, +2 up to 15 s/d.",
            rate_error_s_per_day=-90.0, beat_error_ms=0.2, amplitude_deg=280.0,
            duration_s=20.0, seed=4,
        ),
        WatchProfile(
            key="05_position_variation_large", title="Large rate variation between vertical positions",
            action="Centering, balancing or replacing of the complete regulating organ.",
            position_segments=[(8.0, 30.0), (8.0, -40.0), (8.0, 30.0), (8.0, -40.0)],
            beat_error_ms=0.2, amplitude_deg=270.0, duration_s=32.0, seed=5,
        ),
        WatchProfile(
            key="06_position_variation_small", title="Little rate variation, horizontal vs vertical",
            action="Adjust distance of regulator pins: close pins for V-, open pins for V+.",
            position_segments=[(8.0, 10.0), (8.0, -10.0), (8.0, 10.0), (8.0, -10.0)],
            beat_error_ms=0.2, amplitude_deg=270.0, duration_s=32.0, seed=6,
        ),
        WatchProfile(
            key="07_gear_train_defect", title="Large but regular rate variation (gear train defect)",
            action="Revision and eventually replacement of some gear train spare parts.",
            rate_error_s_per_day=lambda t, amp=35.0, period=8.0: amp * math.sin(2.0 * math.pi * t / period),
            beat_error_ms=0.2, amplitude_deg=270.0, duration_s=24.0, seed=7,
        ),
        WatchProfile(
            key="08_irregular_insufficient_amplitude", title="Irregular rate, amplitude insufficient",
            action="Overhaul.",
            rate_error_s_per_day=make_random_walk(seed=108, step=12.0, clip=70.0),
            beat_error_ms=0.4, amplitude_deg=190.0, timing_jitter_us=15.0,
            duration_s=24.0, seed=8,
        ),
        WatchProfile(
            key="09_balance_knock_occasional", title="Balance wheel knocks occasionally (amplitude > 330 deg)",
            action="Replace mainspring, pallet-stone and/or escape-wheel.",
            rate_error_s_per_day=8.0, beat_error_ms=0.3, amplitude_deg=340.0,
            knock_prob=0.15, duration_s=20.0, seed=9,
        ),
        WatchProfile(
            key="10_balance_knock_continuous", title="Balance wheel knocks continuously (amplitude > 330 deg)",
            action="Replace mainspring, pallet-stone and/or escape-wheel.",
            rate_error_s_per_day=8.0, beat_error_ms=0.3, amplitude_deg=340.0,
            knock_prob=1.0, duration_s=20.0, seed=10,
        ),
        WatchProfile(
            key="11_escape_wheel_untrue", title="Escape-wheel runs untrue",
            action="Replace escape-wheel.",
            rate_error_s_per_day=lambda t, amp=18.0, period=4.0: amp * math.sin(2.0 * math.pi * t / period),
            beat_error_ms=0.2, amplitude_deg=270.0, duration_s=24.0, seed=11,
        ),
        WatchProfile(
            key="12_entry_pallet_poor", title="Entry pallet clips poorly / smeared",
            action="Clean the escapement or replace escape-wheel.",
            rate_error_s_per_day=lambda t, dur=30.0: 5.0 + 40.0 * (t / dur) ** 2,
            beat_error_ms=0.2, amplitude_deg=270.0, entry_pallet_fault=True,
            duration_s=30.0, seed=12,
        ),
        WatchProfile(
            key="13_hairspring_touches", title="Hair spring touches regulator pins / stud",
            action="Centre hair spring, adjust rate.",
            rate_error_s_per_day=8.0, beat_error_ms=0.2, amplitude_deg=270.0,
            scratch_prob=0.18, duration_s=24.0, seed=13,
        ),
        WatchProfile(
            key="14_slow_oscillating_after_position_change", title="Slow oscillating after position change (bad lubrication)",
            action="Clean and lubricate, eventually overhaul.",
            rate_error_s_per_day=lambda t, tau=10.0, start=-150.0, target=8.0: target + (start - target) * math.exp(-t / tau),
            beat_error_ms=0.3, amplitude_deg=260.0, duration_s=30.0, seed=14,
        ),
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--outdir", default="fault_wavs", help="output directory (default: fault_wavs)")
    parser.add_argument("--only", default=None, help="only generate profiles whose key contains this substring")
    parser.add_argument("--list", action="store_true", help="list fault categories and exit")
    parser.add_argument("--seconds-scale", type=float, default=1.0, help="scale every profile's duration (e.g. 0.5 for a quick smoke test)")
    parser.add_argument("--sample-rate", type=int, default=SAMPLE_RATE, choices=SUPPORTED_SAMPLE_RATES,
                         help="output WAV sample rate; must match a rate the app's SampleRatesComboBox offers (default: 48000)")
    args = parser.parse_args()

    profiles = build_profiles()

    if args.list:
        for p in profiles:
            print(f"{p.key:<42} {p.title}\n{'':<42} -> {p.action}")
        return

    if args.only:
        profiles = [p for p in profiles if args.only in p.key]
        if not profiles:
            parser.error(f"no profile key contains '{args.only}'")

    os.makedirs(args.outdir, exist_ok=True)
    for p in profiles:
        p.duration_s *= args.seconds_scale
        samples = synthesize(p, sample_rate=args.sample_rate)
        path = os.path.join(args.outdir, f"{p.key}.wav")
        write_wav_float32(path, args.sample_rate, samples)
        print(f"wrote {path}  ({p.duration_s:.1f}s) - {p.title}")


if __name__ == "__main__":
    main()
