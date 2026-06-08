# Raspberry Pi Performance Measurement Checklist

> **Purpose**: A step-by-step procedure for taking the instrumentation pre-validated on the PC and running it as the **final QA verdict on the Pi**.
> Related documents: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md) (pass/fail criteria) · [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md) (log interpretation) · [PERF_CODE_MAP.md](PERF_CODE_MAP.md) (code locations).
>
> Role split: **PC = pre-testing** (verifying instrumentation behavior and algorithm accuracy, completed) / **Pi = final performance verdict** (actual numbers for latency, CPU, memory, and throttling).

---

## 0. Preparation (one time only)

- [ ] Sync the source (the entire `TimeGrapher/` worked on the PC, copied to the Pi)
- [ ] **Verify the build** (on the PC only the Windows branch is compiled → confirm the first compile of the Linux branch on the Pi):
  ```bash
  cd TimeGrapher && cmake -B build-pi -S . && cmake --build build-pi
  ```
  - Headers used by the Linux branch: `<unistd.h> <cstdio> <cstring> <cstdlib> <QProcess>` (PerfInstrumentation.cpp) — standard POSIX + Qt
  - CMake: `psapi` is wrapped in `IF(WIN32)` so it is irrelevant to the Pi build; the Pi links `asound`
- [ ] **Confirm AGC is OFF** (CON-OP-01, required — leaving it on distorts the measurement):
  ```bash
  alsamixer   # F4(Capture) → set Auto Gain Control to [MM](off)
  ```

---

## 1. Measurement Principles (incorporating mentor feedback — clean measurement)

- [ ] **No modal dialogs or mode switches during measurement**: in the PC measurements, the "Record?" dialog on Start and Live↔Sim switching stalled the event loop, producing **huge outliers** such as a 4-second `ui_loop_lag` and a 470 ms `e2e_full`. → Do not touch anything during the measurement window.
- [ ] **One mode at a time** only. On Start, answer **No** to "Record?".
- [ ] Each measurement should run **30 s+**; exclude the first warm-up run, and look at the **average + median + p95/worst**.
- [ ] Judge results by `e2e_full_ms` (true end-to-end). `e2e_latency_ms` is a lower bound (for reference).

---

## 2. Measurement Scenarios

### 2-1. Live mode — latency, drops, resources, throttling (main performance verdict)
- [ ] Mode = **Live**, sample rate = **48k → 96k → 192k**, each
- [ ] Start → "Record?" No → **leave idle for 60 s** → Stop (3 times per rate)
- [ ] What gets filled in: `e2e_full_ms`·`e2e_latency`·`cap2proc`·`proc2disp`·`disp_paint`·`backlog` (A) / `capture_gap`·`bg_*`·`fg_*` (B) / `cpu_percent`·`throttled_flag`·`rss_bytes` (C·D) / `ui_loop_lag` (A-3)

### 2-2. 30-minute soak — memory and thermal stability
- [ ] Leave Live (or Sim) running idle for **30 minutes continuously** → quit
- [ ] What to watch: `rss_bytes` trend (leak vs. saturation), `throttled_flag` (thermal), `cpu_percent`

### 2-3. Sim mode — accuracy / detection rate (algorithm; same as PC but re-confirm)
- [ ] Mode = **Sim**, set BPH/Amplitude/Error/BeatError → Start (No) → **1000 beats+** (≈2 min)
- [ ] What gets filled in: `onset_err`·`peak_err` (E) / `rate_err`·`beaterr_err`·`amp_err`·`a_match`·`gt_total` (G)

---

## 3. Log Analysis (Pi terminal)

`perf_log.csv` is created in the run directory. Key commands:
```bash
# ★True end-to-end (QA-LT-01 verdict) — median/p95
grep ",e2e_full_ms," perf_log.csv | awk -F, '{print $5}' | sort -n | \
  awk '{a[NR]=$1} END{print "n="NR, "median="a[int(NR/2)], "p95="a[int(NR*0.95)], "max="a[NR]}'

# Stage breakdown (where is the bottleneck?)
for m in cap2proc_latency_ms proc2disp_latency_ms disp_paint_ms; do \
  echo -n "$m: "; grep ",$m," perf_log.csv | awk -F, '{s+=$5;n++} END{print s/n" ms avg"}'; done

# Capture drops
grep ",capture_gap_growth," perf_log.csv | awk -F, '{print $5}'   # near 0 = normal, steadily increasing = drops

# Memory trend (MB)
grep ",rss_bytes," perf_log.csv | awk -F, '{print $1/1000" s "$5/1048576" MB"}'

# Throttling (Pi only — anything other than 0 means thermal/undervoltage occurred)
grep ",throttled_flag," perf_log.csv

# CPU
grep ",cpu_percent," perf_log.csv | awk -F, '{s+=$5;n++;if($5>m)m=$5} END{print "avg="s/n"% max="m"%"}'

# Detection rate (Sim)
echo "detection rate = $(grep -c ',a_match,' perf_log.csv) / $(grep ',gt_total,' perf_log.csv | tail -1 | awk -F, '{print $5}')"
```

---

## 4. Pass Criteria (compare against these values → finalize M1 `(TBD)`)

| Item | metric | Target |
|------|--------|--------|
| End-to-end latency | `e2e_full_ms` | avg ≤50ms · worst ≤100ms |
| Drops | `capture_gap_growth` | 48k=0 · 96k=0 · 192k ≤1/60s |
| CPU | `cpu_percent` | avg ≤70% |
| Throttling | `throttled_flag` | none (0) |
| Memory | `rss_bytes` | 30-min growth ≤200MB · no leak |
| Onset/Peak | `onset_err`/`peak_err` | ≤0.5ms / ≤0.2ms |
| Measurement accuracy | `rate/beaterr/amp_err` | ±1 s/d · ±0.1ms · ±5° |
| Detection rate | `a_match`/`gt_total` | ≥95% · FP ≤2% |
| UI responsiveness | `ui_loop_lag_ms` | ≤200ms |

> ⚠️ Mentor caution: do not adopt measured values directly as required values. Measurement is the basis for judging "is the target achievable / what design is needed".

---

## 5. Already Confirmed in PC Pre-testing (no need to re-confirm on Pi / for reference)
- ✅ Instrumentation pipeline behavior, log format, value validity (no NaN/outliers)
- ✅ Algorithm accuracy (E·G) — PC = Pi identical: onset 0.08ms · peak 0.03ms · rate ±0.3 · amp +3.5° (bias) · detection 97% (long run)
- ✅ End-to-end breakdown (`e2e_full = e2e_latency + disp_paint`) — confirmed that **paint is the main culprit** of latency → will be larger on the Pi
- ⚠️ New things to check on the Pi: **actual latency numbers (especially disp_paint↑↑)**, **throttling (C-2, Pi only)**, **30-minute memory**
