# EXP-02 / Experiment for [[QAS-02](../Requirements/quality-attribute-requirements.md#qas-02-end-to-end-latency)]: End-to-end latency measurement

## Objective

Measure end-to-end latency from a microphone impulse to the GUI update and verify it stays within 100 ms under peak load.

## Status

- [Planned | **In progress** | Suspended | Canceled | Concluded]

## Expected outcomes

- End-to-end latency distribution

- Confirmation that < 100 ms

- Identification of the slowest pipeline stage

## Resources required

- Source code analysis

- Raspberry Pi 5 + audio capture setup

- Timestamped logging across pipeline stages

## Experiment description

- Source code based analysis: Analyze the computational complexity of the processing whole graph.

- Log based analysis: Measure elapsed time from microphone input to GUI update under peak load (12 tabs active)

## Duration

06/15–06/26

## Links and references

- [QAS-02](../Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency)
- [RISK-02](../README.md#risk-02)
- [AV-003](../ArchitectureViews/AV-003-MicToGraph-SeqenceDiagram.md)

## Results and recommendations

- Source code based analysis: All required graphs use the same Tic/Toc calculations and wave signal processing as the Rate/Scope tab and Sound Print graph. Therefore, they have the same algorithmic and computational complexity, and no additional processing overhead is expected based on the provided source code.

- Log based : TBD
