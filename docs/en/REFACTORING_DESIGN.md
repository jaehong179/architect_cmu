# TimeGrapher Architecture Refactoring Design (AS-IS / TO-BE)

> **Purpose**: A design blueprint for restructuring TimeGrapher (a mechanical-watch timing analyzer, Qt6/C++) according to SOLID, loose coupling, high cohesion, and Separation of Concerns (SoC).
> **AS-IS baseline**: `origin/main` (= master, `cb258f2`) — a flat, monolithic layout of 35 root-level sources.
> **TO-BE target**: a layered structure separated into module directories (`core/audio/engine/render/ui/perf`).
> **Execution**: After this document is agreed, proceed with behavior-preserving refactoring. Each change is build-verified (EXIT=0) + locally committed.
>
> Note: the working branch `performance_test_temp` already extracted `tabs/` (display tabs) and `PerfInstrumentation`. Thus the actual code surgery is applied to temp, and this document describes the full journey "monolithic master → final target".

---

## 0. Executive Summary

| Aspect | AS-IS (master) | TO-BE (target) |
|---|---|---|
| Layout | 35 sources flat in root | 6-layer directories under `src/` |
| MainWindow | **God Object** 1813 lines, 11 responsibilities, 44 members | slim controller (wiring only) |
| Audio input | `#ifdef` platform branching, no common interface across 3 sources, ring-buffer write duplicated ×3 | `IAudioSource`/`IAudioBackend` abstractions, single ring buffer |
| DSP/detection | core is pure C (good) / `Timegrapher` is a God orchestrator | detection·timing·pipeline responsibilities split |
| Rendering | `SoundImageRenderer` mixes signal conditioning + rendering | `SignalNormalizer` (compute) ↔ Renderer (presentation) split |
| Display | all inline in MainWindow/.ui | (already on temp) `tabs/` + `TabView` abstraction |
| Perf instrumentation | none | isolated behind `IPerfSink` interface |

---

## 1. AS-IS Architecture

### 1.1 Physical structure (master)
35 sources sit flat in the root directory with no hierarchy. Layer boundaries (input / processing / presentation) are not expressed in the file system at all.

```
architect_cmu/
├── MainWindow.{cpp,h,ui}        ← UI + orchestration (God Object)
├── AudioWorker, SimWorker, PlaybackWorker   ← 3 input sources (no common abstraction)
├── WindowsAudio, LinuxAudio     ← per-platform device control (#ifdef selection)
├── WatchSynthStream, WavStreamWriter, WaveHeader, SharedAudio
├── Timegrapher, Detector, Dsp, Bph          ← core DSP/detection (pure C)
├── RollingAverage, RollingLeastSquares       ← statistics utils
├── SoundImageRenderer, SoundImageWidget      ← folding renderer
└── (qcustomplot)
```

### 1.2 Dependency graph (AS-IS)
Every arrow converges on `MainWindow` — a classic God-Object-centered radial structure.

```
                    ┌────────────────────────┐
                    │      MainWindow        │  ← all responsibilities land here
                    │  (UI·threads·DSP·render·│
                    │   files·settings·measure)│
                    └───────────┬────────────┘
        ┌──────────┬───────────┼───────────┬──────────┐
        ▼          ▼           ▼           ▼          ▼
   AudioWorker  SimWorker  Timegrapher  SoundImage  WavStream
   Playback…    (concrete deps, no interface)    Renderer   Writer
        │          │
        ▼          ▼
   WindowsAudio / LinuxAudio  (#ifdef)
```

**Core issue**: MainWindow directly `#include`s and `new`s 10+ concrete classes. No abstractions → cannot swap, test, or extend.

---

## 2. Code Smell Analysis (SOLID violations)

### 2.1 MainWindow — God Object (SRP·DIP·OCP, critical)

**11 responsibilities mixed in one class** (1813 lines, 44 members):

