# EXP-17 / Experiment for [[RISK-05](../README.md#risk-05)][[RISK-09](../README.md#risk-09)][[FR-AI](../README.md#fr-ai-1)]: Rule/Signal-Processing vs. TinyML Responsibility-Boundary Analysis (Static-Analysis Based) → [ADR-01](../ADRs/ADR-001-watch-position-detection-solution.md)

## Objective

- Statically analyze the diagnosis sub-tasks to produce the evidence (task decomposition, tactic comparison) for the rule/signal-processing vs. TinyML boundary. The final decision, rationale, and consequences are recorded in [ADR-001](../ADRs/ADR-001-watch-position-detection-solution.md).

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- Decomposition of the diagnosis pipeline

- a per-task placement table (measurement-trust involvement, deterministic-algorithm sufficiency, ML data/sensor availability)

- the design decision based on this analysis is recorded in [ADR-001](../ADRs/ADR-001-watch-position-detection-solution.md) (this experiment provides the evidence).

## Resources required

- Analysis of existing source (Detector, Bph, measurement) and Sim mode, domain knowledge, 2–3 person-days, an IDE. No model training or measurement rigs.

## Experiment description

- Decompose the feature (detection, computation, health grading, fault hint, signal anomaly, position reading)

- For each task analyze measurement-trust involvement, deterministic-algorithm sufficiency, and ML data/sensor availability.

- Review deterministic sufficiency of the acoustic path and low-SNR options (adaptive threshold / matched filter).

- Identify the task where ML adds value with realistic labeling.

- Hand the analysis to [ADR-001](../ADRs/ADR-001-watch-position-detection-solution.md) (the decision/rationale is finalized there).

## Duration

06/15-06/17

## Links and references

[ADR-001](../ADRs/ADR-001-watch-position-detection-solution.md) (responsibility-boundary decision); [RISK-05](../README.md#risk-05), [RISK-09](../README.md#risk-09); [FR-AI](../README.md#fr-ai-1)/FR-POS; external camera; follow-up [EXP-18](EXP-18-camera-tinyml-9-position-accuracy.md); [EXP-12](EXP-12-usb-protocol-watch-position-detection.md).

## Results and recommendations

| Sub-task | Affects measurement trust? | Deterministic algorithm sufficient? | ML data/sensor | Analysis note |
|---|---|---|---|---|
| rate/amp/BE/BPH computation | Yes (core) | Yes (closed-form) | — | Rule suitable |
| tick/tock detection | Yes (root) | Yes (low-SNR via adaptive threshold/matched filter) | — | Rule/signal-processing suitable |
| health grading & score | Yes (user-facing) | Yes (domain thresholds) | — | Rule suitable |
| fault/cause hint | Moderate | Yes (pattern→cause) | — | Rule suitable |
| signal anomaly detection | No (advisory) | Partially | — | Achievable with rules/signal-processing |
| watch position reading | No (out-of-path advisory) | No (visual orientation hard to express as rules) | External camera + per-orientation images (labeling feasible) | Strong ML candidate |
| trend prediction | No | — | Long history needed | Deferred |
