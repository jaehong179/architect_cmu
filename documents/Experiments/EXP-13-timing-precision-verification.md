# EXP-13 / Experiment for [QAS-04]: Timing-Precision Verification (Using Existing E-2 Instrumentation)

## Objective

- For a Sim signal with known ground truth (GtBeats), confirm that the real-time pipeline's detection-timing error (E-2 onset/peak_err) is worst-case ≤ 1 sample, and that this precision is preserved as CPU load, sample rate, and signal realism change.

## Status

- [**Planned** | In progress | Suspended | Canceled | Concluded]

## Expected outcomes

- Per-condition mean/σ/worst-case of onset_err_ms/peak_err_ms; pass/fail against the ≤ 1 sample criterion; whether the error grows under load/config changes (indirect indicator of design precision preservation).

## Resources required

- Raspberry Pi 5, existing TimeGrapher build, Sim mode (built-in GtBeats ground truth), existing instrumentation (perf_log.csv: E-2 onset/peak_err, G-2 detection rate), tools/analyze_perf.py. No new code.

## Experiment description

- Configure Sim mode (BPH, Amplitude, Beat Error, Realistic, sps) and run ≥1,000 beats.

- Existing instrumentation logs per-beat E-2 onset_err_ms/peak_err_ms to perf_log.csv.

- Repeat across conditions: ① no CPU load vs peak load, ② 96k vs 192k sps, ③ Sim Clean vs Realistic.

- Use tools/analyze_perf.py to compute worst-case error per condition.

- Judge whether worst-case ≤ 1 sample and whether the error stays stable across conditions (precision preservation).

## Duration

06/22–06/26

## Links and references

QAS-04 · RISK-18

## Results and recommendations

*(to be completed after the experiment)*
