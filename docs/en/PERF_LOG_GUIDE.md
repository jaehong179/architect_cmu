# perf_log.csv Decoding Guide — Column / Section / Linkage Dictionary

> A dictionary explaining, line by line, **what each measurement in `perf_log.csv` is trying to prove** and **which document it links to**.
> Companion documents: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md) (what / why / pass criteria) · [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md) (where the instrumentation lives in code).

---

## 1. Column Structure

One CSV row = one measurement. Seven columns:

| Column | Meaning | Example | Use |
|------|------|------|------|
| **t_ms** | Elapsed time since program start (monotonic clock, ms) | `7394.269` | Reference for time-difference calculations, e.g. capture time vs. display time of the same beat |
| **section** | **Guide top-level category code (A–I)** = "which quality aspect" | `A-1` | Links directly to PERF_VERIFICATION_GUIDE.md §A-1 |
| **qa** | **M1 quality-attribute scenario ID** = "which requirement is verified" | `QA-LT-01` | Links to the M1 QA Taxonomy document |
| **metric** | Specific measurement indicator name | `e2e_latency_ms` | What was measured |
| **value** | Measured value (number) | `42.31` | The actual figure |
| **unit** | Unit | `ms` | Unit of interpretation |
| **extra** | Additional info (comparison values / conditions) | `meas=303.8;set=300.0` | Measured vs. configured value, sample rate, core count, etc. |

> The first two lines are `#` comments (session info: start time, OS, core count); the third line is the header.
> The `section` + `qa` columns together are the **key that connects log ↔ guide ↔ M1 requirements**.

---

## 2. Sections (A–I) = Quality Aspects (PERF_VERIFICATION_GUIDE top-level categories)

| Code | Category | What it tries to prove | Primary linked QA |
|------|--------|---------------------|-----------|
| **A** | Response speed / Latency | The claim that "real-time observation is possible in Live" | QA-LT-01, QA-RT-01, QA-US-01 |
| **B** | Throughput / capture drop | Whether data is not lost at high sample rates | QA-RT-02, QA-RT-01 |
| **C** | CPU / thermal | Whether there is resource headroom to add more features | QA-EE-01 |
| **D** | Memory | Whether it stays continuously usable without OOM or leaks | QA-RT-03, QA-SC-01 |
| **E** | Timing precision | Whether small time differences are captured precisely | QA-AC-02, QA-AC-03 |
| **F** | Render / scale | Whether responsiveness holds up as tabs increase | QA-SC-01, QA-CO-02 |
| **G** | Detection / measurement accuracy | Whether accurate values are produced from audio | QA-CO-01, QA-AC-01, QA-CO-04 |
| **H** | On-device AI | Whether real-time inference is possible on the Pi | QA-SE-01, QA-AC-04 |
| **I** | Deployment / startup | Whether the demo can be brought up reproducibly | QA-DY-01 |

---

## 3. metric Dictionary — What It Proves, Where It Links, Pass Criteria

> "Mode" = the condition under which the metric is recorded. **Live** = real-time microphone, **Sim** = synthetic (has ground-truth values, runs on PC), **Always** = mode-independent.

### A. Latency / Responsiveness
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `e2e_latency_ms` | End-to-end **lower bound**: capture → replot *request* | A-1·QA-LT-01 | ms | (lower bound, reference) | **Live** |
| `e2e_full_ms` | ★**True end-to-end**: capture → **actual pixels drawn** (request + paint combined) | A-1·QA-LT-01 | ms | avg ≤50, worst ≤100 | **Live** |
| `cap2proc_latency_ms` | Segment ①: capture → processing start | A-2·QA-LT-01 | ms | Identify stage bottleneck | **Live** |
| `proc2disp_latency_ms` | Segment ②: processing → replot *request* | A-2·QA-LT-01 | ms | Identify stage bottleneck | Always |
| `disp_paint_ms` | Segment ②-b: replot request → **actual paint complete** (afterReplot) | A-2·QA-LT-01 | ms | Identify stage bottleneck | Always |
| `backlog_samples` | Accumulated samples not yet processed (queue depth) | A-2·QA-LT-01 | samp | Increase = backlog forming | Always |
| `ui_loop_lag_ms` | UI event-loop delay (button responsiveness) | A-3·QA-RT-01 | ms | ≤200 | Always |
| `fault_sync_lost` / `detector_reset` / `sync_acquired` | **Timestamp of occurrence** of fault / sync events | A-4·QA-US-01 | event | injection → log ≤2 s | Always |

