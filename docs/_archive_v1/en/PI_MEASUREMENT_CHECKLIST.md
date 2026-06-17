# Raspberry Pi Performance Measurement Checklist

> **Purpose**: A step-by-step procedure for taking the instrumentation pre-validated on the PC and running it as the **final QA verdict on the Pi**.
> Related documents: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md) (pass/fail criteria · external runbook) · [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md) (log interpretation) · [PERF_CODE_MAP.md](PERF_CODE_MAP.md) (code locations).
>
> Role split: **PC = pre-test** (instrumentation behavior · algorithm accuracy — done) / **Pi = final performance verdict** (real numbers for latency · CPU · memory · throttle).

---

## ★ Measurement architecture (important — what is measured where)

Resources (CPU · memory · throttle) are measured **outside the app**. Measuring them from inside the
process means the measurement itself consumes CPU/memory and contaminates the result (observer effect).
In-app instrumentation only covers the **semantic metrics that can't be seen from outside**.

| What | Where | Output |
|------|-------|--------|
| Latency (A) · accuracy (G) · FPS (F) · backlog (E) · UI loop lag (A-3) | **in-app** | `build/perf_log.csv` |
| Memory (PSS · Private_Dirty · RSS) + thermal · throttle (temp · clock · throttled) | **external** `tools/resource_sample.sh` (combined) | `resource_ext.csv` |
| CPU% | **external** `htop` (quick) or an installed tool | — |

→ The three logs are aligned in time automatically by `tools/perf_join.py` (§3-4). The alignment anchor is
the `epoch_ms_t0` field in the `perf_log.csv` header (= the wall-clock epoch[ms] at monotonic t_ms=0).

---

## 0. Preparation

- [ ] Sync source (the full tree worked on the PC → the Pi)
- [ ] **Build check**:
  ```bash
  cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
  ./build_run.sh . --no-run        # or: cmake --build build --parallel
  ```
  - Resource measurement moved out, so `PerfInstrumentation.cpp` has no OS-specific `/proc`/WinAPI/`vcgencmd` branches (simpler build).
- [ ] **Confirm AGC OFF** (CON-OP-01, mandatory — leaving it on distorts measurement):
  ```bash
  alsamixer   # F4 (Capture) → set Auto Gain Control to [MM] (off)
  ```
- [ ] **Clear thermal state (for throttle accuracy)** — even at idle, 76–79°C and a throttle history (0xe0000) were observed:
  ```bash
  # (1) physically confirm cooling (heatsink/fan)  (2) reboot to clear throttle history
  sudo reboot
  # after reboot, confirm a clean state ↓
  vcgencmd measure_temp        # start at ~60°C or below if possible
  vcgencmd get_throttled       # ★ must be throttled=0x0 (otherwise cooling is insufficient)
  ```

---

## 1. Measurement principles (mentor feedback — clean measurement)

- [ ] **No modal dialogs / mode switches during measurement**: the Start "Record?" dialog and Live↔Sim switches
  stall the event loop, producing huge outliers like `ui_loop_lag` 4 s · `e2e_full` 470 ms. → Don't touch the run window.
- [ ] **One mode at a time**. On Start, answer **No** to "Record?".
- [ ] Each measurement **30 s+**, exclude the first (warm-up) run, report **mean + median + p95/worst**.
- [ ] Judge with `e2e_full_ms` (true end-to-end). `e2e_latency_ms` is a lower bound (reference).
- [ ] **Keep the external samplers (memory · thermal) running from before Start until after Stop** (for time alignment).

---

## 2. Running the measurement (3 terminals)

### Terminal A — the app
```bash
cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
./build/TimeGrapher              # to rebuild while launching: ./build_run.sh .
```

