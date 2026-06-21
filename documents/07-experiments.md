# **7. Experiments**

This section documents the technical experiments (Agile spikes) used to evaluate and validate the quality-attribute requirements. Each experiment follows the standard technical-experiment template. Full details for each experiment are in [Experiments/](Experiments/).

## 7.1 Experiments Summary

| EXP | Title | Status | Duration |
|-----|-------|--------|----------|
| [EXP-03](Experiments/EXP-03-sustained-operation-resource-thermal-stability.md) | Experiment for [[QAS-03](04-quality-attribute-requirements.md#qas-03-long-run-resource-stability)]: 30-minute sustained-operation resource & thermal stability | Planned | 06/22–06/26 |
| [EXP-04](Experiments/EXP-04-measurement-accuracy-t1-t3-detection-rate.md) | Experiment for [[RISK-17](06-risk-management.md#risk-17)]: Measurement Accuracy & T1/T3 Detection-Rate Evaluation (vs. Sim Ground Truth) | Planned | 06/22–06/26 |
| [EXP-05](Experiments/EXP-05-noise-environment-robustness.md) | Experiment for [[QAS-05](04-quality-attribute-requirements.md#qas-05-noise-environment-robustness)]: Noise-environment robustness | Planned | 06/22–06/26 |
| [EXP-06](Experiments/EXP-06-fault-handling-feedback-verification.md) | Experiment for [[QAS-07](04-quality-attribute-requirements.md#qas-07-graceful-degradation-and-fault-feedback)]: Fault-handling & feedback verification | Planned | 06/24–07/01 |
| [EXP-13](Experiments/EXP-13-timing-precision-verification.md) | Experiment for [[QAS-04](04-quality-attribute-requirements.md#qas-04-measurement-timing-precision-preservation)]: Timing-Precision Verification (Using Existing E-2 Instrumentation) | Planned | 06/22–06/26 |
| [EXP-02](Experiments/EXP-02-end-to-end-latency-measurement.md) | Experiment for [[QAS-02](04-quality-attribute-requirements.md#qas-02-end-to-end-latency)]: End-to-end latency measurement | In progress | 06/15–06/19 |
| [EXP-18](Experiments/EXP-18-camera-tinyml-9-position-accuracy.md) | Camera+TinyML 9-Position Accuracy & Per-Mode Fallback Verification | In progress | 06/12–06/26 |
| [EXP-07](Experiments/EXP-07-cross-platform-build-deployment.md) | Experiment for [[QAS-10](04-quality-attribute-requirements.md#qas-10-pc-pi-platform-separation)]: Cross-platform build & deployment | Concluded | 05/25–06/08 |
| [EXP-09](Experiments/EXP-09-legacy-codebase-comprehension-reverse-engineering.md) | Legacy Codebase Comprehension & Reverse Engineering via AI | Concluded | 06/03–06/04 |
| [EXP-10](Experiments/EXP-10-realtime-tinyml-inference-performance.md) | Experiment for [FR-AI-1]: Real-time TinyML Inference Performance on Raspberry Pi | Concluded | 06/04–06/08 |
| [EXP-12](Experiments/EXP-12-usb-protocol-watch-position-detection.md) | Experiment for [FR-POS-1]: USB Protocol Analysis for Automated Watch Position Detection | Concluded | 06/03–06/04 |
| [EXP-14](Experiments/EXP-14-microphone-disconnect-behavior.md) | Experiment for [[RISK-19](06-risk-management.md#risk-19)][[QAS-11](04-quality-attribute-requirements.md#qas-11-microphone-disconnect-user-notification)]: Characterize current behavior on microphone (USB) disconnect | Concluded | 06/15–06/16 |
| [EXP-15](Experiments/EXP-15-buffer-memory-management-tactic-analysis.md) | Experiment for [[RISK-01](06-risk-management.md#risk-01)][[QAS-01](04-quality-attribute-requirements.md#qas-01-real-time-streaming-throughput)][[QAS-03](04-quality-attribute-requirements.md#qas-03-long-run-resource-stability)]: Buffer/Memory-Management Tactic Analysis (Static-Analysis Based) → ADR-03 | Concluded | 06/15–06/16 |
| [EXP-16](Experiments/EXP-16-fault-detection-notification-tactic-analysis.md) | Experiment for [[RISK-19](06-risk-management.md#risk-19)][[QAS-07](04-quality-attribute-requirements.md#qas-07-graceful-degradation-and-fault-feedback)][[QAS-11](04-quality-attribute-requirements.md#qas-11-microphone-disconnect-user-notification)]: Fault-Detection & Notification Tactic Analysis (Static-Analysis Based) → ADR-02 | Concluded | 06/15–06/16 |
| [EXP-17](Experiments/EXP-17-rule-signal-processing-vs-tinyml-boundary.md) | Experiment for [[RISK-05](06-risk-management.md#risk-05)][[RISK-09](06-risk-management.md#risk-09)][FR-AI]: Rule/Signal-Processing vs. TinyML Responsibility-Boundary Analysis (Static-Analysis Based) → ADR-01 | Concluded | 06/15–06/17 |
| [EXP-01](Experiments/EXP-01-multi-sample-rate-capture-stability.md) | Experiment for [[QAS-01](04-quality-attribute-requirements.md#qas-01-real-time-streaming-throughput)]: Multi-sample-rate capture stability | Canceled | 06/08–06/19 |
| [EXP-08](Experiments/EXP-08-measure-current-performance.md) | Experiment for [[QAS-02](04-quality-attribute-requirements.md#qas-02-end-to-end-latency)]: Measure current performance | Canceled | 06/08–06/12 |
| [EXP-11](Experiments/EXP-11-ai-denoising-bypass-manual-labeling.md) | Experiment for [FR-AI-1]: AI-based Denoising Approach to Bypass Manual Labeling | Canceled | 06/15–06/22 |
