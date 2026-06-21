# EXP-04 / Experiment for [RISK-17]: Measurement Accuracy & T1/T3 Detection-Rate Evaluation (vs. Sim Ground Truth)

## Objective

Against Sim-mode ground truth, evaluate whether T1/T3 detection rate and Rate/Beat-Error/Amplitude measurement accuracy fall within target bounds.

## Status

- [**Planned** | In progress | Suspended | Canceled | Concluded]

## Expected outcomes

- T1/T3 detection rate, onset/peak identification error, and (measured − configured) error distributions for Rate, Beat Error, and Amplitude; whether targets are met.

## Resources required

- Raspberry Pi 5 / PC, Sim mode (built-in GtBeats ground truth), existing instrumentation (perf_log.csv: E-2 onset/peak_err, G-1 accuracy, G-2 detection rate), tools/analyze_perf.py.

## Experiment description

- Configure Sim mode (BPH, Amplitude, Beat Error, Realistic; 96k/192k).

- Run ≥1,000 beats.

- Collect E-2 (onset/peak_err), G-1 (rate/beaterr/amp_err), G-2 (a_match/c_match/gt_total) from perf_log.csv.

- Compare detection rate/errors to targets.

- Combine with the Weishi cross-validation (real-watch reference) to confirm accuracy trustworthiness.

## Duration

06/22–06/26

## Links and references

RISK-17 · FR-MSB-1 · FR-CP-4 · CON-RF-02

## Results and recommendations

*(to be completed after the experiment)*
