# 9. ADRS

**ADR-01: Limit AI (TinyML) to camera-based watch-position reading**

We decided to keep the core acoustic measurement path rule/signal-processing based and to apply TinyML only to external-camera-based watch measurement-position (orientation) reading, in order to preserve measurement trustworthiness and explainability while still adding AI value where deterministic methods fall short.

***Decision***

We draw the responsibility boundary outside the acoustic measurement path.

The core acoustic path — tick/tock (T1/T3) detection, rate/beat-error/amplitude computation, health grading, and fault hints — stays rule/signal-processing based (deterministic and explainable).

The low-SNR detection weakness is addressed with deterministic signal processing (adaptive threshold, matched filter), not ML.

Signal anomaly detection is handled with rules/signal-processing (no ML).

TinyML is applied only to camera-based watch position (orientation) reading, using an external camera, among the 9 standard positions (CR, CU(R), CU, CU(L), CL, CD(L), CD, CD(R), DU/DD). This reading is used (a) in the Multi-Position Sequence Display mode to record the deterministically-measured Rate/Beat-Error/Amplitude into the matching position row, and (b) in other display modes only to show the current position label (e.g., 9H).

In neither case does the classifier compute the measurement itself — it only labels position — so a misclassification cannot corrupt measured values. AI auto-detection is the default; in Sequence Display the fallback is manual selection, and in other modes the position is simply not shown when confidence is low.

***Rationale***

