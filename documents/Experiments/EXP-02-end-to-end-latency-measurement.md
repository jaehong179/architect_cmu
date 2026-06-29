# EXP-02 / Experiment for [[QAS-02](../Requirements/quality-attribute-requirements.md#qas-02-end-to-end-latency)]: End-to-end latency measurement

## Objective

Measure end-to-end latency from a microphone impulse to the GUI update and verify it stays within 100 ms under peak load (13 tabs active).

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- End-to-end latency distribution

- Confirmation that < 100 ms

- Identification of the slowest pipeline stage

## Resources required

- Raspberry Pi 5 + audio capture setup
  
- Timestamped logging across pipeline stages

- Peak-load scenario (13 tabs active)

## Experiment description

- Insert timestamp logging at each pipeline stage: (1) audio block captured, (2) block processed for beat detection/measurement, (3) waveform and readings displayed in the GUI.

- Measure elapsed time from microphone input to GUI update under peak load (12 tabs active), reporting capture-to-processing, processing-to-display, and total latency (average and worst-case), plus dropped blocks and missed beats.

## Duration

06/15–06/26

## Links and references

- [QAS-02](../Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency)
- [RISK-02](../README.md#risk-02)
- [AV-003](../ArchitectureViews/AV-003-MicToGraph-SeqenceDiagram.md)

## Results and recommendations

TBD
