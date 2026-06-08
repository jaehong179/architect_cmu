# Performance Verification Guide — "Which part of the document / why / what is being checked"

> Target documents: `Time_Grapher_M1_v7_QA_Taxonomy.docx` (Korean) / `..._EN.docx` (English)
> This document defines **only what must be verified (verification items, purpose, acceptance criteria)**.
> The actual code instrumentation/log insertion is performed by Claude Code following `INSTRUMENTATION_PLAN.md`.

---

## 0. Basic Principles of Verification (reflecting mentor feedback)

1. **Measure from the baseline first.** Before adding more features, you must first establish the current system's resource and timing characteristics
   so you can know "what hits a limit when tabs/graphs are added."
2. **Response time = execution time + wait time.** Don't look only at a single end-to-end number; **decompose into stages**,
   and separately verify "where is it slow (execution)" and "how much does it wait due to load (wait)."
3. **Environment = load must be defined.** Every timing item is only meaningful when measured with an explicit statement of "what the system is doing concurrently at that time"
   (rendering N graphs, sample rate, Pi internal IO, etc.). → distinguish **normal load / peak load**.
4. **Target values must come from the 'evaluation criteria'.** The figures in the document (especially those marked `(TBD)`) are currently **assumed values**.
   The purpose of measurement is, before "was it achieved," first to judge **"is this target valid / what is the value to confirm or revise."**
5. Repeat each item **at least 3 times**, consider excluding the first run (warm-up), and look at the **average + worst (worst/p95/p99)** together.

Format of each item:
`📍 Document location` / `🎯 Purpose of the check (why)` / `✅ What to check (what / under which conditions)` / `📐 Acceptance criteria`

---

## A. Response Speed / Latency

### A-1. End-to-end latency (most important)
- 📍 **QA-LT-01** (Response Measure: "average difference between capture time → screen display time ≤50 ms · worst ≤100 ms"), **EXP-02**, Tracking Metrics
- 🎯 Verify the **real-time claim** that "clock behavior can be observed in real time in Live Mode." The top-level quality attribute of this project.
- ✅ Measure the time from when an external beat reaches the microphone → until the corresponding measured value is visible in the GUI, on a **per-beat (per-event) basis**.
  Not a block average, but the **capture→display time of each A/C event** collected to view the distribution. Record the environment (load) together.
- 📐 Average **≤50 ms**, worst **≤100 ms**. (assumed value → can be readjusted once evaluation criteria are secured)

### A-2. Stage decomposition of latency (execution vs wait)
- 📍 **QA-LT-01 Plan rationale** ("Low Latency section — definition of the 3-stage latency capture→processing→display"), **EXP-02** (stage-wise bottleneck identification)
  ※ **FR-SYS-4**, which originally required 3-stage reporting, **is currently missing from the document** → still needed for instrumentation (restoration recommended).
- 🎯 Determine "where time is spent." Mentor's key point: separate execution time (task processing) and wait time (load/scheduling).
- ✅ Measure **capture→processing latency**, **processing→display latency**, and **end-to-end** each + observe the **backlog (accumulated unprocessed samples)**.
- 📐 (no absolute threshold) — it is sufficient to identify the **bottleneck location** from each stage's average/worst and the backlog trend. Later used as input for the queuing model.

### A-3. GUI input responsiveness
- 📍 **QA-RT-01** ("GUI input response time ≤200 ms"), **EXP-06**
- 🎯 Verify whether **the UI stays responsive without freezing** even during measurement/render load (responsive GUI claim).
- ✅ The time from button/input operation → reflection under a loaded state (Live + multi-tab rendering).
- 📐 **≤200 ms**.

### A-4. Fault feedback / counter reflection latency
- 📍 **QA-AC-04** ("error message display ≤2 seconds"), **QA-US-01** ("fault display ≤2 seconds"), **QA-LT-03** ("count increment reflected on screen within 5 seconds")
- 🎯 Verify whether the user **recognizes faults in a timely manner** (signal loss, overload) (graceful performance degradation, usability).
- ✅ The time from fault/load injection → until the notification/counter appears on screen.
- 📐 Notification **≤2 seconds**, drop/miss counter reflected on screen **≤5 seconds**.

