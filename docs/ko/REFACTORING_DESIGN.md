# TimeGrapher 아키텍처 재구조화 설계서 (AS-IS / TO-BE)

> **목적**: TimeGrapher(기계식 시계 타이밍 분석기, Qt6/C++)를 SOLID·낮은결합·높은응집·관심사분리(SoC) 원칙에 맞게 재구조화하기 위한 설계 청사진.
> **AS-IS 기준선**: `origin/main`(=master, `cb258f2`) — 루트 평면 배치 모놀리식 35개 소스.
> **TO-BE 목표**: 모듈별 디렉터리(`core/audio/engine/render/ui/perf`)로 분리된 계층형 구조.
> **실행**: 본 문서 합의 후, 동작 보존(behavior-preserving) 리팩토링으로 진행. 각 변경은 빌드 검증(EXIT=0) + 로컬 커밋.
>
> 참고: 현재 작업 브랜치 `performance_test_temp`는 이미 `tabs/`(디스플레이 탭)와 `PerfInstrumentation`을 분리한 상태다. 따라서 실제 코드 수술은 temp에 적용되며, 본 문서는 "master(모놀리식) → 최종 목표"의 전체 여정을 기술한다.

---

## 0. 요약 (Executive Summary)

| 구분 | AS-IS (master) | TO-BE (목표) |
|---|---|---|
| 레이아웃 | 루트에 35개 소스 평면 배치 | `src/` 아래 6계층 디렉터리 |
| MainWindow | **God Object** 1813줄, 11개 책임, 멤버 44개 | 슬림 컨트롤러(조립만) |
| 오디오 입력 | `#ifdef` 플랫폼 분기, 소스 3종 공통 인터페이스 없음, 링버퍼 쓰기 3중복 | `IAudioSource`/`IAudioBackend` 추상화, 링버퍼 1곳 |
| DSP/검출 | 코어는 순수 C(양호) / `Timegrapher` 오케스트레이터 God class | 파이프라인·검출·타이밍 책임 분리 |
| 렌더링 | `SoundImageRenderer`가 신호조건화+렌더링 혼합 | `SignalNormalizer`(계산) ↔ Renderer(표현) 분리 |
| 디스플레이 | 전부 MainWindow/.ui에 인라인 | (temp에서 이미) `tabs/` + `TabView` 추상화 |
| 성능 계측 | 없음 | `IPerfSink` 인터페이스 뒤로 격리 |

---

## 1. AS-IS 아키텍처 현황

### 1.1 물리적 구조 (master)
루트 디렉터리에 35개 소스가 계층 없이 평면 배치되어 있다. 레이어(입력/처리/표현) 구분이 파일 시스템상 전혀 드러나지 않는다.

```
architect_cmu/
├── MainWindow.{cpp,h,ui}        ← UI + 오케스트레이션 (God Object)
├── AudioWorker, SimWorker, PlaybackWorker   ← 입력 소스 3종 (공통 추상화 없음)
├── WindowsAudio, LinuxAudio     ← 플랫폼별 장치 제어 (#ifdef 선택)
├── WatchSynthStream, WavStreamWriter, WaveHeader, SharedAudio
├── Timegrapher, Detector, Dsp, Bph          ← 코어 DSP/검출 (순수 C)
├── RollingAverage, RollingLeastSquares       ← 통계 유틸
├── SoundImageRenderer, SoundImageWidget      ← 폴딩 렌더링
└── (qcustomplot)
```

### 1.2 의존 관계 (AS-IS)
모든 화살표가 `MainWindow`로 수렴한다 — 전형적 God Object 중심 방사형 구조.

```
                    ┌────────────────────────┐
                    │      MainWindow        │  ← 모든 책임이 여기로
                    │  (UI·스레드·DSP·렌더·   │
                    │   파일·설정·측정계산)   │
                    └───────────┬────────────┘
        ┌──────────┬───────────┼───────────┬──────────┐
        ▼          ▼           ▼           ▼          ▼
   AudioWorker  SimWorker  Timegrapher  SoundImage  WavStream
   Playback…    (구상 의존, 인터페이스 없음)  Renderer   Writer
        │          │
        ▼          ▼
   WindowsAudio / LinuxAudio  (#ifdef)
```

