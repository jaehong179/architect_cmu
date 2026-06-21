# EXP-10 / Experiment for [FR-AI-1]: Real-time TinyML Inference Performance on Raspberry Pi

## Objective

- Verify whether the lightweight AI model (TinyML) can perform inference in real-time on the Raspberry Pi 5 without impacting the system's core measurement performance, and validate the development team's capability to build and deploy TinyML solutions.

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- Average inference latency for the TinyML model.

- Confirmation that the inference latency is consistently below the allocated time budget per measurement cycle.

- CPU and memory usage report during real-time inference.

- Establish a working TinyML pipeline (edge deployment) to overcome the team's initial lack of experience

## Resources required

- Dummy TinyML model

- Raspberry Pi 5 with the integrated TinyML model.

## Experiment description

1. Integrate the dummy TinyML model into the Timegrapher application running on the Raspberry Pi 5.

2. Measure the inference time for each incoming data block over a sustained period (e.g., 100 inferences).

3. Monitor the overall system's CPU and memory load to ensure that the AI model does not introduce bottlenecks or resource contention.

4. Analyze the collected latency and resource data to determine real-time viability.

## Duration

- 06/04–06/08

## Links and references

- FR-AI-1, [RISK-09](../06-risk-management.md#risk-09), [RISK-11](../06-risk-management.md#risk-11)

## Results and recommendations

- The experiment was conducted using a Conv2D-based audio classification model (19,844 parameters) with MFCC (Mel-frequency cepstral coefficients) preprocessing on the Raspberry Pi 5.

- The measured inference latency was 2ms per data block.

- The 2ms inference time is well within the real-time processing budget.

- This confirms that real-time TinyML inference on the Raspberry Pi is highly viable, effectively mitigating [RISK-09](../06-risk-management.md#risk-09).

- Furthermore, successfully building and deploying this model validates the team's acquired proficiency in TinyML workflows, successfully mitigating [RISK-11](../06-risk-management.md#risk-11)
