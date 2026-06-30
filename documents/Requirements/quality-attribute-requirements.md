# **4. Quality Attribute Requirements (Scenarios)**
This section identifies and specifies concrete quality attribute scenarios developed to satisfy the system's non-functional requirements.

## **4.1 Quality Attribute Scenarios**

Based on the requirements and the project goals, we identified and prioritized the quality-attribute scenarios below. Each scenario is specified using the standard six-part scenario format.

### QAS-01 / Real-Time Streaming Throughput

#### Performance → Throughput

While a continuous 192,000 sps audio stream from the microphone front-end is being captured and the GUI is simultaneously switching tabs, the audio-capture buffer and DSP pipeline capture every audio block without loss, with zero dropped blocks over any 60-second window.

| **Type**               | **Description**                                            |
|------------------------|------------------------------------------------------------|
| **Stimulus**           | Continuous audio stream at 192,000 sps                     |
| **Source of stimulus** | Audio capture front-end (microphone)                       |
| **Artifact**           | Audio capture buffer → DSP pipeline                        |
| **Environment**        | 192k sps active + GUI tab-switching running simultaneously |
| **Response**           | Every audio block captured without loss                    |
| **Response measure**   | Drops = 0 / 60 s                                           |

#### Priority

Importance: High Difficulty: High

#### Risk