---

## B. Throughput / Capture Drop

### B-1. Capture stability per sample rate
- 📍 **QA-RT-02** (`(TBD)` "48k: 0 drops (required) / 96k: 0 drops (target) / 192k: ≤1 drop/60 s"), **EXP-01**
- 🎯 Confirm the throughput limit at which Pi 5 captures, processes, and displays **without dropping data** at high sample rates. (largest technical risk R-T-01)
- ✅ For **48,000 / 96,000 / 192,000 sps** each, **60 seconds of continuous capture × 3 times**, count the number of dropped audio blocks. (AGC OFF required)
- 📐 48k **0** (required), 96k **0** (target), 192k **≤1 drop/60 s** (stretch). If not met → decide to reset the baseline to 96k / downgrade 192k.

### B-2. Continuous capture without interruption
- 📍 **QA-RT-01** ("0 capture interruptions during 60 seconds of continuous capture")
- 🎯 Basic stability of continuous Live Mode operation.
- ✅ Number of capture interruptions (underruns/gaps) occurring during 60 seconds of continuous operation.
- 📐 **0**.

### B-3. Effective throughput (auxiliary metric)
- 📍 (baseline existing counters FPS/SPS/SPF — `TMasterAudioDataRaw`)
- 🎯 Whether the **actual processed sample count (SPS)** is maintained relative to the configured sample rate (early detection of underrun).
- ✅ Log **FPS (render rate), SPS (effective samples/sec), SPF** in comparison with the configured sps.
- 📐 SPS ≈ configured sps (a shortfall = drop/underrun signal), FPS stable.

---

## C. CPU · Thermal (computational resources)

### C-1. CPU utilization
- 📍 **QA-RT-01 / QA-EE-01** ("average CPU ≤70%"), **EXP-06**
- 🎯 Confirm whether there is "room (headroom) to add more features." The 70% target is based on **securing 30% peak/scheduling margin** (Pi 5, 4 cores).
- ✅ During Live (96k) operation, **overall + per-core** CPU%. Also check whether cores are unbalanced (whether threads are concentrated on a single core).
- 📐 Average **≤70%**. (if already near the limit → a signal that a resource management/offloading strategy is needed)

### C-2. Core temperature / throttling
- 📍 **QA-EE-01** ("core temperature below throttle threshold · 0 throughput degradation after 30 minutes"), **EXP-06**
- 🎯 Verify there is no **performance degradation due to thermal throttling** under Pi 5's passive/limited cooling (power and thermal efficiency).
- ✅ Core temperature trend during 30 minutes of continuous operation + **whether throttling occurs** (`vcgencmd get_throttled` flag). Drop/latency changes after throttling.
- 📐 **Below** the throttle threshold, throttled flag **not raised**, throughput degradation/drops after 30 minutes **0**.

---

## D. Memory (8 GB RAM)

### D-1. Memory stability / leaks
- 📍 **QA-RT-03** (`(TBD)` "memory increase ≤200 MB after 30 minutes · no leak pattern · no OOM/swap"), **EXP-06**
- 🎯 Whether it remains **continuously available without OOM** under long-running operation/multiple tabs (especially the risk of unbounded growth of the history buffer/cache, R-T-01).
- ✅ App RSS trend (increase relative to start, sustained-increase pattern) during 30 minutes of continuous operation + **repeated switching across 12 tabs**.
- 📐 Increase **≤200 MB** after 30 minutes, **no** sustained-increase (leak) pattern, **no** OOM/excessive swap.

### D-2. Long-term (8-hour) accumulation
- 📍 **QA-SC-01** ("memory linear increase stays within limit even after 8 hours of accumulation")
- 🎯 Whether accumulated data such as long-term performance graphs stays **within a finite limit** (downsampling/ring-buffer behavior).
- ✅ Whether the memory increase stays **within a limit (not runaway)** during accumulation over several hours, and whether automatic reduction of update frequency works.
- 📐 Linear/saturated within the limit, no OOM.