**핵심**: MainWindow가 10개+ 구상 클래스를 직접 `#include`·`new` 한다. 추상화(인터페이스) 부재 → 교체·테스트·확장 불가.

---

## 2. 코드 스멜 분석 (SOLID 위반)

### 2.1 MainWindow — God Object (SRP·DIP·OCP 위반, 심각)

**한 클래스에 11개 책임 혼재** (1813줄, 멤버 44개):

| # | 책임 | 근거 |
|---|---|---|
| A | UI 구성·위젯 관리 | `CreateGraphs()` 등 |
| B | 오디오 장치·샘플레이트 제어 | `LoadAudioDevices()`, `PopulateSampleRates()` |
| C | 스레딩 오케스트레이션 (QThread 3개 직접 생성·시그널 배선) | `StartAudioThread/Playback/Sim` |
| D | DSP 오케스트레이션·이벤트 라우팅 (`tg_init/process/destroy` 직접 호출) | `HandleInputData()` |
| E | 메인 스코프 렌더링 (QCustomPlot 직접 조작) | `ProcessSamples()` 내부 |
| F | 측정 계산 (rate/beat/amplitude DSP) | `ComputeRateError()`(89줄), `ComputeBeatError()`, `ComputeAmplitude()` |
| G | 파일 I/O (WAV 녹음·재생 검증) | `OpenFile()`(77줄), `RecordSessionCheck()` |
| H | 설정·상태 관리 (20+ 스칼라) | `mLiftAngle`, `mAveragingPeriod` 등 |
| I | 탭/모듈 조정 | (temp) `RegisterDisplayTabs()` |
| J | 성능 계측 | (temp) perf 멤버 10+ |
| K | GUI 모드 관리 | `SetGuiRunMode/StopMode()` |

- **거대 메서드**: `ProcessSamples()` **246줄, 중첩 5단계** — 버퍼링·WAV쓰기·렌더링·DSP호출·이벤트마킹·탭브로드캐스트·그래프렌더·perf로깅을 한 메서드에서 처리.
- **DIP 위반**: `MainWindow.h`가 `AudioWorker/PlaybackWorker/SimWorker/WavStreamWriter/Timegrapher/SoundImageRenderer/qcustomplot` 등 **구상 타입 10+개 직접 의존**. 인터페이스 0개. → 워커 교체·DSP 모킹·렌더백엔드 교체 불가.
- **OCP 위반**: 워커·(temp)탭을 하드코딩 `new`. 신규 소스/탭 추가 시 MainWindow 수정 필수.

### 2.2 오디오 I/O — 추상화 부재 + 중복 (DIP·OCP·SRP·ISP 위반)

- **플랫폼 선택이 `#ifdef`** (`MainWindow.cpp` `ConfigureSoundCard()`): `WindowsSetSoundParameters(...)` vs `LinuxSetSoundParameters(...)`. 시그니처도 다름(Linux는 `agc_name` 인자 추가). 공통 인터페이스 없음 → 신규 플랫폼 추가 시 새 `#ifdef` 블록 + MainWindow 재컴파일.
- **소스 3종(capture/playback/sim)에 공통 인터페이스 없음**: `AudioWorker`·`PlaybackWorker`·`SimWorker`가 병렬 구상 구현. MainWindow가 3개 개별 핸들러(`HandleAudioInput/…`)로 분기 — 다형성 없음.
- **링버퍼 쓰기 로직 3중복**: 동일 memcpy 패턴이 `AudioWorker.cpp:97-110`, `PlaybackWorker.cpp:150-163`, `SimWorker.cpp:96-107`에 복붙.
- **`SharedAudio`(TMasterAudioDataRaw)가 God struct**: 오디오 샘플 + 링버퍼 메타 + perf 필드 + GT 이벤트 링(sim 전용) + 뮤텍스를 한 구조체에 혼재.
- **워커 부수 책임**: `AudioWorker`가 캡처 외에 FPS/SPS 통계·drop 추정·perf 로깅까지 수행(SRP).

### 2.3 DSP/검출 — 코어는 양호, 오케스트레이터에 책임 과중 (SRP)