> ✅ **End-to-end is split into two segments and both are measured** (because replot is `rpQueuedReplot` = deferred rendering):
> - `e2e_latency_ms` = **lower bound** (capture → replot *request*; main-thread processing cost)
> - `disp_paint_ms` = the **actual paint time** that was deferred (request → afterReplot complete)
> - **`e2e_full_ms` = true end-to-end** = capture → actual pixels = (sum of the two above). **QA-LT-01 is judged by `e2e_full_ms`.**
> ※ `afterReplot` is ScopePlot's actual draw-complete signal. (SoundImage painting is separate/parallel and not included.)

### B. Throughput / Drop
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `capture_gap_samples` | Samples short of expected (drop **estimate**) | B-1·QA-RT-02 | samp | — | **Live** |
| `capture_gap_growth` | Shortfall increase over 2 s (sustained increase = drop) | B-1·QA-RT-02 | samp/2s | ≈0 | **Live** |
| `audio_xrun` | Capture error **reported directly** by the device (xrun/overrun) — recorded only on change | B-1·QA-RT-02 | errcode | none | **Live** |
| `audio_state` | Capture-device state transition (e.g. unexpected Idle) | B-1·QA-RT-02 | state | — | **Live** |

> 🎯 **Audio drops are observed two ways**: ① `capture_gap` = time-shortfall **estimate** (always active), ② `audio_xrun` = the **actual device error** reported by `QAudioSource::error()` (the Pi's ALSA xrun is also delivered by Qt through this value). `audio_xrun` errcode: 1=Open · 2=IO · 3=Underrun · 4=Fatal. (Qt owns the ALSA handle, so calling `snd_pcm_status` directly is impossible → substituted with Qt's error().)
| `bg_sps`/`bg_fps`/`bg_spf` | Background (capture) effective throughput | B-3·QA-RT-02 | samp/s etc. | SPS ≈ configured sps | **Live** |
| `fg_sps`/`fg_fps`/`fg_spf` | Foreground (handler + render) effective throughput | B-3·QA-RT-01 | samp/s etc. | Stable | Always |
| `dsp_hpf_ms` | ★Signal processing: HPF (high-pass filter) stage time | B-4·QA-RT-01 | ms | Identify bottleneck | Always |
| `dsp_env_ms` | ★Signal processing: envelope + delay-line stage | B-4·QA-RT-01 | ms | Identify bottleneck | Always |
| `dsp_detect_ms` | ★Signal processing: detection (Detector) stage | B-4·QA-RT-01 | ms | Identify bottleneck | Always |
| `dsp_sync_ms` | ★Signal processing: sync (BPH · sync · event output) stage | B-4·QA-RT-01 | ms | Identify bottleneck | Always |
| `dsp_total_ms` | ★Signal processing total (= sum of the 4 stages above) | B-4·QA-RT-01 | ms | DSP share within proc2disp | Always |

> 📐 `dsp_*` is measured per stage inside `tg_process` and recorded **every second** as the average (value) + **maximum** (extra `max=`).
> → On the Pi, identify "which stage (HPF / envelope / detection / sync) is the main culprit of signal-processing delay" separately. `dsp_total` is the pure DSP portion within `proc2disp`.

### C. CPU / Thermal
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `cpu_percent` | Process CPU% (normalized across all cores) | C-1·QA-EE-01 | % | avg ≤70 | Always |
| `throttled_flag` | Pi thermal-throttle occurrence bitmask | C-2·QA-EE-01 | bitmask | none (0) | **Pi only** |

### D. Memory
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `rss_bytes` | Process resident memory (leak / growth) | D-1·QA-RT-03 | bytes | 30 min ↑≤200MB · no leak | Always |

