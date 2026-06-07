# 계측 코드 위치 지도 (PERF_CODE_MAP)

> 각 측정 항목이 **코드의 어디에서·어떤 흐름으로** 검출되는지 추적하는 문서.
> 짝 문서: [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md)(로그 해독) · [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md).
> ※ 라인 번호는 작성 시점 기준(코드 수정 시 이동 가능). 검색은 소스에서 `[PERF` 또는 `Perf::log` 로.

---

## 0. 계측이 끼어드는 지점 (전체 그림)

```
[캡처 스레드]                         [메인 스레드]
AudioWorker/SimWorker                 MainWindow
  ProcessAudioInput/StartSim            HandleInputData ──► ProcessSamples ──► DisplayResults
   │ 링버퍼 쓰기                          │ 스냅샷(Mutex)      │ tg_process()        │
   │ ◆캡처시각 기록                       │ ◆캡처시각/GT 읽기   │ ◆이벤트 대조        │ ◆측정값 vs 설정
   │ ◆드롭/처리량(B)                                          │ ◆지연/백로그(A)      (G-1)
   │ ◆GT 적재(E/G)                                            │ ◆검출 대조(E/G-2)
                                       QTimer 1Hz ─► SamplePerfResources   ◆CPU/메모리/스로틀(C/D)
                                       QTimer 0.1s ─► SamplePerfUiResponsiveness ◆UI응답(A-3)
   공통 인프라: PerfInstrumentation.{h,cpp} (시계·CPU·RSS·스로틀·CSV 로거)
```
◆ = 계측 삽입 지점.

---

## 1. 항목별 코드 위치 + 검출 흐름

### A-1 / A-2 — 종단간·단계 지연 (캡처→처리→표시) · QA-LT-01
**크로스 스레드 체인**(캡처시각을 워커가 찍고 → 메인이 표시시각과 비교):

| 단계 | 파일 : 라인 | 함수 | 코드가 하는 일 |
|------|------------|------|----------------|
| 필드 정의 | `SharedAudio.h` | `TMasterAudioDataRaw.LastBlockCaptureMs` | 캡처시각 저장 필드 |
| ◆캡처시각 기록 | `AudioWorker.cpp:100` | `ProcessAudioInput` | 블록을 링버퍼에 쓴 직후 `LastBlockCaptureMs = Perf::nowMs()` (Mutex 안) |
| ◆스냅샷 | `MainWindow.cpp:910` | `HandleInputData` | 같은 Mutex 안에서 `mLocalLastBlockCaptureMs` 로 읽음 |
| ◆처리 시작·백로그 | `MainWindow.cpp:1007` | `ProcessSamples` | `perfProcStartMs` 기록 + `backlog_samples` 로그 |
| ◆캡처→처리 | `MainWindow.cpp:1010` | `ProcessSamples` | `cap2proc_latency_ms`(Live 한정) |
| ◆처리→요청 | `MainWindow.cpp:1170` | `ProcessSamples`(replot 요청) | `proc2disp_latency_ms` |
| ◆종단간 하한 | `MainWindow.cpp:1172` | `ProcessSamples` | `e2e_latency_ms`(요청까지, Live) |
| ◆요청시점 보관 | `MainWindow.cpp:1175~` | `ProcessSamples` | `mPerfReplotRequestMs` 등 → afterReplot에서 사용 |
| ◆**실제 페인트 완료** | `MainWindow.cpp` `OnScopeReplotted` | (afterReplot 신호) | `disp_paint_ms`(요청→페인트) · **`e2e_full_ms`(진짜 종단간, Live)** |
| 신호 연결 | `MainWindow.cpp`(생성자) | `MainWindow()` | `connect(ui->ScopePlot, afterReplot, …)` |
| ◆**frame(화면갱신)** | `MainWindow.cpp` `OnScopeReplotted`(1초 집계) ← 요청 카운트 `ProcessSamples` | `paint_fps`(F-1) · extra `replot_req` |

