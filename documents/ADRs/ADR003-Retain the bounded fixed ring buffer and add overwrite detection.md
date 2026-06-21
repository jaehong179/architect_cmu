# ADR-03: Retain the bounded fixed ring buffer and add overwrite detection

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