- ✅ **`Dsp`/`Detector`/`Bph`는 순수 C** — Qt·UI·오디오 의존 0. 단위테스트·재사용 가능(좋은 설계).
- ❌ **`Timegrapher`(735줄)가 God 오케스트레이터**: 버퍼수명 + 파이프라인배선 + 이벤트히스토리 + BPH검출 + sync(PLL) + regime리셋 + 이벤트포맷 + perf집계를 한 곳에서. `tg_process()` 359줄.
- ❌ **`Detector`(970줄)**: 임계값계산 + 상태머신 + regime검출(별도 미니 상태머신) + 타이밍보정 혼재. `tg_detector_process()` 344줄.
- ❌ **BPH 피커 분산**: `phase_score`는 `Bph.cpp`, 후보 스윕·median 가드는 `Timegrapher.cpp`에 흩어짐.
- ❌ **매직넘버 25+개**: `0.4*period`, `0.7*period`, `0.03`, `0.7`(임계), `200Hz`, `50ms` 등 헤더·코드에 산재.
- ❌ **perf 코드가 비즈니스 로직에 inline 침투**: `tg_process()` 안에 `Perf::nowMs()`/`Perf::log()` 6+곳 + 정적 1초 집계 루프. (temp 한정)

### 2.4 렌더링 — 계산과 표현 혼합 (SoC 위반)

- ❌ **`SoundImageRenderer`(1042줄)가 신호조건화 + 렌더링 혼합**: `processSamples()`(57줄) 안에서 **DC제거(EMA)·피크정규화(peak-hold+감쇠)** 를 직접 수행 후 폴딩. 신호조건화는 표현이 아니라 비즈니스 로직.
- ❌ **DSP 중복 구현**: `Dsp.cpp`의 `tg_hpf_*`/`tg_envelope_*`(1극 IIR)와 유사한 DC블록·엔벨로프를 Renderer가 독자 재구현.
- ❌ **Widget/Renderer 의존 역전**: `SoundImageRenderer`가 위젯의 `QImage`를 직접 수정하고 재그리기를 암묵 트리거. Widget은 수동. → 책임 경계 모호.
- ⚠️ **높은 내부 복잡도**: 멤버 30+개, 3개 하위 상태머신(warmup→anchor버퍼링→정상렌더) + 중심정렬(dominant-band 탐색).

### 2.5 디스플레이 탭 — (temp에서 이미 개선됨)

- ✅ `TabView` 추상화, `TabManager` 브로드캐스트, `WaveBuffer`/`ReadoutBar`/`ScopeRender`/`LegendBox` 재사용 헬퍼. **양호.**
- ⚠️ 단 MainWindow가 10개 탭을 직접 `new` → OCP. (master에는 이 분리 자체가 없음 — 디스플레이가 전부 인라인.)

---

## 3. TO-BE 아키텍처

### 3.1 목표 디렉터리 구조

상위 6계층 (의존 방향: 위에서 아래로만, 역방향 의존 금지):

```
src/
├── core/      순수 로직   (Qt·UI 의존 0)
├── audio/     입력·출력   (DIP 인터페이스)
├── engine/    오케스트레이션
├── render/    표현(그리기)
├── ui/        Qt UI
└── perf/      성능 계측   (횡단 관심사)
```

계층별 상세:

#### `core/` — 순수 로직 (Qt·UI 의존 0, 단위테스트 가능)
| 디렉터리 | 책임 | 포함 모듈 |
|---|---|---|
| `core/dsp/` | 신호 필터 | `IIRFilters` (HPF·envelope·MovingAverage 통합 — 중복 제거) |
| `core/detection/` | 이벤트 검출 | `Detector`(상태머신) + `ThresholdCalibrator` + `RegimeDetector` |
| `core/timing/` | 타이밍·동기 | `Bph`, `SyncTracker`(PLL), `Pipeline`(슬림화된 Timegrapher) |
| `core/stats/` | 통계 유틸 | `RollingAverage`, `RollingLeastSquares` |