| # | Responsibility | Evidence |
|---|---|---|
| A | UI construction / widget management | `CreateGraphs()` etc. |
| B | Audio device / sample-rate control | `LoadAudioDevices()`, `PopulateSampleRates()` |
| C | Threading orchestration (creates 3 QThreads directly, wires signals) | `StartAudioThread/Playback/Sim` |
| D | DSP orchestration / event routing (`tg_init/process/destroy` directly) | `HandleInputData()` |
| E | Main scope rendering (QCustomPlot directly) | inside `ProcessSamples()` |
| F | Measurement computation (rate/beat/amplitude DSP) | `ComputeRateError()`(89 lines), `ComputeBeatError()`, `ComputeAmplitude()` |
| G | File I/O (WAV record/playback validation) | `OpenFile()`(77 lines), `RecordSessionCheck()` |
| H | Settings / state management (20+ scalars) | `mLiftAngle`, `mAveragingPeriod` etc. |
| I | Tab / module coordination | (temp) `RegisterDisplayTabs()` |
| J | Performance instrumentation | (temp) 10+ perf members |
| K | GUI mode management | `SetGuiRunMode/StopMode()` |

- **Giant method**: `ProcessSamples()` is **246 lines, 5 levels of nesting** — buffering, WAV writing, rendering, DSP calls, event marking, tab broadcast, graph rendering, and perf logging all in one method.
- **DIP violation**: `MainWindow.h` directly depends on **10+ concrete types** (`AudioWorker/PlaybackWorker/SimWorker/WavStreamWriter/Timegrapher/SoundImageRenderer/qcustomplot`). Zero interfaces. → cannot swap workers, mock DSP, or replace the render backend.
- **OCP violation**: workers and (temp) tabs are hard-coded with `new`. Adding a new source/tab requires editing MainWindow.

### 2.2 Audio I/O — missing abstraction + duplication (DIP·OCP·SRP·ISP)

- **Platform selection via `#ifdef`** (`MainWindow.cpp` `ConfigureSoundCard()`): `WindowsSetSoundParameters(...)` vs `LinuxSetSoundParameters(...)`. Signatures even differ (Linux adds an `agc_name` argument). No common interface → adding a platform means a new `#ifdef` block + recompiling MainWindow.
- **No common interface across the 3 sources (capture/playback/sim)**: `AudioWorker`·`PlaybackWorker`·`SimWorker` are parallel concrete implementations. MainWindow branches into 3 separate handlers (`HandleAudioInput/…`) — no polymorphism.
- **Ring-buffer write duplicated ×3**: the same memcpy pattern is copy-pasted in `AudioWorker.cpp:97-110`, `PlaybackWorker.cpp:150-163`, `SimWorker.cpp:96-107`.
- **`SharedAudio` (TMasterAudioDataRaw) is a God struct**: audio samples + ring-buffer metadata + perf fields + ground-truth event ring (sim-only) + mutex all crammed into one struct.
- **Worker side responsibilities**: `AudioWorker` also computes FPS/SPS statistics, drop estimation, and perf logging beyond capture (SRP).

### 2.3 DSP/detection — core is good, orchestrator is overloaded (SRP)

- ✅ **`Dsp`/`Detector`/`Bph` are pure C** — zero Qt/UI/audio dependency. Unit-testable and reusable (good design).
- ❌ **`Timegrapher` (735 lines) is a God orchestrator**: buffer lifecycle + pipeline wiring + event history + BPH detection + sync (PLL) + regime reset + event formatting + perf aggregation, all in one place. `tg_process()` is 359 lines.
- ❌ **`Detector` (970 lines)**: threshold computation + state machine + regime detection (a separate mini state machine) + timing refinement mixed together. `tg_detector_process()` is 344 lines.
- ❌ **BPH picker scattered**: `phase_score` lives in `Bph.cpp`, while the candidate sweep and median guard live in `Timegrapher.cpp`.
- ❌ **25+ magic numbers**: `0.4*period`, `0.7*period`, `0.03`, `0.7`(threshold), `200Hz`, `50ms`, etc. scattered across headers and code.
- ❌ **Perf code tangled into business logic**: `tg_process()` contains 6+ `Perf::nowMs()`/`Perf::log()` calls + a static 1-second aggregation loop. (temp only)

### 2.4 Rendering — compute mixed with presentation (SoC)

- ❌ **`SoundImageRenderer` (1042 lines) mixes signal conditioning + rendering**: inside `processSamples()` (57 lines) it performs **DC removal (EMA) and peak normalization (peak-hold + decay)** directly, then folds. Signal conditioning is business logic, not presentation.
- ❌ **DSP reimplemented**: it independently reimplements DC-block/envelope (one-pole IIR) that already exist as `tg_hpf_*`/`tg_envelope_*` in `Dsp.cpp`.
- ❌ **Inverted Widget/Renderer dependency**: `SoundImageRenderer` writes the widget's `QImage` directly and implicitly triggers redraws; the Widget is passive → blurred responsibility boundary.
- ⚠️ **High internal complexity**: 30+ members, 3 sub-state-machines (warmup → anchor buffering → normal render) + centering (dominant-band search).

