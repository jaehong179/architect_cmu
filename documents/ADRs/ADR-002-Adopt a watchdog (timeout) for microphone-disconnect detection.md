# ADR-002: Adopt a watchdog (timeout) for microphone-disconnect detection

We use a timeout-based watchdog on audio-block arrival, plus the device error/state callback as a fast-path, to show a "microphone disconnected" notification within 1 s. The circuit breaker was rejected.

#### Decision

- A watchdog tracks the time since the last audio block. If no block arrives within the deadline (≤ 1 s for [QAS-11](../README.md#qas-11-microphone-disconnect-user-notification); this also satisfies [QAS-07](../README.md#qas-07-graceful-degradation-and-fault-feedback) ≤ 2 s), it declares a fault and the GUI shows a notification.
- The device error/state callback (QAudioSource::error() / stateChanged) is wired as a fast-path for when the OS reports the failure directly.
- This reuses the existing watchdog pattern (Bph silence timeout, Timegrapher sync-loss) at the stream level instead of the beat level — a small extension, not a new mechanism.
- No separate heartbeat is added: the incoming audio block is itself the liveness signal, and the watchdog detects its absence.

#### Rationale

Only a watchdog bounds detection latency and detects missing data — exactly what "notify within 1 s" needs ([EXP-16](../Experiments/EXP-16-fault-detection-notification-tactic-analysis.md)). The existing beat-level watchdog proves the pattern works here, so the change is low-risk. The device callback is fast but not reliable alone (some unplugs just go silent), so it is only a fast-path. The circuit breaker was rejected: it stops calling a failing dependency, which is not our problem.

#### Status

Accepted

#### Consequences

#### 

**Positive**

- Detects disconnect within 1 s, satisfying [QAS-11](../README.md#qas-11-microphone-disconnect-user-notification) (and [QAS-07](../README.md#qas-07-graceful-degradation-and-fault-feedback)).

- Reuses a proven pattern → low risk, no new architecture.

- Cleanly separates "microphone unplugged" from "watch present but no beats".

**Negative / costs**

- Small extension needed: track last block time, a deadline timer, and wire the device signal to the GUI.

- The deadline must be tuned to avoid false alarms during brief stalls.