#### `audio/` — 입력·출력 (DIP)
| 디렉터리 | 책임 | 포함 모듈 |
|---|---|---|
| `audio/` (루트) | 공통 추상화·버퍼 | `IAudioSource`, `AudioRingBuffer`(쓰기 단일화: 3중복 → 1) |
| `audio/capture/` | 실시간 캡처 | `AudioCaptureSource` + `IAudioBackend`(Windows/Linux 백엔드) |
| `audio/playback/` | 파일 재생 | `FilePlaybackSource` (구 PlaybackWorker) |
| `audio/sim/` | 합성 신호 | `SynthSource` (구 SimWorker) + `WatchSynthStream` |
| `audio/recording/` | WAV 녹음 | `WavStreamWriter`, `WaveHeader` |

#### `engine/` — 오케스트레이션 (MainWindow에서 추출)
| 모듈 | 책임 |
|---|---|
| `MeasurementEngine` | rate/beat/amplitude 계산 (구 `Compute*` 메서드 이관) |
| `CaptureController` | 스레드 수명·소스 전환·데이터 라우팅 |

#### `render/` — 표현 (계산과 분리)
| 모듈 | 책임 |
|---|---|
| `SignalNormalizer` | DC제거·피크정규화 (Renderer에서 추출한 신호조건화) |
| `SoundImageRenderer` / `SoundImageWidget` | 폴딩 이미지 그리기 (순수 표현) |

#### `ui/` — Qt UI
| 모듈 | 책임 |
|---|---|
| `MainWindow` | 슬림 컨트롤러 (조립·배선만) |
| `ui/tabs/` | 기존 디스플레이 탭 + `TabRegistry`(OCP) |
| `ui/widgets/` | `ReadoutBar`, `LegendBox` |

#### `perf/` — 성능 계측
| 모듈 | 책임 |
|---|---|
| `PerfInstrumentation` | 계측 구현을 `IPerfSink` 인터페이스 뒤로 격리 (코어 로직과 분리) |

### 3.2 핵심 추상화 (DIP)

```cpp
// audio/IAudioSource.h — capture/playback/sim을 한 인터페이스로
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual int  sampleRate() const = 0;
    // 샘플은 AudioRingBuffer로 push (공통)
};

// audio/capture/IAudioBackend.h — 플랫폼 장치 제어 (Windows/Linux/…)
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual bool configure(const AudioDeviceConfig &) = 0;
    virtual std::vector<DeviceInfo> listDevices() = 0;
};

// perf/IPerfSink.h — 계측을 비즈니스 로직에서 분리
class IPerfSink {
public:
    virtual ~IPerfSink() = default;
    virtual void log(const PerfSample &) = 0;
};
```

상위 모듈(engine/ui)은 **구상 클래스가 아닌 위 인터페이스에 의존**한다. 소스/플랫폼/계측 교체가 호출부 수정 없이 가능(OCP).

---

## 4. AS-IS → TO-BE 매핑

| AS-IS (master) | TO-BE 위치 | 변경 유형 |
|---|---|---|
| `Dsp.{cpp,h}` | `core/dsp/IIRFilters` | 이동 + MovingAverage 통합(중복 제거) |
| `Detector.{cpp,h}` | `core/detection/Detector` + `ThresholdCalibrator` + `RegimeDetector` | 이동 + **책임 분리** |
| `Bph.{cpp,h}` | `core/timing/Bph` + `SyncTracker` | 이동 + BPH 피커 통합 |
| `Timegrapher.{cpp,h}` | `core/timing/Pipeline` | 이동 + **오케스트레이션 슬림화**(버퍼/perf 분리) |
| `RollingAverage/LeastSquares` | `core/stats/` | 이동(변경 없음) |
| `AudioWorker` | `audio/capture/AudioCaptureSource` (`IAudioSource` 구현) | 이동 + 인터페이스 + 통계/perf 분리 |
| `PlaybackWorker` | `audio/playback/FilePlaybackSource` | 이동 + 인터페이스 |
| `SimWorker` | `audio/sim/SynthSource` | 이동 + 인터페이스 |
| `WindowsAudio/LinuxAudio` | `audio/capture/` (`IAudioBackend` 구현) | 이동 + 인터페이스(`#ifdef` 제거) |
| `WatchSynthStream` | `audio/sim/` | 이동 |
| `WavStreamWriter/WaveHeader` | `audio/recording/` | 이동 |
| `SharedAudio` | `audio/AudioRingBuffer` + 별도 perf/GT 구조 | **분할**(God struct 해체) |
| `MainWindow`의 `Compute*` | `engine/MeasurementEngine` | **추출** |
| `MainWindow`의 스레드 관리 | `engine/CaptureController` | **추출** |
| `SoundImageRenderer`의 DC/peak | `render/SignalNormalizer` | **추출** |
| `SoundImageRenderer/Widget` | `render/` | 이동 + 책임 경계 정리 |
| `MainWindow`(나머지) | `ui/MainWindow` | 슬림 컨트롤러로 축소 |
| (temp) `tabs/` | `ui/tabs/` + `TabRegistry` | 이동 + OCP |
| (temp) `PerfInstrumentation` | `perf/` (`IPerfSink`) | 이동 + 인터페이스 |

