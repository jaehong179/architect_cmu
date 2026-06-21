# Limit AI (TinyML) to camera-based watch-position reading

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
Measurement orientation, however, cannot be reliably obtained from the acoustic signal alone (EXP-12 concluded a manual fallback due to absent USB position data), and visual orientation is hard to express as rules. In contrast, external-camera images are realistically labelable per orientation, which makes ML training data feasible and avoids the labeled-dataset shortage (RISK-05). A lightweight image classifier is also small enough for on-Pi inference (RISK-09). This makes camera-based position reading the one place where TinyML provides value that deterministic methods cannot easily deliver, without putting AI on the trust-critical measurement path.

***Status***

Accepted

***Consequences***

Positive

- Preserves measurement trustworthiness, explainability, and verifiability by keeping the core path deterministic.

- Avoids the labeled-data shortage (RISK-05) by choosing a domain (camera images) that is easy to label.

- Provides orientation reading that acoustic methods cannot, and keeps AI isolated so a misprediction cannot corrupt measurement.

- Keeps inference lightweight enough for the Raspberry Pi (RISK-09).

Negative / costs

- Adds external-camera hardware and the associated setup.

- Requires building a per-orientation image dataset and verifying position-reading accuracy separately (follow-up EXP-18).

- Introduces new camera-environment dependencies (lighting, angle, framing) that must be assessed as a new risk (RISK-20).
