# Presentation Slides — Architecture & Performance

> TimeGrapher · Real-time mechanical-watch diagnostic app · measured on Raspberry Pi

---

# Part 1. Architecture

---

## Slide 1 — Layered Structure (module relationships)

**One-way dependency — upper layers know lower layers, not vice versa**

```
[UI layer]      TabManager · TabView · 13 tabs
    ↑ (measurement broadcast)
[Engine layer]  CaptureController · MeasurementEngine
    ↑
[DSP layer]     tg_process (detection compute)
    ↑
[Capture layer] Audio · Playback · Sim workers (separate thread)
```

| Property | Detail |
|---|---|
| Dependency direction | Always upper→lower, one-way, no cycles |
| Coupling | Layers connect only through data models (snapshot · WaveBlock) |
| Thread boundary | Capture = separate thread → queued to main |

---

## Slide 2 — TabView Patterns & Tactics

| Pattern / Tactic | Where applied | Benefit |
|---|---|---|
| **Split Module tactic** (QA-MOD-01) | Tab = self-contained module | Adding/editing a screen is isolated |
| **Abstract base interface** | `TabView` common contract | Core does not depend on concrete tabs |
| **Template Method** | Base: `showEvent→onShown`·`frameDue` / Tab: overrides `onMeasurement`·`onWave` | Reuse common skeleton, implement only the differences |
| **Observer (Publish-Subscribe)** | `broadcastMeasurement` → all tabs' `onMeasurement` | Core emits, each tab updates itself |
| **Plugin registration** | one `registerTab` line | New tab = one class + one line |

> Result: adding/editing a tab requires **no changes to existing screens or the compute core** (Open-Closed)

---

# Part 2. Performance

---

## Slide 3 — Problem Statement

- A pipeline that receives audio in real time and updates **13 screens**
- Observation: "the app feels slow" → cause unknown
- Approach principle: **no guess-based optimization, Measure First**
  - Identify the bottleneck with data before changing any code

---

## Slide 4 — Measurement Results

**PERF instrumentation (Pi, per-block processing budget = 19 ms)**

> **What is a "block"?** A chunk of incoming audio cut into a processing unit (≈960 samples ≈ 19 ms of audio).
> To stay real time, compute + render must finish before the next block arrives (within 19 ms) → **budget = 19 ms**

| Stage | Measured (avg / max) | vs budget |
|---|---|---|
| DSP compute (`tg_process`) | 0.47 / 0.55 ms | **2.5%** |
| Screen drawing (paint) | 8.6 / 24.8 ms | **45%** |
| Backlog | ≈1 block | real time maintained |

> Bottleneck is **rendering (paint), not DSP** — about 18× the compute cost
> → All improvements confined to the rendering path; compute core untouched

---

## Slide 5 — Improvement ① Disable Anti-Aliasing (AA)

**What is AA?** A rendering technique that reduces the stair-step edges (aliasing)
on lines/shapes by semi-transparently blending edge pixels with surrounding colors.

- **Cost:** per-pixel edge computation + alpha blending. Expensive when drawing
  graphs with many lines/points on the CPU (software rasterizer)
- **Judgment:** our real-time graphs refresh dozens of times/sec with dense lines →
  AA on/off is **nearly indistinguishable to the eye**
- **Applied:** `setNotAntialiasedElements(aeFills|aePlottables|aeScatters|aeItems)`
  — disable AA only on fills/plottables/scatters/items (axes & text kept)
- **Effect:** largest single win — **paint time reduced by ~57%**

---

## Slide 6 — Improvement ② Decimation (downsampling)

**What is decimation?** When there are far more data points than the screen can show,
**reduce points to the horizontal pixel width, keeping only representative values.**

- **Why needed:** the horizontal pixel count is the display limit — a single pixel
  column can only hold **one point.** Tens of thousands of points fall into the same
  column → drawing them all is **repeated over-drawing of the same pixels**
  (identical on screen, cost wasted)
- **Adaptive basis:** pixel width is not fixed — it **tracks the current plot width;**
  resizing the window changes the decimation target count too
- **Method:** per pixel-column bucket, keep only **min/max (upper/lower extremes)** →
  ~2 points per pixel
- **Why min/max?** plain averaging/thinning misses waveform peaks and smears them;
  min/max preserves the waveform outline → **minimal information loss**
- **Effect:** vertex count is **bounded to screen width regardless of data size** →
  paint no longer grows with data volume

---

## Slide 7 — Improvement ③ Frame Coalescing (30 fps cap)

**What is coalescing?** When draw requests pile up in a short window, merge those within
the display's real update interval (33 ms) and **draw only once.**

- **Problem:** audio arrives in small chunks frequently. When clearing backlog, one
  input pass calls `onWave` several times → rebuilds arrays + redundant renders each time
- **Method:** `frameDue(33ms)` — skip if <33 ms since last render.
  No effect in normal real time (interval ≫ 33 ms); **merges only during bursts**
- **Where 33 ms comes from:** not arbitrary — **1 s ÷ 30 ≈ 33 ms**, derived from the 30 fps cap below

**Why 30 fps (rationale)**
| Candidate | Paint load (at 8.6 ms) | Verdict |
|---|---|---|
| 60 fps (16.6 ms) | 60×8.6 ≈ **51% of a core** | too much |
| **30 fps (33 ms)** | 30×8.6 ≈ **26% of a core** | **chosen** |
| ≤15 fps | low load | motion looks choppy |

> 30 fps = **above the smooth-perception floor (~24 fps)** while bounding
> paint load to **half of 60 fps** (matches measured paint_fps 26.2)

---

## Slide 8 — Decision: Reject DSP Thread Separation (ADR)

**Assumption:** separating DSP into its own thread improves things via parallelism

**Measurement:** DSP is 2.5% of budget (0.47 ms) → not the bottleneck

| | Separate (rejected) | Keep (chosen) |
|---|---|---|
| Bottleneck (paint) improvement | none | solved via rendering optimizations |
| Time gained | +0.47 ms | — |
| Complexity | locks · queues · race conditions | single-thread simplicity retained |

> High complexity vs a 0.47 ms gain → **not cost-effective**

---

## Slide 9 — Results & Revisit Conditions

**Results (Pi)**
- Real-time processing uses **~28% of one CPU core** (72% headroom)
- UI event-loop lag **1.3 ms**
- Memory **stable at 392 MB** (no leak) · thermals & throttling normal

**Revisit conditions (reopen the ADR)**
- `dsp_total` exceeds 20% of budget (≈4 ms)
- Backlog accumulates persistently, causing real-time delay
