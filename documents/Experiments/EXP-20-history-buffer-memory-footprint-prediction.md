# EXP-20 / Central History Buffer Memory-Footprint Prediction

## Objective

- Estimate the memory footprint of a central 8-minute full-resolution history buffer before implementation.

## Status

- [Planned | In progress | Suspended | Canceled | **Concluded**]

## Expected outcomes

- A memory formula and size estimates at each target sample rate.

## Resources required

- Buffer design parameters: retention window, resolution policy, zoomed-out summary step size.

## Experiment description

Three cost components, each derived from the buffer design:

- **8-minute window**: 480 s (15 s stabilization + 60 s/position × 6 positions = 450 s, rounded up).
- **Full-resolution store**: 4 bytes/sample, nothing discarded on write. Cost scales as `sample rate × 480 s × 4 bytes`.
- **Zoomed-out summary index** (8× step per level): pre-built coarser copies for fast zoom-out. Adds ~29% over the full-resolution store.

Formula: `size = sample rate × 480 s × 4 bytes × 1.29`

Evaluated at: 48,000 / 96,000 / 192,000 samples/sec.

## Duration

06/25-06/25

## Links and references

[EXP-03](EXP-03-sustained-operation-resource-thermal-stability.md)

## Results and recommendations

**Estimated size:**

| sample rate | estimated size |
|---|---|
| 48,000 /sec | ~119 MB |
| 96,000 /sec | ~237 MB |
| 192,000 /sec | ~474 MB |

**Findings**

1. The zoomed-out summary index adds only ~29% — the dominant cost is the full-resolution store, which scales linearly with sample rate.
2. Even at 48,000 samples/sec, this buffer alone is ~119 MB.
3. At 192,000 samples/sec it reaches ~474 MB, roughly 4× the 48k case.
4. Single shared instance regardless of tab count — cost does not grow with the number of displays.

**Recommendation**

- The main lever for reducing size is the 480-second window or how it scales with sample rate. Consider a shorter window at higher sample rates.
