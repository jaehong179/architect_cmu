# TimeGrapher 코드 분석 — 통합 인덱스

> **이 파일 하나로 전체를 본다.** `Ctrl+Shift+V`(미리보기)로 열면 아래 다이어그램이 그림으로 렌더링되고,
> 표의 링크로 상세 문서·자동생성 그래프로 바로 이동할 수 있다.

기계식 시계의 음향 신호를 받아 **rate·amplitude·beat error·BPH** 를 실시간 측정·시각화하는 Qt/C++ 앱.

---

## 0. 문서 지도 (여기서 출발)

| 보고 싶은 것 | 파일 | 여는 법 |
|--------------|------|---------|
| **전체 개요 + 핵심 다이어그램** | 📍 이 파일 | `Ctrl+Shift+V` |
| 손작업 종합 분석(구조·산식·분석법) | [CodeAnalysis.md](CodeAnalysis.md) | `Ctrl+Shift+V` |
| Doxygen+CMake 자동분석 결과 | [AutomatedAnalysis.md](AutomatedAnalysis.md) | `Ctrl+Shift+V` |
| clang-uml 자동 시퀀스 다이어그램 | [SequenceDiagrams.md](SequenceDiagrams.md) | `Ctrl+Shift+V` |
| **클릭 탐색형 전체 레퍼런스** (945 그래프) | `doxygen/html/index.html` | 브라우저 |
| 빌드 의존성 그래프 | `doxygen/cmake_deps.svg` | 브라우저 |
| clang-uml 원본 다이어그램 | `clang-uml/*.mmd` | Mermaid 미리보기 |
| ★**성능 측정 전체 개요**(입력→처리→연산→UI + 분석법) | [PERF_MEASUREMENT_OVERVIEW.md](PERF_MEASUREMENT_OVERVIEW.md) | `Ctrl+Shift+V` |
| 성능 검증 — 무엇을·왜·합격기준 | [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md) | `Ctrl+Shift+V` |
| **성능 로그(perf_log.csv) 해독 사전** | [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md) | `Ctrl+Shift+V` |
| **성능 계측 — 코드 어디서 측정?** | [PERF_CODE_MAP.md](PERF_CODE_MAP.md) | `Ctrl+Shift+V` |
| 성능 계측 현황·측정 절차 | [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md) | `Ctrl+Shift+V` |
| **Pi 측정 체크리스트(최종 판정용)** | [PI_MEASUREMENT_CHECKLIST.md](PI_MEASUREMENT_CHECKLIST.md) | `Ctrl+Shift+V` |

재생성 설정/스크립트는 원본 프로젝트에 포함: `docs/Doxyfile` · `.clang-uml` · `docs/clang-uml/gen_cdb.ps1` · `docs/gen_site.py` (이 공유본에는 미포함)

---

## 1. 아키텍처 한눈에 (3계층 + 멀티스레드)

```mermaid
flowchart LR
    subgraph ACQ["① 신호 수집 (백그라운드 스레드)"]
        AW[TAudioWorker<br/>마이크]
        PW[TPlaybackWorker<br/>WAV]
        SW[TSimWorker<br/>합성]
        RB[(TMasterAudioDataRaw<br/>공유 링버퍼)]
        AW --> RB
        PW --> RB
        SW --> RB
    end
    subgraph PROC["② 신호처리/측정 (tg_* C API)"]
        TG[tg_process]
        HPF[HPF] --> ENV[Envelope] --> DET[Detector<br/>A/C 검출] --> SYNC[Sync/BPH PLL]
        TG -.조립.- HPF
    end
    subgraph GUI["③ 표현 (메인/UI 스레드)"]
        MW[MainWindow<br/>ProcessSamples]
        RP[RatePlot]
        SP[ScopePlot]
        SI[SoundImage]
        BAR[측정 요약바]
    end
    RB -->|AudioDataReady signal| MW
    MW --> TG
    TG -->|events/bph| MW
    MW --> RP & SP & SI & BAR
```

---

## 2. 클래스 관계 한눈에 (핵심만)

```mermaid
classDiagram
    class MainWindow {
        <<QMainWindow>>
        +HandleAudioInput()
        +ProcessSamples()
        +A_Event() / C_Event()
        +DisplayResults()
    }
    class TAudioWorker { <<QObject>> }
    class TPlaybackWorker { <<QObject>> }
    class TSimWorker { <<QObject>> }
    class TMasterAudioDataRaw { <<struct>> +Samples +WriteIndex }
    class tg_context { <<struct>> +hpf +env +det +sync }
    class SoundImageRenderer
    class RollingAverage
    class RollingLeastSquares

    MainWindow o-- tg_context : mCtx
    MainWindow o-- TAudioWorker
    MainWindow o-- TPlaybackWorker
    MainWindow o-- TSimWorker
    MainWindow o-- TMasterAudioDataRaw
    MainWindow *-- SoundImageRenderer
    MainWindow *-- RollingAverage : beat/amp
    MainWindow *-- RollingLeastSquares : rate
    TAudioWorker --> TMasterAudioDataRaw : writes
    TPlaybackWorker --> TMasterAudioDataRaw : writes
    TSimWorker --> TMasterAudioDataRaw : writes
```
> 전체 멤버까지 보려면 [CodeAnalysis.md §3](CodeAnalysis.md) 또는 Doxygen `class_main_window.html`.