---

## E. Timing Precision (sample clock / HW dependent)

### E-1. Cumulative stage-wise timing error
- 📍 **QA-AC-03** ("cumulative timing error over 60 seconds ≤1 sample · sum of per-stage quantization ≤1 ms"), **EXP-08**
- 🎯 Whether **timing information is conveyed without loss** through all pre-computation stages from capture (critical because measurements are derived from small time differences).
- ✅ Compare each stage's timestamp vs ground truth using Sim known inputs; cumulative error over 60 seconds.
- 📐 Cumulative **≤1 sample**, sum of stage quantization **≤1 ms**.

### E-2. Onset / Peak identification precision
- 📍 **QA-AC-02** ("Onset ≤0.5 ms · Peak ≤0.2 ms @96k"), **EXP-08**
- 🎯 Whether the measurement reference points (onset/peak) are identified **with sufficient precision**. (at 96k, 1 sample = 10.4 µs)
- ✅ Onset/peak identification error relative to Sim GT.
- 📐 Onset **≤0.5 ms**, Peak **≤0.2 ms** (@96k).

---

## F. Render / Scale Performance

### F-1. Render load when scaling to multiple tabs
- 📍 **QA-SC-01** ("frame drop ≤10% when tabs increase from 1 to 12"), **EXP-06**
- 🎯 Whether **responsiveness collapses under render load** as tabs/graphs increase (scalability). Mentor: "if the next beat arrives while rendering N graphs, a wait occurs."
- ✅ Increase the number of active tabs from **1 → 12** and observe the FPS change (frame drop %), as well as the simultaneous change in end-to-end latency.
- 📐 Frame drop **≤10%**, latency stays within the target (A-1).

### F-2. Display consistency across multiple displays
- 📍 **QA-CO-02** ("numeric difference between displays 0 · update synchronization skew ≤100 ms"), **EXP-07**
- 🎯 Whether the measured value at the same point in time is **displayed consistently and in sync across all tabs (MSB, Trace, Vario, etc.)** (visualization consistency). As tabs increase, there is a risk that update timing drifts → related to render load.
- ✅ Open multiple tabs simultaneously and **compare values at the same point in time** + measure the **update-time difference (skew)** between tabs.
- 📐 Numeric difference **0** (within precision limits), synchronization skew **≤100 ms**.

---

## G. Detection / Measurement Accuracy (DSP under load/noise)

### G-1. Measurement accuracy
- 📍 **QA-CO-01** (`(TBD)` "Rate ≤±1 s/d · Beat Error ≤±0.1 ms · Amplitude ≤±5°"), **EXP-07**
- 🎯 Produce **accurate and consistent measured values** from the acoustic signal (reliability). Result of the algorithm + timing precision.
- ✅ Sim synthesis (configured BPH/Error/Amp/BE) input → compare the produced value with the configured value.
- 📐 Rate **≤±1 s/d**, Beat Error **≤±0.1 ms**, Amplitude **≤±5°**.

### G-2. T1/T3 detection accuracy
- 📍 **QA-AC-01** ("T1·T3 detection rate ≥95% · FP ≤2%"), **EXP-03**
- 🎯 Whether the foundational events of measurement (T1 = rate/BE, T3 = amplitude) are **detected with high reliability**.
- ✅ Sim 1,000 beats (BPH 21,600–36,000, Amp 200–300°) vs ground truth.
- 📐 Detection rate **≥95%**, False Positive **≤2%**.

### G-3. Robustness in noisy environments
- 📍 **QA-CO-04** ("detection rate ≥80% maintained · measurement error increase ≤2×"), **EXP-09**
- 🎯 Whether **detection/measurement does not collapse** even under environmental noise such as 60 dB conversation/vibration.
- ✅ Detection-rate/error change after injecting noise (60 dB SPL) relative to the noise-free baseline. (comparison of filter effect)
- 📐 Detection rate **≥80%** maintained, measurement error increase **≤2×**.

---

## H. On-device AI (TinyML) — exploratory task

