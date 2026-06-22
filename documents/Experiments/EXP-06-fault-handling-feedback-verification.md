# EXP-06 / Experiment for [[QAS-07](../README.md#qas-07-graceful-degradation-and-fault-feedback)]: Fault-handling & feedback verification

## Objective

Verify the system detects faults (signal loss, missed beats, out-of-range values) and gives clear feedback within 2 s while preserving the last valid value.

## Status

- [Planned | In progress | Suspended | **Canceled** | Concluded]

## Expected outcomes

- Error / warning shown ≤ 2 s after a fault

- Invalid outputs = 0 during faults

- Last valid value preserved; threshold lines always visible

## Resources required

- Raspberry Pi 5 + Live capture setup

- Fault injection (disconnect mic, inject noise, force out-of-range)

## Experiment description

- Inject each fault type (signal loss, missed beat)

- Observe the error / status indication and its timing

- Confirm the last valid value is preserved and no invalid output is shown

## Duration

06/24–07/01

## Links and references

[QAS-07](../README.md#qas-07-graceful-degradation-and-fault-feedback) · CON-OP-01

## Results and recommendations

This experiment was not conducted because its results do not determine the direction of the SW architecture.