---

## 3. 측정 흐름 한눈에 (clang-uml 추출 정제판)

```mermaid
sequenceDiagram
    autonumber
    participant HW as 마이크
    participant AW as TAudioWorker
    participant RB as 링버퍼
    participant MW as MainWindow
    participant TG as tg_process
    participant UI as Plot/SoundImage/요약바

    HW->>AW: PCM 샘플
    AW->>RB: lock→memcpy→unlock
    AW-->>MW: AudioDataReady (signal)
    MW->>RB: 스냅샷 → ProcessSamples()
    loop 4096 샘플 청크
        MW->>TG: tg_process(block)
        TG-->>MW: events(A/C), bph, processed_pcm
        alt A 이벤트
            MW->>MW: ComputeRateError / ComputeBeatError
        else C 이벤트
            MW->>MW: ComputeAmplitude → DisplayResults
        end
    end
    MW->>UI: replot / DrawImage / 요약바 갱신
```
> 전체 내부 호출은 [SequenceDiagrams.md](SequenceDiagrams.md) 와 원본 [seq_process_samples.mmd](clang-uml/seq_process_samples.mmd).

---

## 4. 빌드 의존성 한눈에

```mermaid
graph LR
    TimeGrapher --> QtCore & QtWidgets & QtMultimedia & QtPrintSupport
    TimeGrapher --> winmm["winmm(1ms 타이머)"] & Ole32 & Propsys
    QtMultimedia --> QtGui & QtNetwork
    QtWidgets --> QtGui --> QtCore
    QtCore --> Threads
```
> 원본: `doxygen/cmake_deps.svg`. 해석은 [AutomatedAnalysis.md §3](AutomatedAnalysis.md).

---

## 5. 한눈에 보기 — 실행 팁

- **이 README + 미리보기(`Ctrl+Shift+V`)** = 구조/클래스/흐름/의존성 4개 다이어그램을 한 화면에서 스크롤로 확인.
- **클릭 탐색이 필요하면** → `doxygen/html/index.html`(브라우저). 함수별 호출/피호출 그래프까지.
- **재생성**: 코드 변경 후 [AutomatedAnalysis.md §5](AutomatedAnalysis.md) / [SequenceDiagrams.md §5](SequenceDiagrams.md) 런북 실행.

---

## 6. 성능 검증 — 로그(perf_log.csv) ↔ 코드 위치 (한눈에)

> 실행하면 작업 디렉터리에 **`perf_log.csv`** 가 생성된다. CSV 컬럼: `t_ms,section,qa,metric,value,unit,extra`.
> `section`(예 `A-1`)·`qa`(예 `QA-LT-01`)로 [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md)·M1과 연결된다.
> 아래 표 = **"이 측정값은 코드 어디서 나오나"** 를 바로 찾는 색인. (라인은 작성 시점; 최신은 소스에서 `Perf::log(` 검색)

### 계측이 끼어드는 지점
```mermaid
flowchart LR
    AW["AudioWorker / SimWorker<br/>(캡처 스레드)"] -->|"◆캡처시각·드롭·GT 적재"| RB[(공유 링버퍼)]
    RB -->|"◆스냅샷(Mutex)"| MW["MainWindow<br/>(메인 스레드)"]
    MW -->|"◆지연·검출대조·정확도"| LOG[("perf_log.csv")]
    T1["QTimer 1Hz"] -->|"◆CPU·메모리·스로틀"| LOG
    T2["QTimer 0.1s"] -->|"◆UI 응답성"| LOG
```

### 측정 구간(span) 한눈에 — 지연은 "어디부터 어디까지"인가
> 한 비트가 캡처되어 화면에 뜨기까지의 **시각 도장(◆)** 과, 그 사이 **구간(색칠)** 이 각각 어떤 metric인지.

