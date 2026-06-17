# Instrumentation Code Location Map (PERF_CODE_MAP)

> A document that traces **where in the code and through what flow** each measurement item is detected.
> Companion documents: [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md) (log decoding) · [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md).
> Note: Line numbers are as of the time of writing (they may shift when the code is modified). Search the source for `[PERF` or `Perf::log`.

---

## 0. Where instrumentation hooks in (overall picture)

```
[Capture thread]                      [Main thread]
AudioWorker/SimWorker                 MainWindow
  ProcessAudioInput/StartSim            HandleInputData ──► ProcessSamples ──► DisplayResults
   │ ring buffer write                    │ snapshot (Mutex)   │ tg_process()        │
   │ ◆record capture time                 │ ◆read capture time/GT │ ◆event matching   │ ◆measured vs configured
   │ ◆drop/throughput (B)                                       │ ◆latency/backlog (A)   (G-1)
   │ ◆GT loading (E/G)                                          │ ◆detection matching (E/G-2)
                                       QTimer 1Hz ─► SamplePerfResources   ◆CPU/memory/throttle (C/D)
                                       QTimer 0.1s ─► SamplePerfUiResponsiveness ◆UI responsiveness (A-3)
   Shared infrastructure: PerfInstrumentation.{h,cpp} (clock·CPU·RSS·throttle·CSV logger)
```
◆ = instrumentation insertion point.

---

## 1. Code locations per item + detection flow

### A-1 / A-2 — End-to-end / per-stage latency (capture→process→display) · QA-LT-01
**Cross-thread chain** (the worker stamps the capture time → the main thread compares it with the display time):

| Stage | File : Line | Function | What the code does |
|------|------------|------|----------------|
| Field definition | `SharedAudio.h` | `TMasterAudioDataRaw.LastBlockCaptureMs` | Field that stores the capture time |
| ◆Record capture time | `AudioWorker.cpp:100` | `ProcessAudioInput` | Immediately after writing the block to the ring buffer, `LastBlockCaptureMs = Perf::nowMs()` (inside the Mutex) |
| ◆Snapshot | `MainWindow.cpp:910` | `HandleInputData` | Read into `mLocalLastBlockCaptureMs` inside the same Mutex |
| ◆Processing start·backlog | `MainWindow.cpp:1007` | `ProcessSamples` | Records `perfProcStartMs` + logs `backlog_samples` |
| ◆Capture→process | `MainWindow.cpp:1010` | `ProcessSamples` | `cap2proc_latency_ms` (Live only) |
| ◆Process→request | `MainWindow.cpp:1170` | `ProcessSamples` (replot request) | `proc2disp_latency_ms` |
| ◆End-to-end lower bound | `MainWindow.cpp:1172` | `ProcessSamples` | `e2e_latency_ms` (up to request, Live) |
| ◆Store request time | `MainWindow.cpp:1175~` | `ProcessSamples` | `mPerfReplotRequestMs` etc. → used in afterReplot |
| ◆**Actual paint completion** | `MainWindow.cpp` `OnScopeReplotted` | (afterReplot signal) | `disp_paint_ms` (request→paint) · **`e2e_full_ms` (true end-to-end, Live)** |
| Signal connection | `MainWindow.cpp` (constructor) | `MainWindow()` | `connect(ui->ScopePlot, afterReplot, …)` |
| ◆**frame (screen refresh)** | `MainWindow.cpp` `OnScopeReplotted` (1-second aggregation) ← request count from `ProcessSamples` | `paint_fps` (F-1) · extra `replot_req` |

### A-3 — UI responsiveness · QA-RT-01
| File : Line | Function | What the code does |
|------------|------|----------------|
| Timer creation | `MainWindow.cpp` (constructor, 0.1s start) | `MainWindow()` | `mPerfUiTimer` 100ms |
| ◆Log | `MainWindow.cpp:155` | `SamplePerfUiResponsiveness` | Actual firing interval − 100ms = `ui_loop_lag_ms` |

### A-4 — Fault/observation events · QA-US-01
| File : Line | Function | What the code does |
|------------|------|----------------|
| ◆Log | `MainWindow.cpp:1040~1042` | `ProcessSamples` (right after tg_process) | Records the time of `r.sync_lost_event`/`sync_acquired`/`detector_reset` when they occur |

### B-1 / B-2 / B-3 — Drops·throughput · QA-RT-02/RT-01
| File : Line | Function | What the code does |
|------------|------|----------------|
| Store sample rate | `AudioWorker.cpp:46` | `StartAudioRecording` | `mSampleRate`, the baseline for drop estimation |
| ◆Throughput (bg) | `AudioWorker.cpp:118~121` | `ProcessAudioInput` (2-second block) | `bg_sps/fps/spf` |
| ◆Drop estimation | `AudioWorker.cpp:134~136` | 〃 | `capture_gap_samples`·`capture_gap_growth` (expected−actual cumulative) |
| ◆Device error (direct) | `AudioWorker.cpp` `ProcessAudioInput` (just before readAll) | When `QAudioSource::error()` changes | `audio_xrun` (xrun/overrun) |
| ◆Device state | `AudioWorker.cpp` `stateChangeAudioInput` | On state transition | `audio_state` |
| ◆Throughput (fg) | `MainWindow.cpp:1192~1194` | `ProcessSamples` (2-second block) | `fg_sps/fps/spf` |