---

## 5. SOLID 원칙별 적용 내역

| 원칙 | AS-IS 문제 | TO-BE 적용 |
|---|---|---|
| **SRP (단일책임)** | MainWindow 11책임 / Timegrapher 7책임 / Detector 4책임 / Renderer 2책임 | MainWindow→`MeasurementEngine`·`CaptureController`·UI 분리 / Detector→`Threshold`·`Regime` 분리 / Renderer→`SignalNormalizer` 분리 / Timegrapher→`Pipeline` 슬림화 |
| **OCP (개방폐쇄)** | 워커·탭·플랫폼 하드코딩, 추가 시 기존 수정 | `AudioSourceFactory`·`TabRegistry`·`IAudioBackend`로 확장점 개방 |
| **LSP (리스코프)** | 소스 3종 치환 불가 | `IAudioSource` 구현체는 상호 치환 가능 |
| **ISP (인터페이스분리)** | `SharedAudio` God struct를 모두가 통째 의존 | 데이터 구조 분할, 소비자별 최소 인터페이스 |
| **DIP (의존역전)** | 구상 클래스 10+개 직접 의존, `#ifdef` 플랫폼 | `IAudioSource`/`IAudioBackend`/`IPerfSink` 추상화에 의존 |
| **낮은 결합** | MainWindow 방사형 결합, 링버퍼 3중복 | 인터페이스 경계 + `AudioRingBuffer` 단일화 |
| **높은 응집** | 한 메서드 246줄에 6관심사 | 모듈당 단일 관심사, 메서드 추출 |
| **SoC / 디렉터리** | 35개 평면 배치 | `core/audio/engine/render/ui/perf` 계층 분리 |

---

## 6. 리스크 및 비고

- **작업 원칙**: 동작 보존(behavior-preserving) 리팩토링. 변경은 "이동 → 인터페이스 도입 → 로직 분리" 순서로, 외부 동작은 바꾸지 않는다. 각 변경 후 `cmake --build build_cli --target TimeGrapher` → **EXIT=0 확인 후에만** 커밋. 파일 이동은 `git mv`로 이력 보존. **rebase 금지**(공유 브랜치), 최종 main 머지는 merge commit.
- **빌드 시스템**: `CMakeLists.txt`의 소스 목록을 변경에 맞춰 갱신. `qcustomplot`은 외부 라이브러리이므로 이동 대상에서 제외(루트 또는 `third_party/` 유지).
- **C ABI 안정성**: `Timegrapher.h`는 C/Python/Rust 바인딩용 `extern "C"` API. 슬림화 시 **공개 API 시그니처는 보존**(내부만 분리).
- **순수성 유지**: `core/`는 Qt 의존이 들어가지 않도록 엄격히 통제(perf 호출은 `IPerfSink` 주입으로만).
- **플랫폼 백엔드**: Windows(COM/MMDevice)·Linux(ALSA)는 API가 근본적으로 달라 코드 통합이 아닌 **공통 인터페이스 뒤 격리**가 목표(중복 "제거"가 아니라 "경계화").
- **AS-IS 라인번호**: 본 문서의 `file:line`은 분석 시점 기준이며 리팩토링 진행에 따라 이동한다.

---

*설계 합의용 문서. 승인 후 `core/` 이동부터 착수한다.*
