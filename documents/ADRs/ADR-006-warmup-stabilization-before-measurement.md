# ADR-006: Warm-up (stabilization) period before recording measurements

Before recording a measurement, we run a configurable warm-up period. During warm-up the engine already processes the signal but commits no result, so both the watch and the measurement filters settle first — making the very first recorded value accurate.

## Context
A watch just placed or repositioned isn't stable yet: balance amplitude and hairspring need time to settle. Meanwhile the pipeline's rolling statistics (RollingLeastSquares for rate, RollingAverage for trends) need several beats to converge. Recording immediately would capture an unsettled watch and not-yet-converged filters — inaccurate values. This matters most in the multi-position sequence, where each position's recorded result must be trustworthy ([QAS-06](../Requirements/quality-attribute-requirements.md#qas-06--watch-position-auto-detection-accuracy) measures the indicator "after stabilization").

## Decision
- Provide a user-selectable warm-up delay (0 / 5 / 10 / 15 / 20 s), including `0 s` to disable it.
- During warm-up the engine keeps processing the signal (filters converge), but the controller stays in a `Warmup` phase and commits nothing; warm-up end triggers `beginMeasuringNow()` → `Measuring`.
- Warm-up runs at session start and on each position change, so every position is measured only after it settles.
- Warm-up end is set as the rate-graph time origin, leaving no gap in the recorded trace.

## Rationale
- **Filters pre-converge:** processing during warm-up means RollingLeastSquares / RollingAverage are already converged when recording starts.
- **Watch settles:** waiting after placement lets amplitude and hairspring stabilize before the value is trusted.
- **No trace artifact:** resetting the time origin keeps warm-up out of the recorded data.

## Status
Accepted

## Consequences
**Positive**
- Removes unsettled-watch and unconverged-filter transients at the start of each position, supporting [QAS-06](../Requirements/quality-attribute-requirements.md#qas-06--watch-position-auto-detection-accuracy) and [QAS-04](../Requirements/quality-attribute-requirements.md#qas-04--measurement-timing-precision-preservation).
- Configurable: `0 s` for a quick look, longer for a trustworthy sequence run.

**Negative / costs**
- Adds a delay before results appear (mitigated by making it user-selectable, including off).
- Requires a `Warmup` vs `Measuring` distinction in the UI so the user knows why no value is shown yet.