- 📍 **EXP-05** (QA-SE-01·QA-AC-04) ("per-window inference ≤20 ms · accuracy ≥80% · model ≤100 KB")
- 🎯 Whether **real-time inference is feasible** on the Pi (signal quality classification) — for a feasible/downgrade decision. Confirm cloud independence.
- ✅ Pi on-device single-inference time, classification accuracy, model size. (confirm 0 external transmission during measurement)
- 📐 Inference **≤20 ms**, accuracy **≥80%**, model **≤100 KB**. If not met → reduce scope / downgrade Opt (=Low).

---

## I. Deployment / Startup (partial HW · operations)

- 📍 **QA-DY-01** ("clean image deploy→startup ≤30 minutes · AGC OFF checklist passed"), **EXP-11**
- 🎯 Whether the **demo environment can be reproducibly brought up** from the provided Pi image.
- ✅ Deploy→startup time on a clean image, environment-precondition checks such as **AGC OFF**.
- 📐 Deploy→startup **≤30 minutes**, checklist passed, 100% reproducibility.

---

## J. Baseline-First Measurement + Reflecting Results in the Document

**What to measure first (baseline profiling, mentor-recommended):**
1. **A-2 (latency stage decomposition) + A-1 (end-to-end)** — the current system's capture/processing/display time distribution.
2. **C-1·C-2 (CPU·temperature)·D-1 (memory)** — resource headroom/leak/throttling over 30 minutes of operation.
3. **B-1 (per-sample-rate drops)** — the 96k/192k limit.
These three together reveal "how much room there is to add more features," and lead into resource-management strategy experiments.

**Results → targets for document updates:**
- Confirm/revise the values of items marked `(TBD)`: **QA-RT-02 (drops) · QA-RT-03 (memory) · QA-CO-01 (accuracy) · EXP-06 (resources)**, and **QA-LT-01 (latency)**.
- Reflect **bottlenecks/limits** revealed by measurement in the Risk (R-T-01·R-T-02)/Experiment results.
- **Caution (mentor):** do not use measured values directly as required values. Required values (targets) must come from the **evaluation criteria**,
  and measurement is used as a basis for judging "is that target achievable / what design is needed."

---

## K. Items Excluded from This HW Performance Guide (not an omission — different in nature)

> The following are quantitative items present in the document, but because they **are not subjects of HW (Raspberry Pi) performance measurement**, they are not covered in this guide.
> (They are still verified, but separately under the **functional/integrity/usability/development-metric** tracks.)

| Item (QA / EXP) | Value | Reason for exclusion (verified where?) |
|---|---|---|
| **QA-CO-03** / EXP-07 computation·visualization identity | 1:1 tracking·regression 100% | Data **integrity** (logical verification) — via Playback reproduction·regression tests |
| **QA-SE-02** / EXP-12 storage integrity | Save→Load checksum 100% | **Integrity** — save/load checksum verification |
| **QA-IO-01** / EXP-12 interoperability | WAV 100%·round-trip 0 | **Function/format** — external-tool parsing·round-trip |
| **QA-SE-01** / EXP-05 confidentiality | external transmission 0 | **Security** — network traffic inspection (during measurement); inference performance is covered as HW in H |
| **QA-EX-02** / EXP-04 new-tab change scope | ≤3 files·≤300 LoC·regression 0 | **Development metric** — git diff |
| **QA-EX-01·QA-MT-01** / EXP-10 onboarding·verification | coverage ≥70%·learning ≤1 day·addition ≤3 days | **Development/maintainability metric** — unit tests·onboarding |
| **QA-EX-03** / EXP-11 portability | platform-dependent modules ≤15% | **Code structure** — build·static analysis (successful build on both platforms·deployment time are checked in I) |
| **QA-US-01·QA-US-02** / EXP-13 usability | legend·threshold 100%·judgment ≤5 seconds | **Usability/function** — checklist (only fault-notification latency ≤2 seconds is covered as HW in A-4) |

> In other words, the quantitative values that must be measured from the HW performance perspective are all included in **all of A–I + J (priorities)**,
> and the K items above are classified as being verified in other tracks rather than as "performance."
