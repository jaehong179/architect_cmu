# EXP-05 / Experiment for [[QAS-05](../Requirements/quality-attribute-requirements.md#qas-05-noise-environment-robustness)]: Noise-environment robustness

## Objective

Quantify measurement degradation when 60 dB SPL speech noise is injected alongside the watch signal.

## Status

- [Planned | In progress | Suspended | **Canceled** | Concluded]

## Expected outcomes

- under 60 dB SPL noise

- Measurement error increase ≤ 2× the noise-free baseline

## Resources required

- Raspberry Pi 5 + Weishi microphone + speaker

- 60 dB SPL speech-noise file

## Experiment description

- Play back 60 dB speech noise while capturing the watch signal

- Track delta shifts versus the noise-free baseline

- Record detection rate and error degradation

## Duration

06/22–06/26

## Links and references

[QAS-05](../Requirements/quality-attribute-requirements.md#qas-05--noise-environment-robustness) · [RISK-04](../README.md#risk-04) · [FR-SPT-5](../Requirements/functional-requirements.md#fr-spt-5)

## Results and recommendations

This experiment was not conducted because its results do not determine the direction of the SW architecture.
