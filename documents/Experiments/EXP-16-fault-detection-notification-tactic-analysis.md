# EXP-16 / Experiment for [[RISK-19](../06-risk-management.md#risk-19)][QAS-07][QAS-11]: Fault-Detection & Notification Tactic Analysis (Static-Analysis Based) → ADR-02

## Objective

- Without implementing or measuring, statically analyze the available fault signals in the existing code and compare candidate tactics to produce the evidence for choosing a detection/notification tactic that guarantees a user-facing notification within the deadline (QAS-11 ≤ 1 s) on microphone disconnect / signal loss. The final decision, rationale, and consequences are recorded in ADR-02.

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- An inventory of currently available fault signals (callback halt, QAudioSource stateChanged (debug-print only), existing beat-level sync watchdog) with their meaning and latency; a comparison of candidate tactics (timeout/watchdog, device error/state callback, circuit breaker, polling) by what each guarantees; the tactic decision based on this analysis is recorded in ADR-02 (this experiment provides the evidence).

## Resources required

- Access to the existing source (AudioWorker, Bph, Timegrapher); 1–2 person-days; an IDE. No measurement rigs or new implementation. Uses EXP-14's observations as input.

## Experiment description

- Signal identification: analyze what the code can receive on disconnect — readyRead halt (callback stops), QAudioSource::stateChanged (Idle/Stopped) — currently connected but debug-print only; note that QAudioSource::error() is available via the API but is not polled in the current code.

- Existing-pattern analysis: examine how the existing watchdogs (Bph silence timeout, Timegrapher sync-loss) bound detection of "absence of expected events" at the beat level.

- Tactic comparison: compare tactics by whether each bounds detection latency within the deadline (≤ 1 s).

- Circuit-breaker applicability: reason that its purpose (trip after repeated call failures) differs from "notify within 1 s."

- Hand the analysis to ADR-02 (the decision/rationale is finalized there).

## Duration

06/15-06/16

## Links and references

ADR-02 (fault-detection & notification tactic); [RISK-19](../06-risk-management.md#risk-19); QAS-07, QAS-11; EXP-14 (current behavior) as input.

## Results and recommendations

readyRead halt (callback stops) = primary signal / QAudioSource::stateChanged = connected but handler does a qDebug() print only (no fault handling, no GUI notification); QAudioSource::error() is not polled anywhere; there is no capture-gap/drop instrumentation. The existing watchdog covers beat-level only (no beats in signal); it does not detect stream-level absence (microphone disconnect).

| Tactic | Bounds detection latency? | Detects "no data" (mic unplug)? | Fit for "notify ≤ 1 s" | In current code? |
|---|---|---|---|---|
| Timeout / watchdog (no expected block within deadline → fault) | Yes (= the deadline) | Yes (callback stops → deadline trips) | Strong | Yes (Bph/Timegrapher sync watchdog) |
| Device error/state callback (error()/stateChanged) | No (only when OS reports) | Sometimes (some unplugs only go silent) | Fast-path, but not guaranteed alone | stateChanged connected but debug-print only; error() not polled |
| Circuit breaker (trip after N call failures) | No | No (addresses repeated call failures, not absence of data) | Poor / wrong tool | No |
| Polling / heartbeat (periodic health check) | Yes (= poll interval) | Yes | OK but coarse/redundant vs watchdog | No |