We analyzed each diagnosis sub-task (EXP-17) for whether it affects measurement trust, is adequately served by a deterministic algorithm, and whether ML training data can realistically be obtained.  
The acoustic measurement/interpretation tasks are the root of every metric and must remain deterministic, explainable, and verifiable, so ML was excluded there; low-SNR is properly solved by better deterministic signal processing rather than ML.  
Measurement orientation, however, cannot be reliably obtained from the acoustic signal alone (EXP-12 concluded a manual fallback due to absent USB position data), and visual orientation is hard to express as rules. In contrast, external-camera images are realistically labelable per orientation, which makes ML training data feasible and avoids the labeled-dataset shortage ([RISK-05](06-risk-management.md#risk-05)). A lightweight image classifier is also small enough for on-Pi inference ([RISK-09](06-risk-management.md#risk-09)). This makes camera-based position reading the one place where TinyML provides value that deterministic methods cannot easily deliver, without putting AI on the trust-critical measurement path.

***Status***

Accepted

***Consequences***

Positive

- Preserves measurement trustworthiness, explainability, and verifiability by keeping the core path deterministic.

- Avoids the labeled-data shortage ([RISK-05](06-risk-management.md#risk-05)) by choosing a domain (camera images) that is easy to label.

- Provides orientation reading that acoustic methods cannot, and keeps AI isolated so a misprediction cannot corrupt measurement.

- Keeps inference lightweight enough for the Raspberry Pi ([RISK-09](06-risk-management.md#risk-09)).

Negative / costs

- Adds external-camera hardware and the associated setup.

- Requires building a per-orientation image dataset and verifying position-reading accuracy separately (follow-up EXP-18).

- Introduces new camera-environment dependencies (lighting, angle, framing) that must be assessed as a new risk ([RISK-20](06-risk-management.md#risk-20)).

**ADR-02: Adopt a watchdog (timeout) for microphone-disconnect detection**

We decided to adopt a timeout-based watchdog on audio-block arrival, combined with the device error/state callback as a fast-path, to guarantee a user-facing "microphone disconnected" notification within 1 s, and to reject the circuit breaker for this requirement.

#### Decision

We detect microphone disconnect / signal loss at the **stream level** and surface it to the user.

- A **watchdog** tracks the time since the last received audio block; if no block arrives within a bounded deadline (set to the stricter QAS-11 ≤ 1 s; meeting ≤ 1 s also satisfies QAS-07 ≤ 2 s), it declares a "microphone disconnected / signal lost" fault and the GUI shows a dedicated notification.

- The **device error/state callback** (QAudioSource::error() / stateChanged) is wired as an immediate **fast-path** for cases where the OS reports the failure explicitly (currently these are only Perf::log-ged, not surfaced).

- This **reuses the watchdog pattern already proven in the code** (Bph silence timeout, Timegrapher sync-loss) but applies it at the **stream level** (audio-block arrival) instead of the beat level — a small extension, not a new mechanism.

- We **do not** add a separate heartbeat: the incoming audio block itself serves as the liveness signal, and the watchdog only detects its absence.

#### Rationale

We analyzed the fault signals available on disconnect and compared candidate tactics (EXP-16). Only a timeout/watchdog **bounds detection latency** and detects the **absence of incoming data**, which is exactly what "notify within 1 s" requires; the existing beat-level watchdog proves the pattern works in this codebase, so the change is low-risk. The device error/state callback is fast when the OS reports an error, but is **not guaranteed alone** (some unplugs simply go silent), so it is used only as a fast-path. The **circuit breaker was rejected**: its purpose is to stop repeatedly calling a failing dependency, not to bound detection latency or detect absence of data, and the codebase has no repeated-failing-call pattern here. Polling/heartbeat would work but is coarser and redundant given the watchdog.

#### Status

Accepted

#### Consequences

#### 

**Positive**

- Bounds disconnect detection within 1 s, satisfying QAS-11 (and QAS-07's 2 s automatically).

- Reuses a proven in-code pattern → low implementation risk, no new architecture.

- Cleanly separates "microphone unplugged" (no blocks arriving) from "watch present but no beats" (existing beat-level watchdog).

**Negative / costs**

- Requires a small extension: track last audio-block time, a deadline timer, and wiring the device error/state signal to a GUI notification.

- The deadline value must be tuned to avoid false positives during legitimate brief stalls.

**ADR-03: Retain the bounded fixed ring buffer and add overwrite detection**

We decided to keep the bounded fixed-size ring buffer and add lap/overwrite detection, and to treat zero-loss as a consumer-throughput guarantee rather than a buffer guarantee, in order to satisfy both the memory bound and observable data integrity for live 192k capture.

***Decision***

- We keep the existing capture buffer bounded and make any loss observable.

- Retain the fixed-size ring buffer (bounded, ~22 MB @192k) as the capture buffer; do not switch to back-pressure or an adaptive buffer.

- Add lap/overwrite detection: compare the producer WriteIndex against the consumer's unread distance; if the unread distance reaches buffer capacity, count a true loss event and surface it as a fault (feeds the QAS-07 fault-feedback path).

- Treat zero-loss as a consumer-throughput guarantee, not a buffer guarantee: pair the buffer with a render/throughput budget so the consumer keeps up within the 30 s margin (ties to QAS-01 and EXP-02).

***Rationale***

We statically analyzed the capture/buffer code and compared candidate tactics (EXP-15). The memory bound (QAS-03) is met only by a bounded buffer, so the adaptive/growable buffer is rejected (unbounded growth, OOM risk on the Pi). A live microphone cannot be paused, so back-pressure is rejected — it does not give end-to-end zero-loss for live capture; it merely relocates the loss to the ALSA device (xrun). The remaining option is the fixed ring buffer, whose only real weakness is that loss is silent beyond the 30 s consumer-lag margin and is not flagged as a fault. Adding overwrite detection removes that weakness at low cost, turning silent loss into a detected fault. Therefore "zero-loss" is correctly framed as a consumer-throughput requirement, with the buffer guaranteeing bounded memory and observability.

***Status***

Accepted

***Consequences***

Positive

- Satisfies the memory bound (QAS-03) with a constant ~22 MB footprint, no OOM/thermal risk.

- Converts silent data loss into a detected, observable fault (supports QAS-07).

- Low implementation cost (a comparison + counter); no architectural change to capture.

- Keeps the live-capture path intact (no producer blocking).

Negative / costs

- Zero-loss is guaranteed only within the consumer-lag margin; it depends on the consumer keeping up, so the render/throughput budget (QAS-01 / EXP-02) must hold.

- Requires a small extension (lap-distance check + loss counter wired to the fault path).

- The decision should later be confirmed empirically under load by EXP-03.