### A-3 — UI 응답성 · QA-RT-01
| 파일 : 라인 | 함수 | 코드가 하는 일 |
|------------|------|----------------|
| 타이머 생성 | `MainWindow.cpp`(생성자, 0.1s 시작) | `MainWindow()` | `mPerfUiTimer` 100ms |
| ◆로그 | `MainWindow.cpp:155` | `SamplePerfUiResponsiveness` | 실제 발화 간격−100ms = `ui_loop_lag_ms` |

### A-4 — 결함/관측 이벤트 · QA-US-01
| 파일 : 라인 | 함수 | 코드가 하는 일 |
|------------|------|----------------|
| ◆로그 | `MainWindow.cpp:1040~1042` | `ProcessSamples`(tg_process 직후) | `r.sync_lost_event`/`sync_acquired`/`detector_reset` 발생 시각 기록 |

### B-1 / B-2 / B-3 — 드롭·처리량 · QA-RT-02/RT-01
| 파일 : 라인 | 함수 | 코드가 하는 일 |
|------------|------|----------------|
| 샘플레이트 저장 | `AudioWorker.cpp:46` | `StartAudioRecording` | 드롭 추정 기준 `mSampleRate` |
| ◆처리량(bg) | `AudioWorker.cpp:118~121` | `ProcessAudioInput`(2초 블록) | `bg_sps/fps/spf` |
| ◆드롭 추정 | `AudioWorker.cpp:134~136` | 〃 | `capture_gap_samples`·`capture_gap_growth`(기대−실제 누적) |
| ◆장치 오류(직접) | `AudioWorker.cpp` `ProcessAudioInput`(readAll 직전) | `QAudioSource::error()` 변화 시 | `audio_xrun`(xrun/overrun) |
| ◆장치 상태 | `AudioWorker.cpp` `stateChangeAudioInput` | 상태 전이 시 | `audio_state` |
| ◆처리량(fg) | `MainWindow.cpp:1192~1194` | `ProcessSamples`(2초 블록) | `fg_sps/fps/spf` |

### B-4 — 신호처리(DSP) 단계별 처리시간 · QA-RT-01
| 단계 | 파일 : 함수 | 코드가 하는 일 |
|------|------------|----------------|
| ◆HPF | `Timegrapher.cpp` `tg_process`(tg_hpf_process 전후) | `pdHpf` = HPF 소요 |
| ◆엔벨로프 | `Timegrapher.cpp` `tg_process`(tg_envelope_process+delay 후) | `pdEnv` |
| ◆검출 | `Timegrapher.cpp` `tg_process`(tg_detector_process 전후) | `pdDet` |
| ◆동기 | `Timegrapher.cpp` `tg_process`(검출 후~return 직전) | `pdSync`(BPH·sync·이벤트출력) |
| ◆집계·로그 | `Timegrapher.cpp` `tg_process`(return 직전) | 1초마다 평균+최대 → `dsp_hpf/env/detect/sync/total_ms` |

### C-1 / C-2 / D-1 — CPU·스로틀·메모리 · QA-EE-01/RT-03
| 파일 : 라인 | 함수 | 코드가 하는 일 |
|------------|------|----------------|
| 타이머 생성 | `MainWindow.cpp`(생성자, 1s 시작) | `MainWindow()` | `mPerfResourceTimer` 1Hz |
| ◆CPU% | `MainWindow.cpp:166` | `SamplePerfResources` | `cpu_percent`(OS분기→PerfInstrumentation) |
| ◆메모리 | `MainWindow.cpp:170` | 〃 | `rss_bytes` |
| ◆스로틀(Pi) | `MainWindow.cpp:174` | 〃 | `throttled_flag`(vcgencmd) |

### E-2 / G-1 / G-2 — 정밀도·정확도·검출률 (Sim 정답 대조) · QA-AC-01/02, QA-CO-01
**크로스 스레드 체인**(SimWorker가 정답 적재 → 메인이 검출과 대조):

