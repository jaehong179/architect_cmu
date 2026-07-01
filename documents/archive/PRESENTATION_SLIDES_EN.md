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

---

## Slide 10 — Sample-rate Scaling (48kHz vs 192kHz)

**Re-measured on the same Pi, sample rate raised 4× (960 → 3,840 samples/block)**

| Metric | 48kHz | 192kHz | Change |
|---|---|---|---|
| DSP (dsp_total) | 0.47 ms | 0.55 ms | **+17% (barely rises)** |
| — of which detect | 0.453 ms | 0.485 ms | +7% |
| Paint (disp_paint) | 8.6 ms | 7.5 ms | **no increase** |
| Backlog | ≈1 block | ≈1 block | real time held |
| Throughput fg_sps / spf | 50k / 960 | 201k / 3,840 | ×4 (expected) |

**Why CPU & paint don't scale 4×**
- **detect ∝ tic/toc events (time-based)** → independent of sample rate (only the +7% scan part grows)
- **paint ∝ decimation** bounds points to pixel width → independent of sample rate

**Memory & thermal (the only real constraint)**
| Metric | 48kHz | 192kHz | Factor |
|---|---|---|---|
| PSS (absolute plateau) | 392 MB | 1,069 MB | ×2.7 |
| RSS (physical total) | 441 MB | 1,119 MB | ×2.5 |
| History-buffer step | +213 MB | +825 MB | ×~4 |
| Temp peak | 75.2 °C | 80.7 °C | +5.5 °C |
| Throttle | none | none | — |

> ※ Memory figures are **absolute usage** (not the increase). vs 33 MB start, total growth = **+359 MB / +1,036 MB**.
> The 8-min history buffer scales **linearly** with sample rate → **RSS 1.1 GB. On a ≤2 GB Pi, shrink the buffer.**

**Takeaway**: compute & rendering handle 192kHz easily (no leak, real time held). **The only real constraint is memory (1.1 GB) and heat (80.7°C).**

---

## Slide 11 — End-to-End Latency (measured window · per-stage times)

**Our e2e = ④ ring-buffer write → ⑧ actual paint** (in-app only; acoustic & monitor HW excluded)

```
④ ring write ─cap2proc─▶ ⑤ proc start ─proc2disp─▶ ⑦ replot req ─disp_paint─▶ ⑧ paint
  (T_capture)                                                       (afterReplot)
 └────────────────────── e2e_full ──────────────────────┘
 e2e_full = cap2proc + proc2disp + disp_paint
```

**Per-stage time (avg)**
| Segment | Stage | 48kHz | 192kHz |
|---|---|---|---|
| ④→⑤ | cap2proc (queue wait) | *Live-only* | *Live-only* |
| ⑤→⑦ | proc2disp (DSP+broadcast+build) | 1.0 ms | 1.76 ms |
| — | └ of which DSP | 0.47 ms | 0.55 ms |
| ⑦→⑧ | disp_paint (Qt queue+rasterize) | 8.6 ms | 7.5 ms |
| ⑤→⑧ | **processing→paint sum** | **~9.6 ms** | **~9.3 ms** |
| ④→⑧ | e2e_full (true end-to-end) | *needs Live* | *needs Live* |

**Excluded**: the front (sound→mic→ADC→OS buffer) and back (monitor→eye) — the app cannot timestamp them.

> ⚠️ **cap2proc & e2e_full are Live-capture-only metrics.** These 48k/192k runs were **Sim**, so those values are absent;
> only the "processing→paint" segment (⑤→⑧) is measured (**≈9.3–9.6 ms, sample-rate independent**).
> For true capture-inclusive end-to-end, **re-measure in Live mode.**

**Budget check (measurable segment ⑤→⑧)**
| | 48kHz | 192kHz |
|---|---|---|
| Required (block deadline = budget) | 20 ms | 20 ms |
| **Currently used (processing→paint)** | **9.6 ms** | **9.3 ms** |
| **Remaining headroom** | **≈10.4 ms (52%)** | **≈10.7 ms (54%)** |

> ※ Paint (8.6/7.5 ms) is **separately capped at 30 fps** → not on every block. A no-paint block uses only **proc2disp (1.0/1.76 ms)** (≈19/18 ms free).
> The 9.6 ms is the worst case where a block's processing and a paint coincide. Backlog held at ≈1 block confirms real time.
