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

- [RISK-01](06-risk-management.md#risk-01) (Audio block drops during continuous 192k sps capture on Pi 5)

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

- [RISK-02](06-risk-management.md#risk-02) (End-to-end processing latency)

#### Experiment

- [EXP-02](07-experiments.md#exp-02-experiment-for-qas-02-end-to-end-latency-measurement) (End-to-end latency measurement)

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

- [RISK-01](06-risk-management.md#risk-01) (Audio block drops), [RISK-02](06-risk-management.md#risk-02) (CPU heat, memory leaks due to continuous rendering)

#### Experiment

- [EXP-03](07-experiments.md#exp-03-experiment-for-qas-03-30-minute-sustained-operation-resource-thermal-stability) (30-minute sustained-operation resource)

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

- [RISK-18](06-risk-management.md#risk-18) (Real-time processing degrades event-timing precision)

#### Experiment

- [EXP-13](07-experiments.md#exp-13-experiment-for-qas-04-timing-precision-verification-using-existing-e-2-instrumentation) (Timing-precision verification using existing E-2 instrumentation)

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

- [RISK-04](06-risk-management.md#risk-04) (Failure to detect watch beats under typical workshop noise)

#### Experiment

- [EXP-05](07-experiments.md#exp-05-experiment-for-qas-05-noise-environment-robustness) (Noise-environment robustness)

#### <a id="qas-06"></a>QAS- 06/ Position-Detection Safe Fallback

####  *Usability/Dependability -\> Reliability*

#### In Live mode, when the AI position-classification confidence falls below the threshold or the camera becomes unavailable, the system avoids committing an uncertain position. In Multi-Position Sequence Display mode it withholds auto-recording, marks the position as undetermined, and prompts the user to select it manually; in other display modes it simply hides the current position. As a result, no measurement is ever recorded into a wrong position row, and no incorrect position is displayed.

#### 

| **Type**               | **Description**                                                                                                                                                                                                                                                                                                   |
|------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Stimulus**           | Camera input and the AI (TinyML) position-classification inference                                                                                                                                                                                                                                                |
| **Source of stimulus** | AI position-classification confidence below threshold, or camera unavailable (disconnected / occluded)                                                                                                                                                                                                            |
| **Artifact**           | Position-detection module, measurement-to-position mapping, GUI (per-mode display)                                                                                                                                                                                                                                |
| **Environment**        | Live (measurement) mode, Raspberry Pi 5 + external camera                                                                                                                                                                                                                                                         |
| **Response**           | Sequence Display: withhold auto-record → mark "undetermined" → prompt manual selection. Other modes: do not display the position                                                                                                                                                                                  |
| **Response measure**   | In Sequence Display, among items auto-recorded at confidence ≥ threshold, mis-records from wrong-position classification = 0; under low confidence/unavailability switch to manual prompt (zero wrong-row updates). Other modes: position not displayed under low confidence/unavailability (zero wrong displays) |

#### 

#### Priority

Importance: High Difficulty: Medium

#### Risk

- Risk that AI misclassifies the position. In Sequence Display mode it can record measured values into the wrong position row (high impact); in other modes it is display-only (low impact). Depends on camera environment (lighting, angle, occlusion).

#### Experiment

- [RISK-20](06-risk-management.md#risk-20) · [EXP-18](07-experiments.md#exp-18-cameratinyml-9-position-accuracy-per-mode-fallback-verification) · FR-POS · ADR-01

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
<td><p>(1) Time Delay ≤ 2 s (based on <a href="07-experiments.md#exp-06-experiment-for-qas-07-fault-handling-feedback-verification">EXP-06</a>)</p>
<p>(2) Output Accuracy: Invalid output count = 0 (Preserve last valid value)</p></td>
</tr>
</tbody>
</table>

#### Priority

Importance: High Difficulty: Medium

#### Risk

- UI freeze or missing fault feedback on signal loss

#### Experiment

- [EXP-06](07-experiments.md#exp-06-experiment-for-qas-07-fault-handling-feedback-verification) (Usability·feedback)

<a id="qas-08"></a>QAS-08 / New Tab Extensibility

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

Importance: Medium Difficulty: Medium

#### Risk

- [RISK-06 (Code module change explosion when inserting new tabs)](06-risk-management.md#risk-06)

#### Experiment

- N/A

### QAS-09 / Testability

#### Modifiability → Testability

When the CI pipeline runs the automated verification suite at build time on the CI server, the unit tests exercise the core calculation logic and return reproducible results, achieving ≥ 70% coverage with 100% deterministic outcomes.

| **Type**               | **Description**                                                               |
|------------------------|-------------------------------------------------------------------------------|
| **Stimulus**           | CI pipeline runs automated verification suite                                 |
| **Source of stimulus** | CI pipeline                                                                   |
| **Artifact**           | Unit tests                                                                    |
| **Environment**        | Development/Build time (CI server execution)                                  |
| **Response**           | Automated tests cover core calculation logic, returning reproducible outcomes |
| **Response measure**   | Unit test coverage ≥ 70%; 100% deterministic                                  |

#### Priority

Importance: Medium Difficulty: Medium

#### Risk

- N/A

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

- [RISK-16](06-risk-management.md#risk-16) (Platform compatibility risk)

#### Experiment

- [EXP-07](07-experiments.md#exp-07-experiment-for-qas-10-cross-platform-build-deployment) (Cross-platform build·deployment)

#### QAS-11 / Microphone-Disconnect User Notification

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

- [RISK-19](06-risk-management.md#risk-19) (No reliable signal to detect microphone disconnect, or too complex to implement within the deadline)

#### Experiment

- [EXP-07](07-experiments.md#exp-07-experiment-for-qas-10-cross-platform-build-deployment) (Cross-platform build·deployment)