```mermaid
sequenceDiagram
    autonumber
    participant AW as AudioWorker<br/>(캡처 스레드)
    participant RB as 공유 링버퍼
    participant MW as MainWindow<br/>(메인 스레드)
    participant TG as tg_process
    participant UI as 화면(replot)
    participant LOG as perf_log.csv

    AW->>AW: PCM 블록 캡처
    Note over AW: ◆ t_capture 기록<br/>(AudioWorker.cpp ProcessAudioInput)
    AW->>RB: 링버퍼 쓰기 (Mutex)
    AW-->>MW: AudioDataReady (signal)
    MW->>RB: 스냅샷 t_capture·GT (HandleInputData)
    Note over MW: ◆ t_procStart (ProcessSamples 진입)
    rect rgb(225,238,255)
    Note over AW,MW: 구간① cap2proc_latency_ms (A-2) = t_procStart − t_capture
    end
    MW->>TG: tg_process(block)
    TG-->>MW: events A/C, bph
    Note over MW: ◆ A/C ↔ 정답(GT) 대조<br/>onset_err·peak_err (E-2) / a_match·c_match (G-2)
    MW->>UI: replot / DrawImage
    Note over MW: ◆ t_disp (표시 완료)
    rect rgb(255,238,225)
    Note over MW,UI: 구간② proc2disp_latency_ms (A-2) = t_disp − t_procStart
    end
    rect rgb(225,255,225)
    Note over AW,UI: 구간③ e2e_latency_ms (A-1, Live) = t_disp − t_capture  ★전체★
    end
    MW->>LOG: 구간①②값 + backlog 기록
    Note over MW,UI: ⏳ replot은 rpQueuedReplot(지연) → 실제 페인트는 이벤트루프 다음 틱
    UI-->>MW: afterReplot (실제 그려짐) → OnScopeReplotted
    rect rgb(245,225,255)
    Note over MW,UI: 구간②-b disp_paint_ms = afterReplot − replot요청
    end
    rect rgb(225,255,235)
    Note over AW,UI: ★ e2e_full_ms (진짜 종단간) = 캡처 → 실제 페인트 = 구간①+②+②-b
    end
    MW->>LOG: disp_paint_ms · e2e_full_ms 기록
```
> ✅ 종단간을 **두 구간으로 나눠 둘 다 측정**한다: `e2e_latency_ms`(하한=요청까지) + `disp_paint_ms`(미뤄진 페인트) = **`e2e_full_ms`(진짜 종단간)**.
> **QA-LT-01(≤50ms) 판정은 `e2e_full_ms` 로** 한다. (afterReplot = ScopePlot 실제 그리기 완료 신호)

### 주기적 측정 (비트 흐름과 무관 · 타이머/집계)
```mermaid
flowchart TB
    subgraph TMR["⏱ 타이머 기반"]
      direction LR
      T1["QTimer 1Hz<br/>SamplePerfResources()"] --> M1["◆ cpu_percent (C-1)<br/>◆ rss_bytes (D-1)<br/>◆ throttled_flag (C-2, Pi)"]
      T2["QTimer 0.1s<br/>SamplePerfUiResponsiveness()"] --> M2["◆ ui_loop_lag_ms (A-3)"]
    end
    subgraph AGG["📊 2초 집계"]
      direction LR
      A1["AudioWorker 2초<br/>ProcessAudioInput()"] --> B1["◆ bg_sps/fps/spf (B-3)<br/>◆ capture_gap (B-1, Live)"]
      A2["ProcessSamples 2초"] --> B2["◆ fg_sps/fps/spf (B-3)"]
    end
    TMR --> LOG[("perf_log.csv")]
    AGG --> LOG
```