### 2.5 Display tabs — (already improved on temp)

- ✅ `TabView` abstraction, `TabManager` broadcast, and reusable helpers (`WaveBuffer`/`ReadoutBar`/`ScopeRender`/`LegendBox`). **Good.**
- ⚠️ But MainWindow `new`s 10 tabs directly → OCP. (master has no such separation at all — display is fully inline.)

---

## 3. TO-BE Architecture

### 3.1 Target directory structure

Top-level 6 layers (dependencies flow top→bottom only; no reverse dependencies):

```
src/
├── core/      pure logic    (zero Qt/UI dependency)
├── audio/     input/output  (DIP interfaces)
├── engine/    orchestration
├── render/    presentation (drawing)
├── ui/        Qt UI
└── perf/      performance instrumentation (cross-cutting)
```

Per-layer detail:

#### `core/` — pure logic (zero Qt/UI dependency, unit-testable)
| Directory | Responsibility | Modules |
|---|---|---|
| `core/dsp/` | signal filters | `IIRFilters` (HPF·envelope·MovingAverage unified — dedup) |
| `core/detection/` | event detection | `Detector`(state machine) + `ThresholdCalibrator` + `RegimeDetector` |
| `core/timing/` | timing/sync | `Bph`, `SyncTracker`(PLL), `Pipeline`(slimmed Timegrapher) |
| `core/stats/` | statistics utils | `RollingAverage`, `RollingLeastSquares` |

#### `audio/` — input/output (DIP)
| Directory | Responsibility | Modules |
|---|---|---|
| `audio/` (root) | common abstraction·buffer | `IAudioSource`, `AudioRingBuffer` (single writer: ×3 dup → 1) |
| `audio/capture/` | live capture | `AudioCaptureSource` + `IAudioBackend` (Windows/Linux backends) |
| `audio/playback/` | file playback | `FilePlaybackSource` (was PlaybackWorker) |
| `audio/sim/` | synthetic signal | `SynthSource` (was SimWorker) + `WatchSynthStream` |
| `audio/recording/` | WAV recording | `WavStreamWriter`, `WaveHeader` |

#### `engine/` — orchestration (extracted from MainWindow)
| Module | Responsibility |
|---|---|
| `MeasurementEngine` | rate/beat/amplitude computation (was `Compute*` methods) |
| `CaptureController` | thread lifecycle·source switching·data routing |

#### `render/` — presentation (separated from compute)
| Module | Responsibility |
|---|---|
| `SignalNormalizer` | DC removal·peak normalization (signal conditioning extracted from the Renderer) |
| `SoundImageRenderer` / `SoundImageWidget` | folded-image drawing (pure presentation) |

#### `ui/` — Qt UI
| Module | Responsibility |
|---|---|
| `MainWindow` | slim controller (assembly·wiring only) |
| `ui/tabs/` | existing display tabs + `TabRegistry` (OCP) |
| `ui/widgets/` | `ReadoutBar`, `LegendBox` |

#### `perf/` — performance instrumentation
| Module | Responsibility |
|---|---|
| `PerfInstrumentation` | isolate the instrumentation implementation behind `IPerfSink` (decoupled from core logic) |

### 3.2 Key abstractions (DIP)

```cpp
// audio/IAudioSource.h — unify capture/playback/sim behind one interface
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool start() = 0;
    virtual void stop()  = 0;
    virtual int  sampleRate() const = 0;
    // samples are pushed to AudioRingBuffer (shared)
};

// audio/capture/IAudioBackend.h — platform device control (Windows/Linux/…)
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual bool configure(const AudioDeviceConfig &) = 0;
    virtual std::vector<DeviceInfo> listDevices() = 0;
};

// perf/IPerfSink.h — decouple instrumentation from business logic
class IPerfSink {
public:
    virtual ~IPerfSink() = default;
    virtual void log(const PerfSample &) = 0;
};
```

Upper modules (engine/ui) **depend on the interfaces above, not concrete classes**. Swapping source/platform/instrumentation requires no edits to callers (OCP).

---

## 4. AS-IS → TO-BE Mapping