### Terminal B — resource sampler (memory + thermal combined, right after the app is up)
```bash
cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
tools/resource_sample.sh -o resource_ext.csv   # PSS + temp + clock + throttle at 1 s, Ctrl+C to stop
```
> ★ Start it **immediately** after the app launches — starting late leaves a gap in the resource data
> (the previous run started ~40 s late, missing the early window).

### Perform scenarios in the app (Terminal A window)

**2-1. Live mode — latency · drops · resources · throttle (main verdict)**
- [ ] Mode = **Live**, sample rate = **48k → 96k → 192k** each
- [ ] Start → "Record?" No → **idle 60 s** → Stop (3× per rate)
- [ ] Filled into the internal CSV: `e2e_full_ms`·`e2e_latency`·`cap2proc`·`proc2disp`·`disp_paint`·`backlog` (A·E) / `capture_gap`·`bg_*`·`fg_*` (B) / `ui_loop_lag` (A-3) / `paint_fps` (F)
- [ ] Recorded simultaneously in external CSVs (B·C): PSS · temperature · throttle

**2-2. 30-minute soak — memory · thermal stability**
- [ ] Leave Live (or Sim) running **30 min continuously** + repeated switching across 12 tabs → exit
- [ ] Look at: `resource_ext.csv` PSS trend (leak vs saturation) · temperature/clock/throttle

**2-3. Sim mode — accuracy / detection rate (algorithm; same as PC but re-confirm)**
- [ ] Mode = **Sim**, set BPH/Amplitude/Error/BeatError → Start (No) → **1000+ beats** (≈2 min)
- [ ] Filled into the internal CSV: `onset_err`·`peak_err` (E) / `rate_err`·`beaterr_err`·`amp_err`·`a_match`·`gt_total` (G)

### Shutdown
- [ ] **Exit the app cleanly** (so perf_log.csv flushes·closes) → **Ctrl+C** in terminals B·C

---

## 3. Log analysis

> 📌 **Recommended: two tools and you're done.** Full interpretation guide → [PERF_ANALYSIS_TOOLS.md](PERF_ANALYSIS_TOOLS.md).
> ```bash
> tools/verify_measurement.sh                                            # 1) recording integrity
> tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv   # 2) stats+QA verdict+thermal effect (auto warm-up exclusion)
> ```
> 3-1~3-4 below are for manual/detailed checks (inspecting directly with grep·awk).

### 3-1. Inspect the internal CSV (first)
```bash
cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
# confirm the resource metrics are gone (moved external) + the alignment anchor is present
grep -E "cpu_percent|rss_bytes|throttled_flag" build/perf_log.csv   # → should print nothing
grep "epoch_ms_t0" build/perf_log.csv                              # → should show one anchor line
```

### 3-2. Internal metrics (perf_log.csv)
```bash
# ★ true end-to-end (QA-LT-01 verdict) — median/p95
grep ",e2e_full_ms," build/perf_log.csv | awk -F, '{print $5}' | sort -n | \
  awk '{a[NR]=$1} END{print "n="NR, "median="a[int(NR/2)], "p95="a[int(NR*0.95)], "max="a[NR]}'

# stage breakdown (where is the bottleneck?)
for m in cap2proc_latency_ms proc2disp_latency_ms disp_paint_ms; do \
  echo -n "$m: "; grep ",$m," build/perf_log.csv | awk -F, '{s+=$5;n++} END{print s/n" ms avg"}'; done

# capture drops (near 0 = OK, steady growth = drops)
grep ",capture_gap_growth," build/perf_log.csv | awk -F, '{print $5}'

# UI responsiveness
grep ",ui_loop_lag_ms," build/perf_log.csv | awk -F, '{print $5}' | sort -n | \
  awk '{a[NR]=$1} END{print "median="a[int(NR/2)], "p95="a[int(NR*0.95)], "max="a[NR]}'

# detection rate (Sim)
echo "detection = $(grep -c ',a_match,' build/perf_log.csv) / $(grep ',gt_total,' build/perf_log.csv | tail -1 | awk -F, '{print $5}')"
```

