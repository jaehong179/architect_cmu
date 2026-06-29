# LG 2026 SW Architecture Studio Project — Time Grapher Project (Team 1)

*— In collaboration with Carnegie Mellon University —*

Status: **In Progress**

## Table of Contents

1. [Overview](#1-overview)
2. [Milestones and Plan](#2-milestones-and-plan)
3. [Requirements Analysis](#3-requirements-analysis)
4. [Quality Attribute Requirements (Scenarios)](#4-quality-attribute-requirements-scenarios)
5. [Design Constraints](#5-design-constraints)
6. [Risk Management](#6-risk-management)
7. [Experiments](#7-experiments)
8. [Architecture Overview](#8-architecture-overview)
9. [Architecture Decision Records](#9-architecture-decision-records)
10. [References](#10-references)

# **1. Overview**

## **1.1 Introduction**

### **Purpose**

This document is the deliverable of the Time Grapher Project for the LG 2026 SW Architecture Studio. It covers a five-week time-boxed project that extends the baseline Qt/C++ TimeGrapher GUI on the Raspberry Pi 5 to strengthen the acoustic diagnosis and visualization of a mechanical watch.

## **1.2 Objectives**

### **Goals**

- **GUI Integration & Refinement :** Consolidating and finalizing the user interface for the Raspberry Pi 5-based real-time clock diagnostic GUI.

- **Data Reliability Verification :** Ensuring quantitative accuracy, consistency, and data integrity of clock measurements (BPH, beat error, amplitude).

- **Real-Time Performance Optimization :** Achieving ultra-low latency and high-throughput real-time processing under multi-sample-rate environments.

- **Architectural Extensibility :** Designing a modular and clean architecture to ensure easy integration of new features and components.

- **On-Device AI Exploration :** Researching secure, cloud-independent On-Device AI (TinyML) for signal quality classification.

- **Usability & Deployability :** Providing a readily reproducible demo and ensuring portability via a dedicated Raspberry Pi OS image.

### **Development Strategy: Attribute-Driven Design (ADD) & Risk-Mitigated Incremental Evolution**

Given the constrained 5-week schedule and a 7-person team, attempting a big-bang implementation of all system components introduces severe architectural risks. Therefore, this project adopts the SEI Attribute-Driven Design (ADD) process to drive an incremental and risk-mitigated development strategy:

\- Addressing Primary Architectural Drivers First

\- Incremental Expansion of Architectural Tactics

\- Early Evaluation via Test Experiments

# **2. Milestones and Plan**

## 2.1 Plan

Based on the Agile/Scrum framework. The 5 weeks are divided into 5 Sprints (Sprint 0 + Sprints 1~4).

<table>
<colgroup>
<col style="width: 9%" />
<col style="width: 10%" />
<col style="width: 9%" />
<col style="width: 29%" />
<col style="width: 33%" />
<col style="width: 7%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>Sprint</strong></th>
<th colspan="2"><strong>Duration</strong></th>
<th><strong>Purpose</strong></th>
<th><strong>Key Deliverables</strong></th>
<th><strong>Linked Milestone</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>Sprint 0</td>
<td>Week 1</td>
<td>05/25-06/05</td>
<td>Inception — Requirements·Risk·Experiment·Architecture draft</td>
<td><p>Project Plan<br />
Requirements Analysis<br />
Risk Management<br />
Architecture overview</p>
<p>Experiment Plan</p></td>
<td>MS-1 submission</td>
</tr>
<tr class="even">
<td>Sprint 1</td>
<td>Week 2</td>
<td>06/08-06/12</td>
<td>Architecture finalization &amp; experiment execution — design decisions and baseline/feasibility experiments</td>
<td>Software Architecture Document / Experiment results (baseline·feasibility: EXP-08/09/10/12/14) / Validated design decisions</td>
<td>MS-2 preparation</td>
</tr>
<tr class="odd">
<td>Sprint 2</td>
<td>Week 3</td>
<td>06/15-06/19</td>
<td>Core build &amp; initial visualization — signal capture·detection·measurement·basic display</td>
<td>Design-decision ADRs (EXP-15/16/17) / Revised Software Architecture Document / Live Mode operation · rate/BE/amp computation / Summary Bar / Vario·Sequence / Single-Beat Waveform / Scope/2·Beat Error / Long-Term Trace Display<br />
Waveform Compare<br />
Sync Sweep<br />
F0~F3 Filter
</td>
<td>MS-2 submission</td>
</tr>
<tr class="even">
<td>Sprint 3</td>
<td>Week 4</td>
<td>06/22-06/26</td>
<td>Validation of graphs<br /> AI feature implementation</td>
<td>
Anomaly detection<br />
TinyML for suggesting cause of error<br />
TinyML for detecting position of watch<br />
Usability Improvement
</td>
<td>MS-3 preparation</td>
</tr>
<tr class="odd">
<td>Sprint 4</td>
<td>Week 5</td>
<td>06/29-07/01</td>
<td>Integration<br />demo preparation<br />document finalization</td>
<td>End-to-end demo<br />
Presentation materials<br />
Lessons Learned<br /></td>
<td>MS-3 submission</td>
</tr>
</tbody>
</table>

The task-level schedule below breaks each sprint into concrete tasks with an assigned owner (by role) and start/end dates, and includes the technical experiments (spikes) as scheduled tasks. Day-to-day progress is tracked on the team Kanban board, where each backlog card carries an assignee: [Kanban Board](https://miro.com/app/board/uXjVHFnTVy0=/?share_link_id=685489384283)


## **2.2 Roles**

|                      |                              |                                            |                                                                                  |
|----------------------|------------------------------|--------------------------------------------|----------------------------------------------------------------------------------|
| **Role**             | **Owner**                    | **Key Items**                              | **R&R**                                                                          |
| Product Owner        | Nam Sangjae                  | Project management                         | Backlog prioritization·stakeholder handling·approval                             |
| Scrum Master         | Lee Tae-hoon                 | Process + system-wide                      | Daily Standup·Planning·Review·Retro·blocker removal·metrics management           |
| DSP Engineer         | Yoon Joongcheol, Nam Sangjae | SignalProcessing · Detection · Measurement | Signal-processing pipeline·filters·envelope·T1/T3 detection·measurement formulas |
| Application Engineer | Choi Jinsuk, Lee Tae-hoon    | Timegrapher · Workers · Domain model       | DSP↔UI domain flow·Worker Thread·event dispatch·Mode abstraction                 |
| UI Engineer          | Kim Jae-hong, Cho Jin-young  | Visualization Layer · MainWindow           | Rendering·interaction·UX consistency of 12 display tabs                          |
| AI Developer         | Park Jongjin                 | AI Feature                                 | TinyML model evaluation·POC integration·Raspberry Pi inference                   |

# **3. Requirements Analysis**
[Requirements/functional-requirements.md](Requirements/functional-requirements.md)

# **4. Quality Attribute Requirements (Scenarios)**
[Requirements/quality-attribute-requirements.md](Requirements/quality-attribute-requirements.md)

# **5. Design Constraints**

The following constraints apply to the whole project. All members shall be aware of and comply with them.

|           |                                                                                                                                                                           |                        |
|-----------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------|
| **ID**    | **Constraint**                                                                                                                                                            | **Source**             |
| CON-HW-01 | Runs on the CanaKit Raspberry Pi 5 Starter Kit (Pi 5, 8 GB RAM, 128 GB microSD)                                                                                           | p.26 / Raspberry Pi    |
| CON-HW-02 | 1280×800 5-inch capacitive touchscreen (HDMI + USB) environment                                                                                                           | p.27 / Touchscreen     |
| CON-HW-03 | Uses a Weishi-style timegrapher microphone input                                                                                                                          | p.28 / Time Grapher    |
| CON-SW-03 | Runs on the supplied Raspberry Pi 5 system image                                                                                                                          | p.29 / Raspberry Pi OS |
| CON-OP-01 | For consistent signal analysis and measurement, the AGC (Automatic Gain Control) of both the platform (OS) and the hardware must be turned OFF.                           | p.30 / AGC warning     |
| CON-OP-02 | Lift Angle is a user-configurable parameter                                                                                                                               | p.10 / Lift Angle note |
| CON-RF-01 | The system interface and user workflow shall be designed using the functional specification of the Witschi Chronoscope X1 G3 instruction manual as the primary reference. | p.33 / Witschi Manual  |
| CON-RF-02 | The core audio signal-processing and measurement modules shall be implemented in strict compliance with the measurement formulas defined in TimeGrapher Equations_v0.pdf. | p.33 / Equations       |

# **6. Risk Management**

Identified technical/non-technical risks with probability·impact (High-Medium-Low) assessment. Mitigation actions are absorbed into the Sprint Backlog or a Spike (EXP).

- Impact: H = milestone deliverable not possible, M = deliverable delay·quality drop, L = avoidable

- Mitigation priority: H×H → immediate Spike, H×M or M×H → next Sprint, L×H or H×L → backlog, otherwise → monitor

| **ID**      | **Description**                                                                                                                                                                                                                                        | **Probability** | **Impact** | **Related QA**         | **Notes** | **Type**      |
|-------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------|------------|------------------------|-----------|---------------|
| <a id="risk-01"></a>**RISK-01** | Audio block drops during continuous 192k sps capture on the Raspberry Pi 5                                                                                                                                                                             | Medium          | High       | [QAS-01](Requirements/quality-attribute-requirements.md#qas-01--real-time-streaming-throughput), [QAS-03](Requirements/quality-attribute-requirements.md#qas-03--long-run-resource-stability)         | [EXP-01](Experiments/EXP-01-multi-sample-rate-capture-stability.md)    | Technical     |
| <a id="risk-02"></a>**RISK-02** | End-to-end processing latency exceeds the target                                                                                                                                                                                                       | Medium          | High       | [QAS-02](Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency)                 | [EXP-02](Experiments/EXP-02-end-to-end-latency-measurement.md)    | Technical     |
| <a id="risk-03"></a>**RISK-03** | Render performance may not meet requirements as the number of graphs/tabs grows                                                                                                                                                                        | High            | Medium     | [QAS-01](Requirements/quality-attribute-requirements.md#qas-01--real-time-streaming-throughput), [QAS-03](Requirements/quality-attribute-requirements.md#qas-03--long-run-resource-stability), [QAS-08](Requirements/quality-attribute-requirements.md#qas-08-new-tab-extensibility) | [EXP-02](Experiments/EXP-02-end-to-end-latency-measurement.md)    | Technical     |
| <a id="risk-04"></a>**RISK-04** | Insufficient T1/T3 detection accuracy in noisy environments                                                                                                                                                                                            | Medium          | High       | [QAS-05](Requirements/quality-attribute-requirements.md#qas-05--noise-environment-robustness)                 | [EXP-05](Experiments/EXP-05-noise-environment-robustness.md)    | Technical     |
| <a id="risk-05"></a>**RISK-05** | Lack of labeled dataset / time to train a TinyML model                                                                                                                                                                                                 | High            | High       | FR-AI                  | [EXP-11](Experiments/EXP-11-ai-denoising-bypass-manual-labeling.md)    | Technical     |
| <a id="risk-06"></a>**RISK-06** | New display tab causes a large module-change ripple                                                                                                                                                                                                    | Medium          | Medium     | [QAS-08](Requirements/quality-attribute-requirements.md#qas-08--new-tab-extensibility)                 |           | Technical     |
| <a id="risk-07"></a>**RISK-07** | Difficulty understanding the current baseline code structure                                                                                                                                                                                           | Medium          | Medium     | [QAS-09](Requirements/quality-attribute-requirements.md#qas-09--testability)                 | [EXP-09](Experiments/EXP-09-legacy-codebase-comprehension-reverse-engineering.md)    | Non-technical |
| <a id="risk-08"></a>**RISK-08** | Peak load level is not yet specified; wrong assumptions cause abnormal behavior (mentor feedback)                                                                                                                                                      | Medium          | High       | [QAS-02](Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency), [QAS-03](Requirements/quality-attribute-requirements.md#qas-03--long-run-resource-stability)         |           | Technical     |
| <a id="risk-09"></a>**RISK-09** | Cannot do real-time TinyML inference on the Pi                                                                                                                                                                                                         | High            | Low        | FR-AI                  | [EXP-10](Experiments/EXP-10-realtime-tinyml-inference-performance.md)    | Technical     |
| <a id="risk-10"></a>**RISK-10** | No prior team experience with Qt / Qt graphing library (QCustomPlot)                                                                                                                                                                                   | Medium          | Medium     | [QAS-08](Requirements/quality-attribute-requirements.md#qas-08--new-tab-extensibility)                 |           | Non-technical |
| <a id="risk-11"></a>**RISK-11** | No prior team experience with TinyML                                                                                                                                                                                                                   | Medium          | Medium     | FR-AI                  | [EXP-10](Experiments/EXP-10-realtime-tinyml-inference-performance.md)    | Non-technical |
| <a id="risk-12"></a>**RISK-12** | Limited timegrapher domain knowledge                                                                                                                                                                                                                   | Medium          | Medium     | All                    |           | Non-technical |
| <a id="risk-13"></a>**RISK-13** | Within the 5-week schedule, not all tabs + AI can be implemented                                                                                                                                                                                       | High            | Medium     | [QAS-08](Requirements/quality-attribute-requirements.md#qas-08--new-tab-extensibility)                 |           | Non-technical |
| <a id="risk-14"></a>**RISK-14** | Late distribution of the grading rubric (Week 2~3)                                                                                                                                                                                                     | High            | Medium     | —                      |           | Non-technical |
| <a id="risk-15"></a>**RISK-15** | Uncertainty in the automatic watch-position detection feature — it is not yet certain how reliably the current measurement position can be detected and shown automatically.                                                                           | Medium          | High       | FR-WPT                 | [EXP-12](Experiments/EXP-12-usb-protocol-watch-position-detection.md)    | Technical     |
| <a id="risk-16"></a>**RISK-16** | The same codebase may not build and run successfully across the ARM Raspberry Pi 5, x86 PC, and macOS environments.                                                                                                                                    | Medium          | Medium     | [QAS-10](Requirements/quality-attribute-requirements.md#qas-10--pc-pi-platform-separation)                 | [EXP-07](Experiments/EXP-07-cross-platform-build-deployment.md)    | Technical     |
| <a id="risk-17"></a>RISK-17     | Measurement accuracy / detection rate of the T1/T3 detection and Rate/Beat-Error/Amplitude computation algorithms may fall below target (against Sim ground truth)                                                                                     | Medium          | High       |                        | [EXP-04](Experiments/EXP-04-measurement-accuracy-t1-t3-detection-rate.md)    | Technical     |
| <a id="risk-18"></a>RISK-18     | Real-time processing (buffering / threading / resampling) may degrade event-timing precision                                                                                                                                                           | Medium          | High       | [QAS-04](Requirements/quality-attribute-requirements.md#qas-04--measurement-timing-precision-preservation)                 | [EXP-13](Experiments/EXP-13-timing-precision-verification.md)    | Technical     |
| <a id="risk-19"></a>RISK-19     | No reliable signal to detect microphone disconnect, or it is too complex to implement reliably within the deadline                                                                                                                                     | Medium          | High       | [QAS-11](Requirements/quality-attribute-requirements.md#qas-11--microphone-disconnect-user-notification)                 | [EXP-06](Experiments/EXP-06-fault-handling-feedback-verification.md)    | Technical     |
| <a id="risk-20"></a>RISK-20     | Risk that AI misclassifies the position. In Sequence Display mode it can record measured values into the wrong position row (high impact); in other modes it is display-only (low impact). Depends on camera environment (lighting, angle, occlusion). | Medium          | High       | [QAS-06](Requirements/quality-attribute-requirements.md#qas-06--position-detection-safe-fallback)                 | [EXP-18](Experiments/EXP-18-camera-tinyml-9-position-accuracy.md)    | Technical     |

# **7. Experiments**

This section documents the technical experiments (Agile spikes) used to evaluate and validate the quality-attribute requirements. Each experiment follows the standard technical-experiment template. Full details for each experiment are in [Experiments/](Experiments/).

## 7.1 Experiments Summary

Ordered by "Status"

| EXP | Title | Status | Duration |
|-----|-------|--------|----------|
| [EXP-02](Experiments/EXP-02-end-to-end-latency-measurement.md) | Experiment for [[QAS-02](Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency)]: End-to-end latency measurement | In progress | 06/15–06/26 |
| [EXP-07](Experiments/EXP-07-cross-platform-build-deployment.md) | Experiment for [[QAS-10](Requirements/quality-attribute-requirements.md#qas-10--pc-pi-platform-separation)]: Cross-platform build & deployment | Concluded | 05/25–06/08 |
| [EXP-09](Experiments/EXP-09-legacy-codebase-comprehension-reverse-engineering.md) | Legacy Codebase Comprehension & Reverse Engineering via AI | Concluded | 06/03–06/04 |
| [EXP-10](Experiments/EXP-10-realtime-tinyml-inference-performance.md) | Experiment for [FR-AI-1]: Real-time TinyML Inference Performance on Raspberry Pi | Concluded | 06/04–06/08 |
| [EXP-12](Experiments/EXP-12-usb-protocol-watch-position-detection.md) | Experiment for [FR-POS-1]: USB Protocol Analysis for Automated Watch Position Detection | Concluded | 06/03–06/04 |
| [EXP-14](Experiments/EXP-14-microphone-disconnect-behavior.md) | Experiment for [[RISK-19](#risk-19)][[QAS-11](Requirements/quality-attribute-requirements.md#qas-11--microphone-disconnect-user-notification)]: Characterize current behavior on microphone (USB) disconnect | Concluded | 06/15–06/16 |
| [EXP-16](Experiments/EXP-16-fault-detection-notification-tactic-analysis.md) | Experiment for [[RISK-19](#risk-19)][[QAS-07](Requirements/quality-attribute-requirements.md#qas-07--graceful-degradation-and-fault-feedback)][[QAS-11](Requirements/quality-attribute-requirements.md#qas-11--microphone-disconnect-user-notification)]: Fault-Detection & Notification Tactic Analysis (Static-Analysis Based) → [ADR-02](ADRs/ADR-002-Adopt%20a%20watchdog%20(timeout)%20for%20microphone-disconnect%20detection.md) | Concluded | 06/15–06/16 |
| [EXP-17](Experiments/EXP-17-rule-signal-processing-vs-tinyml-boundary.md) | Experiment for [[RISK-05](#risk-05)][[RISK-09](#risk-09)][FR-AI]: Rule/Signal-Processing vs. TinyML Responsibility-Boundary Analysis (Static-Analysis Based) → [ADR-001](ADRs/ADR-001-watch-position-detection-solution.md) | Concluded | 06/15–06/17 |
| [EXP-18](Experiments/EXP-18-camera-tinyml-9-position-accuracy.md) | Camera+TinyML 9-Position Accuracy & Per-Mode Fallback Verification | Concluded | 06/12–06/26 |
| [EXP-19](Experiments/EXP-19-wav-file-synthesis-for-watch-fault-type-learning-and-verification.md) | WAV File Synthesis for Watch Fault-Type Learning and Verification | Concluded | 06/17–06/24 |
| [EXP-01](Experiments/EXP-01-multi-sample-rate-capture-stability.md) | Experiment for [[QAS-01](Requirements/quality-attribute-requirements.md#qas-01--real-time-streaming-throughput)]: Multi-sample-rate capture stability | Canceled | 06/08–06/19 |
| [EXP-03](Experiments/EXP-03-sustained-operation-resource-thermal-stability.md) | Experiment for [[QAS-03](Requirements/quality-attribute-requirements.md#qas-03--long-run-resource-stability)]: 30-minute sustained-operation resource & thermal stability | Canceled | 06/22–06/26 |
| [EXP-04](Experiments/EXP-04-measurement-accuracy-t1-t3-detection-rate.md) | Experiment for [[RISK-17](#risk-17)]: Measurement Accuracy & T1/T3 Detection-Rate Evaluation (vs. Sim Ground Truth) | Canceled | 06/22–06/26 |
| [EXP-05](Experiments/EXP-05-noise-environment-robustness.md) | Experiment for [[QAS-05](Requirements/quality-attribute-requirements.md#qas-05--noise-environment-robustness)]: Noise-environment robustness | Canceled | 06/22–06/26 |
| [EXP-06](Experiments/EXP-06-fault-handling-feedback-verification.md) | Experiment for [[QAS-07](Requirements/quality-attribute-requirements.md#qas-07--graceful-degradation-and-fault-feedback)]: Fault-handling & feedback verification | Canceled | 06/24–07/01 |
| [EXP-08](Experiments/EXP-08-measure-current-performance.md) | Experiment for [[QAS-02](Requirements/quality-attribute-requirements.md#qas-02--end-to-end-latency)]: Measure current performance | Canceled | 06/08–06/12 |
| [EXP-11](Experiments/EXP-11-ai-denoising-bypass-manual-labeling.md) | Experiment for [FR-AI-1]: AI-based Denoising Approach to Bypass Manual Labeling | Canceled | 06/15–06/22 |
| [EXP-13](Experiments/EXP-13-timing-precision-verification.md) | Experiment for [[QAS-04](Requirements/quality-attribute-requirements.md#qas-04--measurement-timing-precision-preservation)]: Timing-Precision Verification (Using Existing E-2 Instrumentation) | Canceled | 06/22–06/26 |
| [EXP-15](Experiments/EXP-15-buffer-memory-management-tactic-analysis.md) | Experiment for [[RISK-01](#risk-01)][[QAS-01](Requirements/quality-attribute-requirements.md#qas-01--real-time-streaming-throughput)][[QAS-03](Requirements/quality-attribute-requirements.md#qas-03--long-run-resource-stability)]: Buffer/Memory-Management Tactic Analysis (Static-Analysis Based) | Canceled | 06/15–06/16 |

# 8. Architecture Overview

- [AV-001: Context Diagram](ArchitectureViews/AV-001-Context-view.md)
- [AV-002: TimeGrapher Module View (Package Diagram)](ArchitectureViews/AV-002-TimeGrapher-module-view.md)
- [AV-003: Live Microphone to Graph Behavior Diagram](ArchitectureViews/AV-003-MicToGraph-SeqenceDiagram.md)
- [AV-004: Position-Detection Runtime View](ArchitectureViews/AV-004-Position_Detection_Runtime_View.md)
- [AV-005: Register Tab Diagram](ArchitectureViews/AV-005-RegiseterTab-Diagram.md)
- [AV-006: Deployment View](ArchitectureViews/AV-006-Deployment-view.md)

# 9. Architecture Decision Records

- [ADR-001: Limit AI (TinyML) to camera-based watch-position detection](ADRs/ADR-001-watch-position-detection-solution.md)
- [ADR-002: Adopt a watchdog (timeout) for microphone-disconnect detection](ADRs/ADR-002-Adopt%20a%20watchdog%20(timeout)%20for%20microphone-disconnect%20detection.md)
- [ADR-003: Register display tabs through a Tab Manager](ADRs/ADR-003-register-display-tabs.md)
- [ADR-004: Share a single WaveLodHistory instance across all display tabs](ADRs/ADR-004-shared-waveform-history-buffer.md)

# 10. References

- Time Grapher Project Plan (Draft).pdf — 34 pages, PLAKOSH·POPOWSKI·BECK 2026

- TimeGrapher Equations_v0.docx.pdf — 11 pages (measurement-formula definitions)

- Witschi Chronoscope X1 G3 Instruction Manual — position convention · display-mode reference

- Witschi Electronic Ltd Training Course: Measuring Technology and Troubleshooting for Watches

- TimeGrapher_v10.4_Student.zip — baseline Qt Creator project