| 단계 | 파일 : 라인 | 함수 | 코드가 하는 일 |
|------|------------|------|----------------|
| GT 자료구조 | `SharedAudio.h:10` | `TGtBeat` + `GtBeats[]/GtHead/GtTotal` | 정답 A/C 샘플 링버퍼 |
| ◆GT 초기화 | `SimWorker.cpp:43~45` | `TSimWorker()` | 링 0으로 초기화 |
| ◆GT 적재 | `SimWorker.cpp:109~119` | `StartSim` | 합성기 `events[]` → `a_sample`, `c_sample(=a+a_to_c×fs)` 저장 |
| ◆GT 스냅샷 | `MainWindow.cpp:914~916` | `HandleInputData`(Sim일 때, Mutex) | `mLocalGt[]` 로 복사 |
| Sim 설정 보관 | `MainWindow.cpp`(SimStart) | `SimStart` | `mLastSimCfg`(정답 설정값) |
| ◆A 대조(E-2/G-2) | `MainWindow.cpp:1083~1090` | `ProcessSamples`(A 이벤트) | 검출 A vs 정답 A → `onset_err_ms`·`a_match`/`a_unmatched` |
| ◆C 대조(E-2/G-2) | `MainWindow.cpp:1137~1144` | `ProcessSamples`(C 이벤트) | 검출 C vs 정답 C → `peak_err_ms`·`c_match`/`c_unmatched` |
| ◆정확도(G-1) | `MainWindow.cpp:505~521` | `DisplayResults` | 측정값−`mLastSimCfg` → `rate_err`/`beaterr_err`/`amp_err`·`gt_total` |

---

## 2. 측정값의 '원천' (어디서 계산된 값을 로그가 읽는가)

계측은 **새 계산을 만들지 않고**, 기존 측정 결과를 읽어서 기록한다:

| 로그 metric | 읽는 원천 변수 (기존 코드) | 계산 위치 |
|-------------|---------------------------|-----------|
| `e2e/cap2proc/proc2disp` | `Perf::nowMs()` 시각차 | (계측 자체) |
| `bg_*`/`fg_*` | `mRawAudio->SPS/FPS/SPF`, `mForegroundSPS…` | AudioWorker / ProcessSamples |
| `rate_err` | `mRateErrorEvents.RlsRate` | `ComputeRateError`(RollingLeastSquares) |
| `beaterr_err` | `mBeatErrorEvents.RollBeatError->GetAverage()` | `ComputeBeatError` |
| `amp_err` | `mAmplitudeEvents.RollAmplitude->GetAverage()` | `ComputeAmplitude` |
| `onset_err`/`peak_err` | 검출 `r.events[i].sample_index` vs `mLocalGt` | tg_process(Detector) vs GT |
| `a_match`/`gt_total` | 검출 이벤트 vs `GtTotal` | 〃 |

---

## 3. 공통 인프라 (PerfInstrumentation)
| 기능 | 파일 : 함수 | 비고 |
|------|------------|------|
| 단조 시계 | `PerfInstrumentation.cpp` `nowMs()` | std::chrono(공통) |
| 태그 CSV 로거 | `log()` | `section/qa/metric` 기록 |
| CPU% | `sampleProcessCpuPercent()` | **Win**: GetProcessTimes / **Pi**: /proc/self/stat |
| RSS | `sampleProcessRssBytes()` | **Win**: GetProcessMemoryInfo / **Pi**: /proc/self/statm |
| 스로틀 | `readThrottled()` | **Pi**: vcgencmd / **Win**: N/A |
| 시작/종료 | `Main.cpp:28 / :61` | `Perf::init/shutdown` |

> 헤더 [PerfInstrumentation.h](../PerfInstrumentation.h) 상단에 전체 태그↔문서 매핑표 있음.

---

## 4. 빠른 검색
```powershell
# 모든 로그 삽입 지점
Select-String -Path *.cpp -Pattern 'Perf::log\('
# 계측 주석(데이터흐름 포함)
Select-String -Path *.cpp,*.h -Pattern '\[PERF'
```
