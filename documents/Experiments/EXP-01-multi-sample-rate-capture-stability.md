# EXP-01 / Experiment for [[QAS-01](../Requirements/quality-attribute-requirements.md#qas-01-real-time-streaming-throughput)]: Multi-sample-rate capture stability

## Objective

Determine whether the Pi 5 can sustain continuous capture at 48k / 96k / 192k sps with zero block drops, and judge whether 192k sps is a viable default target.

## Status

- [Planned | In progress | Suspended | **Canceled** | Concluded]

## Expected outcomes

- <https://github.com/jaehong179/architect_cmu/blob/exp01/scripts/arecord_test.sh>

- Drop count per 60 s at each sample rate (target: 0 drops; 192k objective ≤ 1 / 60 s)

- Average CPU and memory usage per sample rate

- Decision on whether 192k sps is adopted as the default target

## Resources required

- Raspberry Pi 5 + Weishi-style microphone

- Audio tools (arecord, top, htop)

- AGC OFF environment (CON-OP-01)

## Experiment description

- Run 60 s continuous capture at each sample rate (48k / 96k / 192k), 3 repetitions each

- Record drop count, CPU usage and memory usage; average the repetitions

- Compare results against the drops = 0 target

## Duration

06/08–06/19

## Links and references

[QAS-01](../Requirements/quality-attribute-requirements.md#qas-01--real-time-streaming-throughput) · [QAS-03](../Requirements/quality-attribute-requirements.md#qas-03--long-run-resource-stability) · [FR-CP-6](../Requirements/functional-requirements.md#fr-cp-6) · CON-OP-01

## Results and recommendations

This experiment was not conducted because its results do not determine the direction of the SW architecture.