### B-4 — Per-stage signal processing (DSP) time · QA-RT-01
| Stage | File : Function | What the code does |
|------|------------|----------------|
| ◆HPF | `Timegrapher.cpp` `tg_process` (around tg_hpf_process) | `pdHpf` = HPF time spent |
| ◆Envelope | `Timegrapher.cpp` `tg_process` (after tg_envelope_process+delay) | `pdEnv` |
| ◆Detection | `Timegrapher.cpp` `tg_process` (around tg_detector_process) | `pdDet` |
| ◆Sync | `Timegrapher.cpp` `tg_process` (after detection ~ just before return) | `pdSync` (BPH·sync·event output) |
| ◆Aggregation·log | `Timegrapher.cpp` `tg_process` (just before return) | Average + max every second → `dsp_hpf/env/detect/sync/total_ms` |

### C-1 / C-2 / D-1 — CPU·throttle·memory · QA-EE-01/RT-03
| File : Line | Function | What the code does |
|------------|------|----------------|
| Timer creation | `MainWindow.cpp` (constructor, 1s start) | `MainWindow()` | `mPerfResourceTimer` 1Hz |
| ◆CPU% | `MainWindow.cpp:166` | `SamplePerfResources` | `cpu_percent` (OS branch → PerfInstrumentation) |
| ◆Memory | `MainWindow.cpp:170` | 〃 | `rss_bytes` |
| ◆Throttle (Pi) | `MainWindow.cpp:174` | 〃 | `throttled_flag` (vcgencmd) |

### E-2 / G-1 / G-2 — Precision·accuracy·detection rate (matching against Sim ground truth) · QA-AC-01/02, QA-CO-01
**Cross-thread chain** (SimWorker loads ground truth → main thread matches it against detections):

| Stage | File : Line | Function | What the code does |
|------|------------|------|----------------|
| GT data structure | `SharedAudio.h:10` | `TGtBeat` + `GtBeats[]/GtHead/GtTotal` | Ground-truth A/C sample ring buffer |
| ◆GT initialization | `SimWorker.cpp:43~45` | `TSimWorker()` | Initializes the ring to 0 |
| ◆GT loading | `SimWorker.cpp:109~119` | `StartSim` | Synthesizer `events[]` → stores `a_sample`, `c_sample(=a+a_to_c×fs)` |
| ◆GT snapshot | `MainWindow.cpp:914~916` | `HandleInputData` (when Sim, Mutex) | Copies into `mLocalGt[]` |
| Store Sim config | `MainWindow.cpp` (SimStart) | `SimStart` | `mLastSimCfg` (ground-truth config values) |
| ◆A matching (E-2/G-2) | `MainWindow.cpp:1083~1090` | `ProcessSamples` (A event) | Detected A vs ground-truth A → `onset_err_ms`·`a_match`/`a_unmatched` |
| ◆C matching (E-2/G-2) | `MainWindow.cpp:1137~1144` | `ProcessSamples` (C event) | Detected C vs ground-truth C → `peak_err_ms`·`c_match`/`c_unmatched` |
| ◆Accuracy (G-1) | `MainWindow.cpp:505~521` | `DisplayResults` | Measured value − `mLastSimCfg` → `rate_err`/`beaterr_err`/`amp_err`·`gt_total` |

---

## 2. The 'origin' of measured values (where the value the log reads is computed)

Instrumentation **does not create new calculations**; it reads existing measurement results and records them:

| Log metric | Source variable read (existing code) | Computation location |
|-------------|---------------------------|-----------|
| `e2e/cap2proc/proc2disp` | `Perf::nowMs()` time difference | (instrumentation itself) |
| `bg_*`/`fg_*` | `mRawAudio->SPS/FPS/SPF`, `mForegroundSPS…` | AudioWorker / ProcessSamples |
| `rate_err` | `mRateErrorEvents.RlsRate` | `ComputeRateError` (RollingLeastSquares) |
| `beaterr_err` | `mBeatErrorEvents.RollBeatError->GetAverage()` | `ComputeBeatError` |
| `amp_err` | `mAmplitudeEvents.RollAmplitude->GetAverage()` | `ComputeAmplitude` |
| `onset_err`/`peak_err` | Detected `r.events[i].sample_index` vs `mLocalGt` | tg_process(Detector) vs GT |
| `a_match`/`gt_total` | Detected events vs `GtTotal` | 〃 |

---

## 3. Shared infrastructure (PerfInstrumentation)
| Feature | File : Function | Notes |
|------|------------|------|
| Monotonic clock | `PerfInstrumentation.cpp` `nowMs()` | std::chrono (shared) |
| Tagged CSV logger | `log()` | Records `section/qa/metric` |
| CPU% | `sampleProcessCpuPercent()` | **Win**: GetProcessTimes / **Pi**: /proc/self/stat |
| RSS | `sampleProcessRssBytes()` | **Win**: GetProcessMemoryInfo / **Pi**: /proc/self/statm |
| Throttle | `readThrottled()` | **Pi**: vcgencmd / **Win**: N/A |
| Start/shutdown | `Main.cpp:28 / :61` | `Perf::init/shutdown` |

> The top of the header [PerfInstrumentation.h](../../PerfInstrumentation.h) contains a full tag↔document mapping table.

---

## 4. Quick search
```powershell
# All log insertion points
Select-String -Path *.cpp -Pattern 'Perf::log\('
# Instrumentation comments (including data flow)
Select-String -Path *.cpp,*.h -Pattern '\[PERF'
```
