# EXP-11 / Experiment for [FR-AI-1]: AI-based Denoising Approach to Bypass Manual Labeling

## Objective

- Verify whether pivoting the AI task to noise reduction (denoising) can effectively enhance the watch signal for legacy algorithms, while completely eliminating the need for manual data labeling by utilizing synthetic noise injection.

## Status

- [Planned | In progress | Suspended | **Canceled** | Concluded]

## Expected outcomes

- Successful automatic generation of a large-scale paired training dataset (Noisy Input → Clean Target).

- Training a lightweight TinyML denoising model (e.g., DeepFilterNet or RNNoise-style model).

- Confirmation that the denoised audio significantly improves the accuracy of the traditional peak-detection/measurement logic compared to raw noisy audio.

## Resources required

- A small set of high-quality, clean watch ticking recordings (Ground Truth).

- A database of various environmental noises (white noise, room ambiance, human voices).

- Computing resources for generating the mixed datasets and training the model.

## Experiment description

- Collect or synthesize a high-quality baseline of clean watch tick sounds.

- Synthetically mix these clean sounds with various noise profiles at varying Signal-to-Noise Ratios (SNR) to auto-generate the training dataset.

- Train a lightweight denoising model using the Noisy-Clean pairs.

- Integrate the model into the pipeline as a pre-processing step before the main measurement algorithm.

- Evaluate the overall system accuracy and measurement stability with and without the AI denoising block.

## Duration

06/15–06/22

## Links and references

FR-AI-1 · RISK-05

## Results and recommendations

1) Testing confirmed that human speech is not captured by the TimeGrapher hardware. Therefore, it was concluded that the TimeGrapher hardware does not accept human voice as an input signal.

2) Strong impact noise can overlap with the watch signal and may introduce audio clipping, leading to irreversible information loss. As a result, the original watch signal cannot be reliably recovered using AI-based noise suppression techniques alone.

Therefore, this experiment is canceled.
