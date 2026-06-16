# TimeGrapher Code Analysis — Unified Index

> **See the whole code structure from this one file.** Open it with `Ctrl+Shift+V` (preview) to render the diagrams below as images,
> and jump straight to the detailed docs and auto-generated graphs via the links in the tables.
> **Performance (perf) measurement analysis has a separate index** → [PERF_ANALYSIS.md](PERF_ANALYSIS.md).

A Qt/C++ app that receives the acoustic signal of a mechanical watch and measures and visualizes **rate, amplitude, beat error, and BPH** in real time.

---

## 0. Document Map (start here)

| What you want to see | File | How to open |
|--------------|------|---------|
| **Overall overview + key diagrams** | 📍 This file | `Ctrl+Shift+V` |
| Hand-written comprehensive analysis (structure, formulas, methods) | [CodeAnalysis.md](CodeAnalysis.md) | `Ctrl+Shift+V` |
| Doxygen+CMake automated analysis results | [AutomatedAnalysis.md](AutomatedAnalysis.md) | `Ctrl+Shift+V` |
| clang-uml automated sequence diagrams | [SequenceDiagrams.md](SequenceDiagrams.md) | `Ctrl+Shift+V` |
| **Click-to-navigate full reference** (945 graphs) | `doxygen/html/index.html` | Browser |
| Build dependency graph | `doxygen/cmake_deps.svg` | Browser |
| clang-uml source diagrams | `clang-uml/*.mmd` | Mermaid preview |
| ★**Performance (perf) measurement analysis** (separate index) | [PERF_ANALYSIS.md](PERF_ANALYSIS.md) | `Ctrl+Shift+V` |

Regeneration settings/scripts are included in the original project: `docs/Doxyfile` · `.clang-uml` · `docs/clang-uml/gen_cdb.ps1` · `docs/gen_site.py` (not included in this shared copy)

---

## 1. Architecture at a Glance (3 layers + multithreaded)

```mermaid
flowchart LR
    subgraph ACQ["① Signal Acquisition (background thread)"]
        AW[TAudioWorker<br/>Microphone]
        PW[TPlaybackWorker<br/>WAV]
        SW[TSimWorker<br/>Synthesis]
        RB[(TMasterAudioDataRaw<br/>Shared ring buffer)]
        AW --> RB
        PW --> RB
        SW --> RB
    end
    subgraph PROC["② Signal Processing/Measurement (tg_* C API)"]
        TG[tg_process]
        HPF[HPF] --> ENV[Envelope] --> DET[Detector<br/>A/C detection] --> SYNC[Sync/BPH PLL]
        TG -.assemble.- HPF
    end
    subgraph GUI["③ Presentation (main/UI thread)"]
        MW[MainWindow<br/>ProcessSamples]
        RP[RatePlot]
        SP[ScopePlot]
        SI[SoundImage]
        BAR[Measurement summary bar]
    end
    RB -->|AudioDataReady signal| MW
    MW --> TG
    TG -->|events/bph| MW
    MW --> RP & SP & SI & BAR
```

---

## 2. Class Relationships at a Glance (core only)

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
> To see all members, refer to [CodeAnalysis.md §3](CodeAnalysis.md) or Doxygen `class_main_window.html`.

---

## 3. Measurement Flow at a Glance (refined clang-uml extraction)

```mermaid
sequenceDiagram
    autonumber
    participant HW as Microphone
    participant AW as TAudioWorker
    participant RB as Ring buffer
    participant MW as MainWindow
    participant TG as tg_process
    participant UI as Plot/SoundImage/Summary bar

    HW->>AW: PCM samples
    AW->>RB: lock→memcpy→unlock
    AW-->>MW: AudioDataReady (signal)
    MW->>RB: snapshot → ProcessSamples()
    loop 4096-sample chunk
        MW->>TG: tg_process(block)
        TG-->>MW: events(A/C), bph, processed_pcm
        alt A event
            MW->>MW: ComputeRateError / ComputeBeatError
        else C event
            MW->>MW: ComputeAmplitude → DisplayResults
        end
    end
    MW->>UI: replot / DrawImage / update summary bar
```
> For the complete internal calls, see [SequenceDiagrams.md](SequenceDiagrams.md) and the source [seq_process_samples.mmd](clang-uml/seq_process_samples.mmd).

---

## 4. Build Dependencies at a Glance

```mermaid
graph LR
    TimeGrapher --> QtCore & QtWidgets & QtMultimedia & QtPrintSupport
    TimeGrapher --> winmm["winmm(1ms timer)"] & Ole32 & Propsys
    QtMultimedia --> QtGui & QtNetwork
    QtWidgets --> QtGui --> QtCore
    QtCore --> Threads
```
> Source: `doxygen/cmake_deps.svg`. Interpretation in [AutomatedAnalysis.md §3](AutomatedAnalysis.md).

---

## 5. At a Glance — Usage Tips

- **This README + preview (`Ctrl+Shift+V`)** = view all four diagrams (structure/class/flow/dependencies) on one screen by scrolling.
- **If you need click-to-navigate** → `doxygen/html/index.html` (browser). Includes per-function caller/callee graphs.
- **If you need performance measurement analysis** → [PERF_ANALYSIS.md](PERF_ANALYSIS.md) (perf_log.csv ↔ code location, measurement spans).
- **Regeneration**: After code changes, run the runbook in [AutomatedAnalysis.md §5](AutomatedAnalysis.md) / [SequenceDiagrams.md §5](SequenceDiagrams.md).

---

### (Optional) If you really need a "single-screen HTML"
If you want to merge all `.md` files (including Mermaid) into a **single HTML** and view them at once in a browser, let me know.
I will create `docs/index.html` that bundles README·CodeAnalysis·Automated·Sequence into one tab/scroll.