| AS-IS (master) | TO-BE location | Change type |
|---|---|---|
| `Dsp.{cpp,h}` | `core/dsp/IIRFilters` | move + unify MovingAverage (dedup) |
| `Detector.{cpp,h}` | `core/detection/Detector` + `ThresholdCalibrator` + `RegimeDetector` | move + **split responsibilities** |
| `Bph.{cpp,h}` | `core/timing/Bph` + `SyncTracker` | move + consolidate BPH picker |
| `Timegrapher.{cpp,h}` | `core/timing/Pipeline` | move + **slim orchestration** (extract buffer/perf) |
| `RollingAverage/LeastSquares` | `core/stats/` | move (no change) |
| `AudioWorker` | `audio/capture/AudioCaptureSource` (implements `IAudioSource`) | move + interface + extract stats/perf |
| `PlaybackWorker` | `audio/playback/FilePlaybackSource` | move + interface |
| `SimWorker` | `audio/sim/SynthSource` | move + interface |
| `WindowsAudio/LinuxAudio` | `audio/capture/` (implements `IAudioBackend`) | move + interface (remove `#ifdef`) |
| `WatchSynthStream` | `audio/sim/` | move |
| `WavStreamWriter/WaveHeader` | `audio/recording/` | move |
| `SharedAudio` | `audio/AudioRingBuffer` + separate perf/GT struct | **split** (dismantle God struct) |
| MainWindow's `Compute*` | `engine/MeasurementEngine` | **extract** |
| MainWindow's thread mgmt | `engine/CaptureController` | **extract** |
| `SoundImageRenderer`'s DC/peak | `render/SignalNormalizer` | **extract** |
| `SoundImageRenderer/Widget` | `render/` | move + clarify boundary |
| `MainWindow` (rest) | `ui/MainWindow` | shrink to slim controller |
| (temp) `tabs/` | `ui/tabs/` + `TabRegistry` | move + OCP |
| (temp) `PerfInstrumentation` | `perf/` (`IPerfSink`) | move + interface |

---

## 5. SOLID Application by Principle

| Principle | AS-IS problem | TO-BE application |
|---|---|---|
| **SRP** | MainWindow 11 / Timegrapher 7 / Detector 4 / Renderer 2 responsibilities | MainWindow → `MeasurementEngine`·`CaptureController`·UI split / Detector → `Threshold`·`Regime` split / Renderer → `SignalNormalizer` split / Timegrapher → `Pipeline` slimmed |
| **OCP** | workers·tabs·platforms hard-coded; adding requires edits | `AudioSourceFactory`·`TabRegistry`·`IAudioBackend` open the extension points |
| **LSP** | the 3 sources are not substitutable | `IAudioSource` implementations are mutually substitutable |
| **ISP** | everyone depends on the whole `SharedAudio` God struct | split the data struct; minimal per-consumer interfaces |
| **DIP** | 10+ concrete dependencies, `#ifdef` platforms | depend on `IAudioSource`/`IAudioBackend`/`IPerfSink` abstractions |
| **Loose coupling** | MainWindow radial coupling, ring-buffer ×3 dup | interface boundaries + single `AudioRingBuffer` |
| **High cohesion** | 6 concerns in one 246-line method | single concern per module, methods extracted |
| **SoC / directories** | 35 sources flat | `core/audio/engine/render/ui/perf` layer separation |

---

## 6. Risks & Notes

- **Working principle**: behavior-preserving refactoring. Changes follow "move → introduce interface → split logic" and do not alter external behavior. After each change, run `cmake --build build_cli --target TimeGrapher` → commit **only after EXIT=0**. Move files with `git mv` to preserve history. **No rebase** (shared branch); the final merge to main is a merge commit.
- **Build system**: update the source list in `CMakeLists.txt` as changes proceed. `qcustomplot` is a third-party library and is excluded from moves (kept in root or `third_party/`).
- **C ABI stability**: `Timegrapher.h` is an `extern "C"` API for C/Python/Rust bindings. When slimming, **preserve the public API signatures** (split internals only).
- **Keep core pure**: strictly prevent Qt dependencies from entering `core/` (perf calls only via injected `IPerfSink`).
- **Platform backends**: Windows (COM/MMDevice) and Linux (ALSA) have fundamentally different APIs; the goal is **isolation behind a common interface**, not literal code merging (boundary, not "dedup").
- **AS-IS line numbers**: `file:line` references are as of analysis time and shift as refactoring proceeds.

---

*Design agreement document. Once approved, work begins with the `core/` move.*
