# EXP-15 / Experiment for [[RISK-01](../06-risk-management.md#risk-01)][QAS-01][QAS-03]: Buffer/Memory-Management Tactic Analysis (Static-Analysis Based) → ADR-03

## Objective

- Without writing or measuring new code, statically analyze the current capture/buffer code and compare known buffer tactics to produce the evidence for a buffer/memory-management design that satisfies both zero data loss and the memory bound — in particular, reason about whether a fixed-size ring buffer is compatible with zero-loss. The final decision, rationale, and consequences are recorded in ADR-03.

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- Documentation of the current buffer structure (sizes, overwrite/drop policy, producer/consumer relation, where loss can occur); a trade-off table of candidate tactics (fixed ring / back-pressure fixed / adaptive-growable) across loss, latency, memory, complexity; analysis of contradictions/risks under the 192k · 30-min · 12-tab workload; the design decision based on this analysis is recorded in ADR-03 (this experiment provides the evidence).

## Resources required

- Access to the existing source (SharedAudio, AudioWorker, MainWindow capture/buffer path); 2–3 person-days of architecture analysis; a code-navigation IDE. No measurement rigs or new implementation (static analysis only).

## Experiment description

- Trace where/how the buffer is filled and drained along the capture-to-consumer path; identify buffer size, overwrite/drop policy, and how producer–consumer speed mismatch is handled.

- Statically trace where data is overwritten or dropped under load spikes or consumer stalls (the conditions that break zero-loss).

- Compare fixed ring / back-pressure / adaptive buffer across loss, latency, memory, complexity, citing known properties.

- Under 192k sps · 30 min · 12-tab cycling, reason about each tactic's memory ceiling and zero-loss guarantee.

- **Hand the analysis to ADR-03** (the decision/rationale is finalized there).

## Duration

06/15-06/16

## Links and references

ADR-003 (buffer/memory-management tactic) · QAS-01 · QAS-03 · [RISK-01](../06-risk-management.md#risk-01) · EXP-03

## Results and recommendations

Current buffer structure (static analysis): a single fixed-size 30-second ring buffer (SECONDS_OF_BUFFER 30; ~22 MB @192k); the producer (AudioWorker) overwrites the oldest samples on wrap without checking the consumer (no back-pressure); the consumer (main thread) reads TotalSamplesWritten − LastTotal with no lap/overwrite guard. There is no loss/drop instrumentation of any kind: the only output on wrap is a qInfo("MasterAudioData Samples Rollover") debug print that fires on every normal wrap, so it does not indicate data loss. If the consumer lags beyond the 30 s margin, the ring is overwritten and the loss is completely silent and unmeasured.

| Tactic | Zero-loss | Memory bound | Live-capture viable | Complexity |
|---|---|---|---|---|
| Fixed ring, overwrite-oldest (current) | Only within the 30 s consumer-lag margin; silent loss if exceeded | Yes (bounded, ~22 MB @192k) | Yes | Low |
| Fixed ring + lap/overwrite detection | Same margin, but loss becomes observable (detected) | Yes | Yes | Low |
| Back-pressure (producer waits when full) | Zero-loss between producer and consumer, but not end-to-end for live capture — the loss moves to the ALSA device (xrun) (general property of real-time capture; not present in the current code) | Yes | No (a live microphone cannot be paused) | Medium |
| Adaptive / growable buffer | Zero-loss until memory is exhausted (hypothetical candidate not in the code; general reasoning) | No — unbounded growth, OOM risk on the Pi | Yes | Medium |
