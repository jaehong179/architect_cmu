# Presentation Script — Architecture & Performance

> Each paragraph = one slide.

---

# Part 1. Architecture

---

## [Slide 1] Layered Structure

> "Let me start with the structure. This program is built from four layers. At the bottom, capture receives audio; DSP computes; the engine packages results; and at the top, the UI tabs draw them. The key point is that **dependency is one-way**: upper layers know lower layers, but lower layers do not know upper layers. Layers connect only through data models — the measurement snapshot and the waveform block — so coupling is low. Capture also runs on a separate thread and hands data to main through a queue, so input and rendering never block each other."

---

## [Slide 2] TabView Patterns & Tactics

> "TabView, the common skeleton of all screens, applies a few patterns. First, the **Split Module tactic** makes each tab a self-contained module, so a change in one does not propagate to others. Second, the **Template Method pattern** keeps the common skeleton — such as when to redraw and how to coalesce frames — in the base class, while each tab overrides only its own update logic. Third, the **Observer, or publish-subscribe pattern**, lets the core broadcast measurement results while all 13 tabs receive and update themselves. Thanks to this structure, adding a new screen takes one class and one registration line, and doing so requires no changes to existing screens or the compute core."

---

# Part 2. Performance

---

## [Slide 3] Problem Statement

> "Now for performance. This program receives audio in real time and updates 13 screens. During development, we observed that the app felt slow. Instead of guess-based optimization — jumping straight to the compute code — we applied a **Measure-First principle: identify the cause with data before touching anything.**"

---

## [Slide 4] Measurement Results

> "We instrumented each stage of the pipeline and measured the times. Here, **a 'block' is a chunk of incoming audio cut into a processing unit** — about 960 samples, roughly 19 milliseconds of sound. To stay real time, we must finish this block's compute and screen update **within 19 milliseconds**, before the next block arrives. That is why the **processing budget is 19 milliseconds.**
>
> The results were telling: **DSP compute was only 0.47 ms, 2.5% of the budget**, while **screen drawing took 8.6 ms, 45% of the budget.** The bottleneck was not compute but rendering — about 18× the difference. So we confined our improvements to the rendering path and left the compute core untouched. Let me walk through the three improvements one by one."

---

## [Slide 5] Improvement ① Disable Anti-Aliasing

> "The first is anti-aliasing, or AA. AA is a rendering technique that reduces the stair-step edges on lines and shapes by semi-transparently blending edge pixels with surrounding colors. The problem is that this requires per-pixel edge computation and color blending, which is expensive when drawing graphs with many lines and points on the CPU. But our graphs refresh dozens of times per second with dense lines, so with or without AA the result is nearly indistinguishable to the eye. So we disabled AA only for fills, plottables, scatters, and items, keeping it for axes and text. This single change cut paint time by about 57% — the largest of any single improvement."

---

## [Slide 6] Improvement ② Decimation

> "The second is decimation, or downsampling. Let me first explain why it's needed. The graph's horizontal axis is measured in pixels, and **a single pixel column can hold only one point.** But we have tens of thousands to hundreds of thousands of data points, so many of them fall into the same pixel column. Drawing them all means **repeatedly over-drawing the same pixels** — the screen looks identical while the drawing cost is wasted. In effect, we would be painting the same spot over and over.
>
> Also, this horizontal pixel count is not fixed. For example, a 1000-pixel window means 1000 columns; enlarging or shrinking the window changes that value, and the **decimation target count adjusts to the current plot width** accordingly.
>
> The method: within each pixel's range, we take only the maximum and minimum — the upper and lower extremes — reducing to about two points per pixel. Plain averaging or thinning would miss the waveform's peaks and smear it, but keeping min and max preserves the waveform outline, so information loss is minimal. As a result, the number of drawn points is bounded to the screen width regardless of data volume, so paint cost does not grow as data grows."

---

## [Slide 7] Improvement ③ Frame Coalescing and the 30 fps Rationale

> "The third is frame coalescing. Audio arrives in small chunks frequently, and when clearing backlog, a single input pass calls the update function several times, rebuilding arrays and drawing redundantly each time. So we skip a render if less than 33 milliseconds have passed since the last one. In normal real-time operation there is no effect; it only merges when data bursts in.
>
> This **33 milliseconds is not arbitrary. One second divided by 30 is about 33 milliseconds** — the interval for drawing 30 times per second, i.e., 30 fps. So why 30 fps? Here is the rationale. One paint takes 8.6 milliseconds; at 60 fps, paint alone would consume 516 milliseconds per second, 51% of a core. Dropping to 30 fps makes it 258 milliseconds, 26% — half. On the other hand, below 15 fps the motion looks choppy. So 30 fps sits above the ~24 fps smooth-perception floor while bounding paint load to half of 60 fps. That is why we used the 33-millisecond interval, and the measured refresh rate came out at 26 fps, matching this cap."

---

## [Slide 8] Decision — Reject DSP Thread Separation

> "Here we faced an architecture decision. The backlog included a task to 'separate DSP into its own thread,' a reasonable assumption from a parallelism standpoint. But by measurement, DSP was only 2.5% of the budget — not the bottleneck. Separating it would greatly increase complexity through locks, queues, and race conditions, while gaining only 0.47 milliseconds. Judging it not cost-effective, we rejected the task and recorded that decision as an ADR."

---

## [Slide 9] Results & Revisit Conditions

> "As a result, on the Pi, real-time processing used about 28% of one CPU core, UI event-loop lag was 1.3 milliseconds, memory was stable at 392 MB, and thermals and throttling were normal. That said, this decision is conditional. We specified that if DSP time exceeds 20% of the budget, or if backlog accumulates persistently, we will revisit thread separation."
