# EXP-08 / Experiment for [[QAS-02](../README.md#qas-02-end-to-end-latency)]: Measure current performance

## Objective

Measure the current system's end-to-end performance (latency, drops, bottleneck) to understand the level achieved today, and use it to set the performance targets and the improvement plan.

## Status

- [Planned | In progress | Suspended | **Canceled** | Concluded]

## Expected outcomes

- A baseline of the current system's latency (including per-stage), drops, and processing bottleneck

- Identification of which stage is slow (the bottleneck)

- Performance targets and follow-up plan derived from the measured level

## Resources required

- Raspberry Pi 5 (target device)

- Microphone (Live) or Sim-mode signal (measurement input)

- Performance log (perf_log.csv) collection

## Experiment description

1. On Live (192k sps, tab switching), collect end-to-end and per-stage latency, drops, and resource use into the log

2. Aggregate per stage to identify the bottleneck and summarize the current level

## Duration

06/08-06/12

## Links and references

[QAS-02](../README.md#qas-02-end-to-end-latency)

## Results and recommendations

- (06/05) Performance log (perf_log.csv) generation implemented — metrics at each point (capture → processing → display) are now recorded to the log.

- This experiment is canceled. Because this experiment is almost similar as [EXP-02](EXP-02-end-to-end-latency-measurement.md)