### E. Timing Precision (Sim ground-truth comparison)
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `onset_err_ms` | Detected A (onset) vs. ground-truth A error | E-2·QA-AC-02 | ms | ≤0.5 | **Sim** |
| `peak_err_ms` | Detected C vs. ground-truth C error | E-2·QA-AC-02 | ms | ≤0.2 | **Sim** |

### G. Detection / Measurement Accuracy (Sim ground-truth comparison)
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `rate_err_s_per_d` | Measured rate − configured rate | G-1·QA-CO-01 | s/d | ±1 | **Sim** |
| `beaterr_err_ms` | Measured beat error − configured | G-1·QA-CO-01 | ms | ±0.1 | **Sim** |
| `amp_err_deg` | Measured amplitude − configured | G-1·QA-CO-01 | deg | ±5 | **Sim** |
| `a_match`/`c_match` | A/C detection success (matched with ground truth) | G-2·QA-AC-01 | event | — | **Sim** |
| `a_unmatched`/`c_unmatched` | Detection miss / false detection (FP) | G-2·QA-AC-01 | event | FP ≤2% | **Sim** |
| `gt_total` | Cumulative ground-truth beat count (detection-rate denominator) | G-2·QA-AC-01 | beats | — | **Sim** |

> **F (render/scale) · H (AI) · I (deployment)** are currently uninstrumented (tabs/AI not yet implemented, deployment is procedural). See [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md) §1.

---

### F. Render / Frame (frame drop)
| metric | What it proves | section·qa | Unit | Pass criteria | Mode |
|--------|-------------|-----------|------|-----------|------|
| `paint_fps` | **Actual screen-update count per second** (afterReplot). A drop under load = frame drop | F-1·QA-SC-01 | frame/s | ≤10% degradation as tabs increase | Always |

> 🎞 The `replot_req` in `extra` = request count. **Requests > paints is normal** (rpQueuedReplot coalescing). A true frame drop = when `paint_fps` plummets under load. (Details → [PERF_MEASUREMENT_OVERVIEW §1.5](PERF_MEASUREMENT_OVERVIEW.md))

## 4. Formulas (for aggregation)
- **Detection rate (G-2)** = Σ`a_match` ÷ `gt_total` (final value). **FP** = Σ`a_unmatched`.
- **End-to-end latency (A-1)** = average / p95 / worst of `e2e_latency_ms`. Stage sum ≈ `cap2proc` + `proc2disp`.
- **Precision (E-2)** = average/worst of the **absolute value** of `onset_err_ms` · `peak_err_ms`.
- **Accuracy (G-1)** = signed average of `*_err_*` (= bias) + absolute value (= error spread).
- **Drop (B-1)** = if `capture_gap_growth` stays positive, there is a drop.
- **Memory (D-1)** = start→end growth of `rss_bytes`; an upward trend = suspected leak.

## 5. What Gets Populated by Mode
| Mode | Sections populated | Sections empty (normal) | Purpose |
|------|---------------|-----------------|------|
| **Sim** | A-2 (`backlog` · `proc2disp`) · A-3 · A-4 · B-3 (`fg_*`) · C · D · **E · G** | `cap2proc` · `e2e` (A) · B-1 · `bg_*` (all Live-only) | Accuracy/precision verification (PC-capable) |
| **Live** | A-1 · A-2 (full) · A-3 · A-4 · B (full) · C · D | E · G (no ground-truth values) | Actual latency/drop measurement (microphone required) |

> ⚠️ Current PC measurements (CPU, latency) are **for reference only**. Since the QA targets are **based on the Raspberry Pi**, performance (A · B · C · D) must be re-measured on the Pi.

## 6. Quick Extraction Examples
```powershell
# Average/max end-to-end latency (after Live measurement)
Import-Csv perf_log.csv | ? section -eq 'A-1' | Measure-Object value -Average -Maximum
# Detection rate
$r=Import-Csv perf_log.csv; ($r|? metric -eq 'a_match').Count / [double]($r|? metric -eq 'gt_total')[-1].value
```