### 측정값 → 코드 위치 색인
| 로그 metric | section·qa | 측정되는 코드 위치(파일 · 함수) | 무엇을 측정 |
|-------------|-----------|--------------------------------|-------------|
| `e2e_latency_ms` | A-1·QA-LT-01 | `MainWindow.cpp` `ProcessSamples`(replot 요청 직후) ← 캡처 `AudioWorker.cpp` `ProcessAudioInput` | 종단간 **하한**(요청까지, Live) |
| `e2e_full_ms` | A-1·QA-LT-01 | `MainWindow.cpp` `OnScopeReplotted`(afterReplot) | ★**진짜 종단간**: 캡처→실제 픽셀(Live) |
| `cap2proc_latency_ms` | A-2·QA-LT-01 | `MainWindow.cpp` `ProcessSamples`(처리 시작) | 구간①: 캡처→처리(Live) |
| `proc2disp_latency_ms` | A-2·QA-LT-01 | `MainWindow.cpp` `ProcessSamples`(replot 요청) | 구간②: 처리→요청 |
| `disp_paint_ms` | A-2·QA-LT-01 | `MainWindow.cpp` `OnScopeReplotted`(afterReplot) | 구간②-b: 요청→실제 페인트 |
| `paint_fps` | F-1·QA-SC-01 | `MainWindow.cpp` `OnScopeReplotted`(1초 집계) | 실제 화면 갱신율 (**frame drop**) |
| `backlog_samples` | A-2·QA-LT-01 | `MainWindow.cpp` `ProcessSamples`(진입) | 미처리 대기량 |
| `ui_loop_lag_ms` | A-3·QA-RT-01 | `MainWindow.cpp` `SamplePerfUiResponsiveness`(0.1s 타이머) | UI 응답성 |
| `fault_sync_lost`·`detector_reset` | A-4·QA-US-01 | `MainWindow.cpp` `ProcessSamples`(tg_process 직후) | 결함 이벤트 시각 |
| `capture_gap_samples`·`_growth` | B-1·QA-RT-02 | `AudioWorker.cpp` `ProcessAudioInput`(2s) | 캡처 드롭 추정(Live) |
| `audio_xrun`·`audio_state` | B-1·QA-RT-02 | `AudioWorker.cpp` `ProcessAudioInput`/`stateChangeAudioInput` | 장치 직접보고 캡처오류(Live) |
| `bg_sps/fps/spf` | B-3·QA-RT-02 | `AudioWorker.cpp` `ProcessAudioInput`(2s) | 캡처 처리량(Live) |
| `fg_sps/fps/spf` | B-3·QA-RT-01 | `MainWindow.cpp` `ProcessSamples`(2s) | 전경 처리량 |
| `dsp_hpf/env/detect/sync/total_ms` | B-4·QA-RT-01 | `Timegrapher.cpp` `tg_process`(단계별, 1s 집계) | ★신호처리 단계별 시간 |
| `cpu_percent` | C-1·QA-EE-01 | `MainWindow.cpp` `SamplePerfResources`(1Hz) | CPU% |
| `throttled_flag` | C-2·QA-EE-01 | `MainWindow.cpp` `SamplePerfResources` → `PerfInstrumentation` `readThrottled` | Pi 스로틀 |
| `rss_bytes` | D-1·QA-RT-03 | `MainWindow.cpp` `SamplePerfResources`(1Hz) | 메모리 |
| `onset_err_ms` | E-2·QA-AC-02 | `MainWindow.cpp` `ProcessSamples`(A 이벤트, GT 대조) | A onset 정밀도(Sim) |
| `peak_err_ms` | E-2·QA-AC-02 | `MainWindow.cpp` `ProcessSamples`(C 이벤트, GT 대조) | C peak 정밀도(Sim) |
| `rate_err_s_per_d`·`beaterr_err_ms`·`amp_err_deg` | G-1·QA-CO-01 | `MainWindow.cpp` `DisplayResults` | 측정값−설정값(Sim) |
| `a_match`·`c_match`·`gt_total` | G-2·QA-AC-01 | `MainWindow.cpp` `ProcessSamples`+`DisplayResults` | 검출률(Sim) |

> 정답값(GT)은 `SimWorker.cpp` `StartSim` 에서 적재 → `SharedAudio.h` `GtBeats[]` → `MainWindow.cpp` `HandleInputData` 에서 스냅샷.
> 계측 공통 인프라(시계·CPU·RSS·스로틀·CSV, **Win/Pi 분기**): `PerfInstrumentation.{h,cpp}`.
> **정확한 라인 단위 추적**은 → [PERF_CODE_MAP.md](PERF_CODE_MAP.md). **로그 줄 해석**은 → [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md).

### 모드별로 채워지는 것
| 모드 | 채워짐 | 빔(정상) |
|------|--------|----------|
| **Sim** | A-2(`backlog`·`proc2disp`)·A-3·A-4·B-3(`fg_*`)·C·D·**E·G** | A-1(`e2e`)·A-2(`cap2proc`)·B-1·B-3(`bg_*`) — 모두 Live 전용 |
| **Live** | A-1·A-2(전체)·A-3·A-4·B(전체: `bg_*`·`fg_*`·`capture_gap`)·C·D | E·G (정답값 없음) |

> 이유: `cap2proc`·`e2e` 는 코드에서 `if(perfIsLive)` 로 묶여 Live에서만 기록되고, `bg_*`·`capture_gap` 는 `AudioWorker`(Live 캡처)에만 있다. (실측 Sim 로그에서도 `cap2proc`·`e2e`·`bg_*`·`capture_gap` 는 비어 있음 = 정상)

---

### (선택) 진짜로 "한 화면 HTML"이 필요하면
모든 `.md`(Mermaid 포함)를 **단일 HTML** 로 합쳐 브라우저에서 한 번에 보고 싶다면 알려주세요.
`docs/index.html` 을 만들어 README·CodeAnalysis·Automated·Sequence를 탭/스크롤 하나로 묶어 드립니다.
