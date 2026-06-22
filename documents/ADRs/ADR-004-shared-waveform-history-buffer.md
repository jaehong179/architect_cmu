# ADR-004: Capture recent history in one central buffer, separate from the per-display live buffers

We will capture the recent signal history in a single central buffer that the display pipeline feeds in parallel with live rendering — rather than letting each display keep its own long history — so that paused scroll-back and cross-display seek become possible without growing memory per display or slowing the live path.

***Decision***

We separate the live render path from a single central history store.

The display pipeline broadcasts each processed wave block to the visual displays and, in parallel, to one central history buffer that is attached as a non-visual subscriber of the same broadcast. Live rendering keeps using each display's own short working buffer, fed directly by the broadcast; the central history is not on the live render path. The central buffer retains the recent window (target ≈ 8 minutes) of signal at full resolution, with a draw-time multi-resolution (level-of-detail) index used only for plotting. On pause the source is frozen; scroll-back/seek reads the requested span from the central buffer and the display draws it through its normal render path (replay), using one absolute time coordinate shared across displays. Adding the history means registering one new non-visual subscriber; core processing and existing displays are not modified.

The alternative is to give each display its own multi-minute history buffer. Under that design every display would independently accumulate envelope, raw PCM, level-of-detail data, and event records.

***Rationale***

**Why 8 minutes.** The buffer length is derived from the watch measurement workflow. When a watch changes position, two physical settling periods must elapse before measurements become reliable: the hairspring needs about 15 seconds to stabilize under gravity, and the escapement wheel completes one full revolution in 60 seconds. This gives 75 seconds per position. A mechanical watch is typically tested in 6 standard positions (dial up, dial down, crown up, crown down, crown left, crown right), so covering all positions requires 75 × 6 = 450 seconds (7.5 minutes). Rounding up to 480 seconds (8 minutes) provides a margin that ensures the buffer always holds a complete set of all-position measurements.

Letting every display hold its own multi-minute history would multiply memory by the number of displays and worsen render scaling — the growth captured in [RISK-03](https://github.com/jaehong179/architect_cmu/blob/main/documents/06-risk-management.md#risk-03). A single central store bounds memory regardless of display count and keeps long-run resource use predictable, which [QAS-03](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-03-long-run-resource-stability) requires. Routing live data through the central store and having each display re-pull and re-format it every frame would add work on the hot path, threatening the end-to-end latency target ([QAS-02](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-02-end-to-end-latency)) and the timing-precision concern of extra buffering/resampling ([RISK-18](https://github.com/jaehong179/architect_cmu/blob/main/documents/06-risk-management.md#risk-18)). Keeping the live path direct avoids this; the history accumulates alongside as a cheap append, preserving streaming throughput ([QAS-01](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-01-real-time-streaming-throughput)). Introducing the history as a non-visual subscriber confines the change to one integration point with no edits to core processing or existing displays — the same open/closed property as [QAS-08](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-08-new-tab-extensibility) and a continuation of ADR-003. Retaining the signal at full resolution while applying level-of-detail only at draw time preserves zoom/measurement fidelity yet bounds the number of plotted points, so render cost stays bounded as the window grows ([RISK-03](https://github.com/jaehong179/architect_cmu/blob/main/documents/06-risk-management.md#risk-03)).

***Status***

Accepted

***Consequences***

Positive

- Memory is bounded by one central buffer instead of (display count × history), keeping long-run resource use predictable — satisfies [QAS-03](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-03-long-run-resource-stability) and mitigates [RISK-03](https://github.com/jaehong179/architect_cmu/blob/main/documents/06-risk-management.md#risk-03).
- The 8-minute window covers a full 6-position measurement cycle with margin, so users can scroll back to review any position without data loss.
- The live path stays direct and unchanged, protecting end-to-end latency and throughput ([QAS-02](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-02-end-to-end-latency), [QAS-01](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-01-real-time-streaming-throughput)) and avoiding added timing risk ([RISK-18](https://github.com/jaehong179/architect_cmu/blob/main/documents/06-risk-management.md#risk-18)).
- All displays see the same history, eliminating inconsistencies when different displays show the same seek position.
- History is added at a single integration point with no changes to core processing or existing displays ([QAS-08](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-08-new-tab-extensibility); builds on ADR-003).
- Displays draw live and replayed (past) data through one render path, simplifying display logic.

Negative / costs

- The most recent short window is held both in a display's working buffer and in the central buffer (small duplication).
- Two data paths (live vs replay) add conceptual complexity.
- All displays share one buffer with a fixed history length and sample rate. A display that needs a different length or rate would require extending the buffer design.
- Full-resolution retention keeps a large resident memory footprint; its long-run effect on resource/thermal stability must be verified against [QAS-03](https://github.com/jaehong179/architect_cmu/blob/main/documents/04-quality-attribute-requirements.md#qas-03-long-run-resource-stability) (sustained-operation resource/thermal spikes).
