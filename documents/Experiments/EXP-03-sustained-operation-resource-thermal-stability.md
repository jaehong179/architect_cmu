# EXP-03 / Experiment for [QAS-03]: 30-minute sustained-operation resource & thermal stability

## Objective

Profile memory growth, CPU thermal ceilings and GUI loop stability during 30 minutes of sustained operation.

## Status

- [**Planned** | In progress | Suspended | Canceled | Concluded]

## Expected outcomes

- Memory growth ≤ 200 MB over 30 minutes

- Thermal throttling events = 0

- Stable FPS with no GUI loop degradation

## Resources required

- Raspberry Pi 5 + monitoring scripts (top, htop, /sys/class/thermal/)

- 192k sps capture workload

## Experiment description

- Run a script that uniformly cycles tab switching across a 30-minute block while capturing at 192k sps

- Log RSS, CPU% and FPS throughout

- Check for memory leaks and thermal throttling

## Duration

06/22–06/26

## Links and references

QAS-03 · [RISK-01](../06-risk-management.md#risk-01) · [RISK-02](../06-risk-management.md#risk-02)

## Results and recommendations

*(to be completed after the experiment)*