### 3-3. External metrics (resource_ext.csv — combined)
> Combined CSV columns: `epoch_s,pss_kb,pss_mb,private_dirty_kb,rss_kb,temp_c,arm_mhz,throttled_hex,now_throttling,ever_throttled`
> ★ "Was THIS measurement throttled?" must be judged by **throttling during the run = `throttled_hex` low 4 bits
>   (or `now_throttling`)**, NOT `ever_throttled` (since-boot history, includes throttling before the run).
```bash
# memory leak: PSS growth from start to end (MB)  ($3=pss_mb)
awk -F, 'NR==2{s=$3} NR>1{e=$3} END{printf "PSS start=%.1fMB end=%.1fMB growth=%.1fMB\n", s, e, e-s}' resource_ext.csv

# thermal: peak temp · min clock + % of samples actually throttling DURING the run
python3 - resource_ext.csv <<'PY'
import csv,sys
n=now=0; t=[]; c=[]
for r in csv.DictReader(open(sys.argv[1])):
    n+=1; t.append(float(r['temp_c'])); c.append(int(r['arm_mhz']))
    if int(r['throttled_hex'],16)&0xF: now+=1
print(f"peak={max(t):.1f}°C  min_clock={min(c)}MHz  throttled_during_run={now}/{n} ({now/n*100:.0f}%)")
PY
```
> Note: the default awk (mawk) lacks bit ops (`and()`/`strtonum()`), so the throttle tally is done in python.

### 3-4. Time alignment (internal ↔ external)
```bash
# merge into long format (epoch_ms,t_ms,source,metric,value)
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv -o joined.csv

# correlate: did latency spikes coincide with heat/throttle?
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv \
    --correlate e2e_full_ms --tolerance 1500 -o corr.csv
```

---

## 4. Pass/fail criteria (compare against these → confirm the M1 `(TBD)` values)

| Item | Source / metric | Target |
|------|-----------------|--------|
| End-to-end latency | `perf_log.csv` `e2e_full_ms` | mean ≤50 ms · worst ≤100 ms |
| Drops | `perf_log.csv` `capture_gap_growth` | 48k=0 · 96k=0 · 192k ≤1/60s |
| UI response | `perf_log.csv` `ui_loop_lag_ms` | ≤200 ms |
| Onset/Peak | `perf_log.csv` `onset_err`/`peak_err` | ≤0.5 ms / ≤0.2 ms |
| Measurement accuracy | `perf_log.csv` `rate/beaterr/amp_err` | ±1 s/d · ±0.1 ms · ±5° |
| Detection rate | `perf_log.csv` `a_match`/`gt_total` | ≥95% · FP ≤2% |
| **Memory** | **`resource_ext.csv` PSS** | 30-min growth ≤200 MB · no leak |
| **Throttle** | **`resource_ext.csv` throttled-during-run** (throttled_hex low 4 bits / now_throttling) | none (0%) |
| **CPU** | **external (htop/pidstat)** | mean ≤70% |

> ⚠️ Mentor note: don't take measured values as the requirement verbatim. Measurement is the basis for judging "is the target achievable / what design is needed".

---

## 5. Already confirmed in the PC pre-test (no need to re-check on the Pi / reference)
- ✅ Instrumentation pipeline behavior · log format · value sanity (no NaN/outliers)
- ✅ Algorithm accuracy (E·G) — PC=Pi identical: onset 0.08 ms · peak 0.03 ms · rate ±0.3 · amp +3.5° (bias) · detection 97% (long run)
- ✅ End-to-end decomposition (`e2e_full = e2e_latency + disp_paint`) — **paint is the dominant latency contributor**, confirmed → will be larger on the Pi
- ⚠️ New on the Pi: **real latency numbers (esp. disp_paint↑↑)**, **thermal·throttle (external, Pi-only)**, **30-min memory (PSS)**
