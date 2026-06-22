# ADR-004: Capture recent history in one central buffer, separate from the per-display live buffers

We keep one central 8-minute history buffer, fed in parallel with live rendering, so that paused scroll-back and cross-display seek work without growing memory per display or slowing the live path. Per-display history was rejected.

***Decision***

* The display broadcast delivers each wave block to all visual displays and, in parallel, to one central history buffer registered as a non-visual subscriber. The central buffer is not on the live render path.
* The buffer retains ~8 minutes of signal at full resolution, with a level-of-detail index applied only at draw time.
* 8 minutes is derived from the measurement workflow: hairspring gravity stabilization (15 s) + escapement wheel full revolution (60 s) = 75 s per position × 6 standard positions = 450 s (7.5 min), rounded up to 480 s.
* On pause, scroll-back/seek reads from the central buffer and each display draws it through its normal render path (replay), sharing one absolute time coordinate.
* Adding the history is one new subscriber registration; core processing and existing displays are not modified.

***Rationale***

* Per-display history would multiply memory by display count ([RISK-03](../README.md#risk-03)) and violate the long-run resource bound ([QAS-03](../README.md#qas-03--long-run-resource-stability)).
* Routing live data through the central store would add hot-path work, threatening latency ([QAS-02](../README.md#qas-02--end-to-end-latency)) and timing precision ([RISK-18](../README.md#risk-18)). Keeping the live path direct and appending history alongside preserves throughput ([QAS-01](../README.md#qas-01--real-time-streaming-throughput)).
* The non-visual-subscriber approach confines the change to one integration point — same open/closed property as [QAS-08](../README.md#qas-08--new-tab-extensibility) and a continuation of ADR-003.

***Status***

Accepted

***Consequences***

Positive

- Memory bounded by one buffer regardless of display count — satisfies [QAS-03](../README.md#qas-03--long-run-resource-stability), mitigates [RISK-03](../README.md#risk-03).
- 8-minute window covers a full 6-position cycle; no data loss on scroll-back.
- Live path unchanged — protects latency/throughput ([QAS-02](../README.md#qas-02--end-to-end-latency), [QAS-01](../README.md#qas-01--real-time-streaming-throughput)), avoids timing risk ([RISK-18](../README.md#risk-18)).
- Single integration point, no changes to core or existing displays ([QAS-08](../README.md#qas-08--new-tab-extensibility); builds on ADR-003).

Negative / costs

- Small duplication: the most recent short window exists in both the display's working buffer and the central buffer.
- Two data paths (live vs replay) add conceptual complexity.
- Full-resolution retention keeps a large resident footprint; long-run thermal/resource effect must be verified against [QAS-03](../README.md#qas-03--long-run-resource-stability).
