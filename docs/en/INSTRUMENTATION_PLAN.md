# Instrumentation Plan & Results (INSTRUMENTATION_PLAN)

> Companion document: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md) — "what, why, and pass criteria"
> This document: **where and how those items are instrumented in the current code** + measurement/analysis methods.
> All instrumentation is **measurement-only** and does not change product functionality. Works on both Windows and Raspberry Pi.

---

## 0. Log Structure (common)

- Every measurement is recorded as a single line via `Perf::log(section, qa, metric, value, unit, extra)`.
- Output: **`perf_log.csv`** (in the run's working directory, rewritten on each run) + console (qDebug) simultaneously.
- CSV columns: `t_ms , section , qa , metric , value , unit , extra`
  - `t_ms` = monotonic clock (ms) since program start. The common reference for latency calculations.
  - `section` (e.g. `A-1`) / `qa` (e.g. `QA-LT-01`) provide a **1:1 link to the guide and M1 documents**.
- Instrumentation module: [PerfInstrumentation.h](../../PerfInstrumentation.h) / [.cpp](../../PerfInstrumentation.cpp)
  (a full tag↔document mapping table is at the top of the header). Only CPU, memory, and throttle use per-OS `#if` branches.
- **Per-log-group ON/OFF**: change the header's `PERF_GRP_*` / `PERF_MASTER_ENABLE` macros to `1`/`0` and
  rebuild to enable/disable recording at the group level (disabling also removes flush overhead → less observer effect).
  See [PERF_MEASUREMENT_OVERVIEW.md](PERF_MEASUREMENT_OVERVIEW.md) §3.5 for usage and the group↔metric table.

### Extraction examples
```powershell
# Extract end-to-end latency only → compute average/p95
Import-Csv perf_log.csv | Where-Object section -eq 'A-1' | Measure-Object value -Average -Maximum
```
```bash
grep ",A-1," perf_log.csv | awk -F, '{print $5}' | sort -n   # Pi/Linux
```

---

## 1. Per-Item Instrumentation Status (guide §A~§I)

Legend: ✅ code instrumentation / 🟡 external tool / manual procedure / ⬜ not applied (no applicable target in the current system)

| Guide | metric (CSV) | Status | Instrumentation location (file·function) | Pass criteria |
|--------|--------------|------|----------------------|-----------|
| **A-1** End-to-end lower bound | `e2e_latency_ms` | ✅ | MainWindow.cpp `ProcessSamples` (replot request) ← capture time from AudioWorker.cpp `ProcessAudioInput` | (lower bound·reference) |
| **A-1** ★true end-to-end | `e2e_full_ms` | ✅ | MainWindow.cpp `OnScopeReplotted` (afterReplot) | average ≤50ms·worst ≤100ms |
| **A-2** capture→process | `cap2proc_latency_ms` | ✅ | MainWindow.cpp `ProcessSamples` (process start) | identify stage bottleneck |
| **A-2** process→request | `proc2disp_latency_ms` | ✅ | MainWindow.cpp `ProcessSamples` (replot request) | identify stage bottleneck |
| **A-2** request→actual paint | `disp_paint_ms` | ✅ | MainWindow.cpp `OnScopeReplotted` (afterReplot) | identify stage bottleneck |
| **A-2** backlog | `backlog_samples` | ✅ | MainWindow.cpp `ProcessSamples` (entry) | trend (increase = queuing) |
| **A-3** UI responsiveness | `ui_loop_lag_ms` | ✅ | MainWindow.cpp `SamplePerfUiResponsiveness` (100ms timer) | ≤200ms |
| **A-4** fault recognition | `fault_sync_lost`·`detector_reset` | ✅ (semi-automatic) | MainWindow.cpp `ProcessSamples` (right after tg_process) | injection→log ≤2 s |
| **B-1** capture drop (estimated) | `capture_gap_samples`·`capture_gap_growth` | ✅ | AudioWorker.cpp `ProcessAudioInput` (2 s block) | 48k 0·96k 0·192k ≤1/60s |
| **B-1** capture error (device direct) | `audio_xrun`·`audio_state` | ✅ | AudioWorker.cpp `ProcessAudioInput`/`stateChangeAudioInput` (`QAudioSource::error()`) | no xrun |
| **B-2** uninterrupted operation | (observed via B-1 growth) | ✅ | 〃 | growth=0 |
| **B-4** signal-processing per stage | `dsp_hpf/env/detect/sync/total_ms` | ✅ | Timegrapher.cpp `tg_process` (per stage, aggregated per 1 s) | identify DSP bottleneck stage |
| **B-3** effective throughput | `bg_sps/fps/spf`·`fg_sps/fps/spf` | ✅ | AudioWorker.cpp + MainWindow.cpp `ProcessSamples` | SPS≈configured sps |
| **C-1** CPU% | `cpu_percent` | ✅ (built-in) | MainWindow.cpp `SamplePerfResources` (1Hz) | average ≤70% |
| **C-2** throttle (Pi) | `throttled_flag` | ✅ (Pi only) | 〃 → PerfInstrumentation `readThrottled` (vcgencmd) | none |
| **D-1/D-2** memory | `rss_bytes` | ✅ | MainWindow.cpp `SamplePerfResources` (1Hz) | 30 min ↑≤200MB·no leak |
| **E-2** Onset/Peak precision | `onset_err_ms`·`peak_err_ms` | ✅ (Sim) | MainWindow.cpp `ProcessSamples` (A/C event comparison) | Onset ≤0.5ms·Peak ≤0.2ms |
| **G-1** measurement accuracy | `rate_err_s_per_d`·`beaterr_err_ms`·`amp_err_deg` | ✅ (Sim) | MainWindow.cpp `DisplayResults` | ±1 s/d·±0.1ms·±5° |
| **G-2** detection rate | `a_match`·`c_match`·`a_unmatched`·`gt_total` | ✅ (Sim) | MainWindow.cpp `ProcessSamples`+`DisplayResults` | detection rate ≥95%·FP ≤2% |
| **G-3** noise robustness | (compare G-1/G-2 with noise ON/OFF) | 🟡 (procedure) | 〃 + Sim `noise_peak_amplitude`/Live 60dB | detection rate ≥80%·error ≤2× |
| **E-1** cumulative timing | — | 🟡 (future) | requires per-stage timestamps to be added (FR-SYS-4) | cumulative ≤1 sample |
| **F-1** screen refresh rate (frame drop) | `paint_fps` (+`replot_req`) | ✅ | MainWindow.cpp `OnScopeReplotted` (aggregated per 1 s) | degradation under load ≤10% (tabs 1→12 after tab implementation) |
| **F-2** display consistency | — | ⬜ | single display (tabs not implemented) | later |
| **H** TinyML | — | ⬜ | not implemented (exploratory task) | — |
| **I** deployment/startup | — | 🟡 (checklist) | operational procedure (AGC OFF, etc.) | ≤30 min |

---

## 2. Measurement Execution Procedure

### (1) Baseline profile — guide §J priorities
1. **Live mode 60 s ×3** (48k/96k/192k each, AGC OFF):
   → `A-1/A-2` (latency), `B-1/B-3` (drop/throughput), `C/D` (CPU/memory/throttle) recorded automatically.
2. From perf_log.csv, check the **latency stage breakdown** via `A-2` → pinpoint chunk-buffering / delay-line bottlenecks.
3. 30-minute continuous operation for `C-1`·`rss_bytes` trends → headroom/leak/throttle.

### (2) Accuracy & precision (E·G) — possible on PC (Pi not required)
1. Run in **Sim mode** after configuring BPH/Error/Amplitude/BeatError.
   → `G-1` (measured vs configured error), `E-2` (onset/peak error), `G-2` (detection rate) recorded automatically.
2. For clean measurement, **uncheck** Realistic (clean config); for robustness (G-3), enable Realistic/noise and compare with the same log.

### (3) Analysis → document update
- **Finalize/revise** the values of the `(TBD)` items (QA-RT-02·RT-03·CO-01·LT-01·EXP-06) with measurement results.
- ⚠️ Mentor's note: do not use measured values directly as required values. Requirements come from the evaluation criteria; measurements are the basis for feasibility judgment.

---

## 3. Formula Notes (during analysis)
- **Detection rate (G-2)** = Σ`a_match` / `gt_total` (final value). **FP** = Σ`a_unmatched`.
- **Latency (A-1)** = judged by the average/median/p95 of ★**`e2e_full_ms`** (true end-to-end, including paint). `e2e_latency_ms` is the lower bound (up to request). Stage sum: `cap2proc + proc2disp + disp_paint = e2e_full` (per-event).
- **CPU% (C-1)** = normalized across all cores (0~100). Check the core count via `cores=` in `extra`.
- **Drop (B-1)** = normal if `capture_gap_growth` is near 0; a sustained positive value indicates drops.

---

## 4. Platform Branch Summary
| Measurement | Windows | Raspberry Pi (Linux) |
|------|---------|---------------------|
| clock/latency/throughput/drop | common (std::chrono, Qt) | common |
| CPU% | GetProcessTimes | /proc/self/stat |
| RSS | GetProcessMemoryInfo(psapi) | /proc/self/statm |
| throttle | N/A (always false) | vcgencmd get_throttled |

> Build verification complete: Windows (MinGW/Qt 6.11.1) compiles and links OK. The Linux branch is separated by `#if defined(Q_OS_LINUX)` and includes the headers (`<cstring>/<cstdlib>/<unistd.h>`) in preparation for the Pi build.
