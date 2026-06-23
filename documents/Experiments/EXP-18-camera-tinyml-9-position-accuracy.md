# EXP-18 / Camera+TinyML 9-Position Accuracy & Per-Mode Fallback Verification

## Objective

- Verify 9-position classification accuracy and that per-mode safe fallback works (Sequence Display = manual switch; other modes = no display).

## Status

- [Planned | **In progress** | Suspended | Canceled | Concluded]

## Expected outcomes

- 9-position accuracy (confusion matrix); Sequence-Display manual switch with zero mis-records under low confidence; no-display in other modes under low confidence; Pi inference latency.

## Resources required

- Raspberry Pi 5 + external camera, per-orientation labeled image dataset, a trained TinyML model, real watches.

## Experiment description

- Collect/label 9-position images.

- Train a lightweight classifier, deploy on Pi.

- Measure per-position accuracy (confusion matrix).

- Sequence Display mode: across positions, verify values record into the correct row, and under low confidence/occlusion/unavailability it switches to manual with zero mis-records.

- Other display modes: verify the current position shows correctly (e.g., 9H) and is hidden under low confidence/unavailability.

- Verify Pi inference latency is acceptable ([RISK-09](../README.md#risk-09)).

## Duration

06/12-06/26

## Links and references

[QAS-06](../Requirements/quality-attribute-requirements.md#qas-06--position-detection-safe-fallback) ; [RISK-20](../README.md#risk-20); [ADR-01](../ADRs/ADR-001-watch-position-detection-solution.md); follow-up to [EXP-17](EXP-17-rule-signal-processing-vs-tinyml-boundary.md), [EXP-12](EXP-12-usb-protocol-watch-position-detection.md).

## Results and recommendations

*(to be completed after the experiment)*
