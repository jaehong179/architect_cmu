# ADR-002: Adopt a watchdog (timeout) for microphone-disconnect detection

We decided to adopt a timeout-based watchdog on audio-block arrival, combined with the device error/state callback as a fast-path, to guarantee a user-facing "microphone disconnected" notification within 1 s, and to reject the circuit breaker for this requirement.

#### Decision

We detect microphone disconnect / signal loss at the **stream level** and surface it to the user.

- A **watchdog** tracks the time since the last received audio block; if no block arrives within a bounded deadline (set to the stricter [QAS-11](../04-quality-attribute-requirements.md#qas-11-microphone-disconnect-user-notification) ≤ 1 s; meeting ≤ 1 s also satisfies [QAS-07](../04-quality-attribute-requirements.md#qas-07-graceful-degradation-and-fault-feedback) ≤ 2 s), it declares a "microphone disconnected / signal lost" fault and the GUI shows a dedicated notification.

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

- Bounds disconnect detection within 1 s, satisfying [QAS-11](../04-quality-attribute-requirements.md#qas-11-microphone-disconnect-user-notification) (and [QAS-07](../04-quality-attribute-requirements.md#qas-07-graceful-degradation-and-fault-feedback)'s 2 s automatically).

- Reuses a proven in-code pattern → low implementation risk, no new architecture.

- Cleanly separates "microphone unplugged" (no blocks arriving) from "watch present but no beats" (existing beat-level watchdog).

**Negative / costs**

- Requires a small extension: track last audio-block time, a deadline timer, and wiring the device error/state signal to a GUI notification.

- The deadline value must be tuned to avoid false positives during legitimate brief stalls.
