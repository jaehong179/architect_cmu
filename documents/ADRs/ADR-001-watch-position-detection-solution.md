# ADR-001 : Limit AI (TinyML) to camera-based watch-position detection

We keep all acoustic measurement rule-based, and use TinyML only to read the watch position from a camera.

***Decision***

- The acoustic path stays rule-based: tick/tock detection, rate/beat-error/amplitude, health grading, fault hints. Low-SNR is solved with signal processing (adaptive threshold, matched filter), not ML.
- TinyML only reads the watch position from a camera (one of the 6 standard positions).
- The classifier never computes measurements — it only labels the position, so a wrong position cannot corrupt a measured value.
- AI detection is the default; when confidence is low, the user selects the position manually.

***Rationale***

- Acoustic measurement is the root of every metric, so it must stay deterministic and explainable — ML stays out.
- Watch position, however, can't be read from the microphone USB ([EXP-12](../Experiments/EXP-12-usb-protocol-watch-position-detection.md)).
- Camera images are easy to label per position, so ML is feasible here and a small model runs fine on the Pi ([EXP-18](../Experiments/EXP-18-camera-tinyml-9-position-accuracy.md)).

***Status***

Accepted

***Consequences***

Positive

- Users do not need to select current position manually.
- AI is isolated, so a wrong guess cannot corrupt the measurement.
- Keeps inference lightweight enough for the Raspberry Pi ([RISK-09](../README.md#risk-09)).

Negative / costs

- Adds a camera, a per-position dataset, and accuracy testing.
- Adds camera-environment dependencies as a new risk ([RISK-20](../README.md#risk-20)).