- [RISK-01](../README.md#risk-01) (Audio block drops during continuous 192k sps capture on Pi 5)

#### Experiment

- N/A

### QAS-02 / End-to-End Latency

#### Performance → Latency

When a single acoustic impulse (a tic/tac event) arrives from the timegrapher microphone, with the full pipeline (acquisition → signal processing → presentation) running on the Raspberry Pi 5 at 192,000 sps and 36,000 BPH under peak CPU load, the pipeline updates the corresponding measurement value on the GUI within 100 ms even in the worst case.

| **Type**               | **Description**                                                                       |
|------------------------|---------------------------------------------------------------------------------------|
| **Stimulus**           | A single acoustic impulse occurring within one tic cycle at 36000 BPH (100 ms period) |
| **Source of stimulus** | Timegrapher microphone                                                                |
| **Artifact**           | Full pipeline: Acquisition → Signal processing → Presentation                         |
| **Environment**        | Raspberry Pi 5, 192000 sps, 36000 BPH operating condition, under peak CPU load        |
| **Response**           | Measurement value updated on GUI                                                      |
| **Response measure**   | \< 100 ms                                                                             |

#### Priority

Importance: High Difficulty: Medium

#### Risk

- [RISK-02](../README.md#risk-02) (End-to-end processing latency)

#### Experiment

- [EXP-02](../Experiments/EXP-02-end-to-end-latency-measurement.md) (End-to-end latency measurement)

### QAS-03 / Long-Run Resource Stability

#### Dependability → Availability

While the system runs on the Raspberry Pi with the 12 GUI tabs cycling at 30-second intervals, when a sustained high-rate 192,000 sps audio stream **from the microphone front-end** drives the processing load, the full pipeline (acquisition → signal processing → presentation) maintains a stable memory footprint with no OOM or CPU throttling, keeping the memory increase ≤ 200 MB over 30 minutes..

| **Type**               | **Description**                                                                                                      |
|------------------------|----------------------------------------------------------------------------------------------------------------------|
| **Stimulus**           | Input and processing load of high-rate 192k sps audio data                                                           |
| **Source of stimulus** | Audio capture front-end (microphone)                                                                                 |
| **Artifact**           | Full pipeline: Acquisition → Signal processing → Presentation                                                        |
| **Environment**        | Normal operating state while running on a Raspberry Pi, with 12 GUI tabs continuously cycling at 30-second intervals |
| **Response**           | Stable memory footprint; no OOM or CPU throttling                                                                    |
| **Response measure**   | Memory increase ≤ 200 MB over 30 minutes                                                                             |

#### Priority

Importance: High Difficulty: Medium

#### Risk

- [RISK-01](../README.md#risk-01) (Audio block drops), [RISK-02](../README.md#risk-02) (CPU heat, memory leaks due to continuous rendering)

#### Experiment

- [EXP-03](../Experiments/EXP-03-sustained-operation-resource-thermal-stability.md) (30-minute sustained-operation resource)

### QAS-04 / Measurement Timing-Precision Preservation

#### Performance / Dependability → Timing Precision

When the existing Sim Mode generates a signal whose true beat indices are known and that signal is processed through the real-time pipeline (acquisition → filtering → event detection → measurement), block buffering, worker threads, and resampling preserve event-timing precision so that the detection-timing error against the Sim ground truth is worst-case ≤ 1 sample (≈ ±5 µs at 192,000 sps) and stays within bound as CPU load, sample rate, and signal realism change.

| **Type**               | **Description**                                                                                                                                     |
|------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------|
| **Stimulus**           | A Sim-generated signal of 1,000 watch beats whose true sample positions (ground-truth indices) are logged                                           |
| **Source of stimulus** | Existing Sim Mode signal generator that records each beat's ground-truth sample position (GtBeats)                                                  |
| **Artifact**           | Real-time processing pipeline: Acquisition → Filtering → Event detection → Measurement                                                              |
| **Environment**        | Raspberry Pi 5, 192,000 sps, block buffering and worker threads active, under normal operation                                                      |
| **Response**           | Buffering, threading, and resampling preserve event-timestamp precision end-to-end without distortion                                               |
| **Response measure**   | Detection-timing error vs. Sim ground truth, worst-case ≤ 1 sample (≈ ±5 µs @192k); stays within bound across CPU-load / sps / Realistic conditions |

#### Priority

Importance: High Difficulty: High

#### Risk

- [RISK-18](../README.md#risk-18) (Real-time processing degrades event-timing precision)

#### Experiment

- [EXP-13](../Experiments/EXP-13-timing-precision-verification.md) (Timing-precision verification using existing E-2 instrumentation)

### QAS-05 / Noise-Environment Robustness

#### Dependability → Reliability (Robustness)

While the watch is measured in Live Mode at 96,000 sps and 60 dB SPL (Sound Pressure Level) speech noise reaches the microphone alongside the watch signal, the DSP filter chain and T1/T3 detection still produce valid measurements, with the error increase held to ≤ 2× the noise-free baseline.

| **Type**               | **Description**                                        |
|------------------------|--------------------------------------------------------|
| **Stimulus**           | 60 dB SPL speech noise injected alongside watch signal |
| **Source of stimulus** | Microphone under workshop noise (60 dB SPL speech)     |
| **Artifact**           | DSP filter chain + T1/T3 detection                     |
| **Environment**        | Live Mode, 96k sps, ambient noise                      |
| **Response**           | Valid measurements despite degraded SNR                |
| **Response measure**   | error increase ≤ 2× noise-free baseline                |

#### Priority

Importance: Medium Difficulty: High

#### Risk

- [RISK-04](../README.md#risk-04) (Failure to detect watch beats under typical workshop noise)

#### Experiment

- [EXP-05](../Experiments/EXP-05-noise-environment-robustness.md) (Noise-environment robustness)

### QAS-06 / Watch-Position Auto-Detection Accuracy

#### Usability/Dependability -\> Reliability (Accuracy)

When the watch's physical measurement position is changed, the system shall automatically classify and display the correct standard position without false positives.

####

| **Type**               | **Description** |
|------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Stimulus**           | Physical change of watch measurement position (e.g., CH → 6H) |
| **Source of stimulus** | User repositioning the watch on the timegrapher microphone |
| **Artifact**           | AI Position Classification Module → GUI Layout |
| **Environment**        | Live Mode |
| **Response**           | System automatically classifies the correct standard position and updates the display |
| **Response measure**   | Classification Accuracy > 95% over test cycles; GUI position indicator updates within 2.0 seconds after stabilization; False Positive Rate < 3% |

#### 

#### Priority
Importance: High Difficulty: High

#### Risk
- [RISK-15](../README.md#risk-15)

#### Experiment
- [EXP-12](../Experiments/EXP-12-usb-protocol-watch-position-detection.md)
- [EXP-18](../Experiments/EXP-18-camera-tinyml-9-position-accuracy.md)

### QAS-07 / Graceful Degradation and Fault Feedback

#### Dependability → Safety + Usability

While the system runs in Live Mode under normal capture load on the Raspberry Pi 5, when a measurement-continuity break or processing anomaly occurs at the audio input or signal-processing stage (microphone disconnect, stream loss, missed beats, or out-of-range amplitude), the Signal Validation Gate and GUI halt invalid output, preserve the last valid value, and show an error/status alert within 2 s, with zero invalid outputs displayed. (The user-facing notification timing for microphone disconnect is specified separately in [QAS-11](#qas-11-microphone-disconnect-user-notification).)

<table>
<colgroup>
<col style="width: 30%" />
<col style="width: 69%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>Type</strong></th>
<th><strong>Description</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><strong>Stimulus</strong></td>
<td>Measurement continuity break / Data processing anomaly (Stream loss, missed beats, out-of-range amplitude, microphone disconnect)</td>
</tr>
<tr class="even">
<td><strong>Source of stimulus</strong></td>
<td>Audio input or Signal-processing stage</td>
</tr>
<tr class="odd">
<td><strong>Artifact</strong></td>
<td>Signal Validation Gate and GUI</td>
</tr>
<tr class="even">
<td><strong>Environment</strong></td>
<td>Live Mode under normal capture load on the Raspberry Pi 5</td>
</tr>
<tr class="odd">
<td><strong>Response</strong></td>
<td>Halts invalid output, preserves the last valid value, and provides error status/feedback</td>
</tr>
<tr class="even">
<td><strong>Response measure</strong></td>
<td><p>(1) Time Delay ≤ 2 s (based on <a href="../Experiments/EXP-06-fault-handling-feedback-verification.md">EXP-06</a>)</p>
<p>(2) Output Accuracy: Invalid output count = 0 (Preserve last valid value)</p></td>
</tr>
</tbody>
</table>

#### Priority

Importance: High Difficulty: Medium

#### Risk

- UI freeze or missing fault feedback on signal loss

#### Experiment

- [EXP-06](../Experiments/EXP-06-fault-handling-feedback-verification.md) (Usability·feedback)

### QAS-08 / New Tab Extensibility

#### Modifiability → Extensibility

When a developer adds a new watch-visualization tab to the Qt project at development time, the new tab integrates into the GUI Layout and Tab Manager without any change to the core DSP modules or existing tab source files, producing zero regressions.

| **Type**               | **Description**                                                                             |
|------------------------|---------------------------------------------------------------------------------------------|
| **Stimulus**           | Developer adds a new watch visualization tab                                                |
| **Source of stimulus** | Developer adding a new visualization tab                                                    |
| **Artifact**           | GUI Layout + Tab Manager                                                                    |
| **Environment**        | Development time (adding code to Qt Project)                                                |
| **Response**           | Developer integrates tab without editing core DSP or other tab codes                        |
| **Response measure**   | No modifications required to core DSP modules or existing tab source files; regressions = 0 |

#### Priority

Importance: High Difficulty: Medium

#### Risk

- [RISK-06 (Code module change explosion when inserting new tabs)](../README.md#risk-06)

#### Experiment

- N/A


### QAS-10 / PC ↔ Pi Platform Separation

#### Portability → Platform Separation

When a developer ports the application between platforms (Raspberry Pi 5 ↔ x86 PC) or adds a new platform backend at build time, the change is confined to the platform-audio backend layer while the measurement/detection core compiles unchanged; both builds pass from the same core source, and for the same input the two platforms' results agree within Rate ≤ 0.5 s/d and Beat Error ≤ 0.05 ms.

| **Type**               | **Description**                                                                                                                                                                                                                                                                        |
|------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Stimulus**           | Port the application to a different platform (Raspberry Pi 5 ↔ x86 PC) or add a new platform backend                                                                                                                                                                                   |
| **Source of stimulus** | Developer                                                                                                                                                                                                                                                                              |
| **Artifact**           | Codebase — measurement/detection core and platform-audio abstraction layer                                                                                                                                                                                                             |
| **Environment**        | Development / Build time                                                                                                                                                                                                                                                               |
| **Response**           | Changes are confined to the platform-audio backend layer; the measurement/detection core compiles unchanged and behaves identically on both platforms                                                                                                                                  |
| **Response measure**   | \(1\) On porting/platform swap, changes occur only in the platform-audio backend layer and the measurement/detection core is unchanged. (2) From the same core source both builds pass, and for the same input the two platforms agree within Rate ≤ 0.5 s/d and Beat Error ≤ 0.05 ms. |

#### Priority

Importance: Medium Difficulty: Medium

#### Risk

- [RISK-16](../README.md#risk-16) (Platform compatibility risk)

#### Experiment

- [EXP-07](../Experiments/EXP-07-cross-platform-build-deployment.md) (Cross-platform build·deployment)

### QAS-11 / Microphone-Disconnect User Notification

*Usability*

While the system is running with the GUI in Live mode, when the Raspberry Pi loses its connection to the measurement microphone (USB audio input), the GUI shall display a dedicated "microphone disconnected" notification within 1 s. (Complements [QAS-07](#qas-07-graceful-degradation-and-fault-feedback), which covers the safety-side handling of the same fault.)

| **Type**               | **Description**                                                                  |
|------------------------|----------------------------------------------------------------------------------|
| **Stimulus**           | The measurement microphone (USB audio input) is disconnected from the Pi         |
| **Source of stimulus** | Audio capture layer — microphone/USB connection loss                             |
| **Artifact**           | GUI (status/notification area)                                                   |
| **Environment**        | System running, GUI in Live mode on Raspberry Pi 5                               |
| **Response**           | A dedicated "microphone disconnected" notification is shown on the GUI           |
| **Response measure**   | The disconnect notification appears on the GUI within 1 s of the connection loss |

#### Priority

Importance: High Difficulty: Medium

#### Risk

- [RISK-19](../README.md#risk-19) (No reliable signal to detect microphone disconnect, or too complex to implement within the deadline)

#### Experiment

- [EXP-07](../Experiments/EXP-07-cross-platform-build-deployment.md) (Cross-platform build·deployment)

### QAS-12 / Measurement History Aggregation & Overhaul-Timing Provision

**Usability → Aggregate**

When a watch holder scans the watch's QR code, the system aggregates the watch's multiple measurement records into a single trend and presents an overhaul-timing assessment derived from that trend. Beyond the scan, the user does not manually collect or compare individual measurements. (See ADR-005 for the architectural decision that supports this aggregation.)

| Type | Description |
|------|-------------|
| Stimulus | A watch holder scans the QR code to request the watch's condition |
| Source of stimulus | Watch holder (repair engineer or watch owner) |
| Artifact | History aggregation/storage (DynamoDB), aggregation/judgment logic (Lambda), delivery path (API → web) |
| Environment | Normal operation; the watch has accumulated history from one or more prior measurements |
| Response | The system aggregates multiple measurements into a trend and presents a trend-based overhaul-timing assessment with its supporting evidence |
| Response measure | User performs 0 manual data-collection actions; the overhaul assessment is derived from the aggregated trend, not a single value |

## Priority

Importance: High  Difficulty: Medium

## Risk

N/A

## Experiment

N/A
