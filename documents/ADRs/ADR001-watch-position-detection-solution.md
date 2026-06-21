# ADR001 : Limit AI (TinyML) to camera-based watch-position detection

We keep all acoustic measurement rule-based, and use TinyML only to read the watch position from a camera. This keeps measurement trustworthy, and adds AI only where rules cannot help.

***Decision***

The acoustic path stays rule-based: tick/tock detection, rate/beat-error/amplitude, health grading, fault hints. Low-SNR is solved with signal processing (adaptive threshold, matched filter), not ML.
TinyML only reads the watch position from a camera (one of the 9 standard positions).
The classifier never computes measurements — it only labels the position, so a wrong position cannot corrupt a measured value.
AI detection is the default; when confidence is low, the user selects the position manually.

***Rationale***

Acoustic measurement is the root of every metric, so it must stay deterministic and explainable — ML stays out. Watch position, however, can't be read from the acoustic signal (EXP-12), and is hard to express as rules. Camera images are easy to label per position, so ML is feasible here (avoids RISK-05) and a small model runs fine on the Pi (RISK-09).

***Status***

Accepted

***Consequences***

Positive

- Keeps measurement trustworthy and explainable.
- AI is isolated, so a wrong guess cannot corrupt the measurement.
- Provides orientation reading that acoustic methods cannot, and keeps AI isolated so a misprediction cannot corrupt measurement.
- Keeps inference lightweight enough for the Raspberry Pi ([RISK-09](../06-risk-management.md#risk-09)).

Negative / costs

- Adds a camera, a per-position dataset, and accuracy testing (EXP-18).
- Adds camera-environment dependencies as a new risk (RISK-20).
