# Performance Analysis Tools

> The `tools/` scripts that **verify and analyze** the measurement outputs (`build/perf_log.csv` + `resource_ext.csv`).
> Measurement procedure → [PI_MEASUREMENT_CHECKLIST.md](PI_MEASUREMENT_CHECKLIST.md); pass/fail criteria → [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md).
> All **install-free** (bash/awk + Python standard library). No pandas.

---

## At a glance (after a measurement, these two lines)

```bash
tools/verify_measurement.sh                                            # 1) was it recorded correctly?
tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv   # 2) stats · verdict · thermal effect
```

---

## 1. `verify_measurement.sh` — recording integrity check

Auto-checks whether the data was **recorded correctly** (not a pass/fail QA verdict).

```bash
tools/verify_measurement.sh [perf_log.csv] [resource_ext.csv]   # default paths if no args
```

Checks:
- **Internal**: alignment anchor (`epoch_ms_t0`) present · resource metrics (cpu/rss/throttle) absent (moved external) · row count · no NaN/inf
- **External**: PSS·temperature filled within plausible range · `now_throttling` matches `throttled_hex` (bit logic correct)
- **Alignment**: `perf_join.py` merges the two logs
- Memory is checked for **recording only (min/max), no growth evaluation**.

---

## 2. `analyze_perf.py` — integrated analysis report ★

Per-metric stats + QA targets + external resource summary + thermal effect, in one shot.

```bash
tools/analyze_perf.py [perf_log.csv] [--resource resource_ext.csv] [--bucket <metric>]
```

Output:
1. **Per-metric stats** — n · mean · median · p95 · p99 · worst. (for error metrics the max-abs is the 'worst')
2. **vs QA targets** — ✅/❌ for known metrics (`ui_loop_lag`·`rate/beaterr/amp_err`·`e2e_full`·`onset/peak_err`).
3. **External resources** — PSS (growth shown only, not evaluated) · peak temp · clock range · % throttled during run.
4. **Thermal effect** — split a chosen latency metric **by clock bucket**. If the value is the same regardless of clock, it is *not CPU-clock-bound* (render/GPU-bound).

★ **Automatic warm-up exclusion**: the **first 2 samples (`WARMUP_SKIP`) of each metric are always dropped.** This removes
unconverged startup outliers (e.g. `rate_err`'s first 2 samples spike to -3.5) from stats and verdicts. The header shows how many rows were dropped.

> ⚠️ Pitfall of ad-hoc `awk`: `sort -n | tail` catches only the **largest positive** value and **misses negative outliers (-3.5)**.
> This tool judges by absolute value, so it catches outliers on both sides. (That's why we use this tool instead of hand awk.)

---

## 3. `perf_join.py` — internal↔external time alignment

Ties the monotonic clock (`t_ms`) and wall clock (`epoch_s`) via the `epoch_ms_t0` anchor: `event_epoch_ms = epoch_ms_t0 + t_ms`.

```bash
# merge into long format (epoch_ms,t_ms,source,metric,value)
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv -o joined.csv

# correlate: attach the nearest temp/memory at each internal event's time
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv --correlate disp_paint_ms --tolerance 1500 -o corr.csv
```
- `--resource` reads memory·thermal columns from the one combined CSV (individual `--mem`/`--thermal` also accepted).
- Header is parsed by name, so column order doesn't matter.

---

## 4. External sampler (used during measurement)

| Script | Platform | Output | Notes |
|--------|----------|--------|-------|
| `resource_sample.sh` | **ARM/Pi (Linux)** | `resource_ext.csv` | memory (PSS) + thermal combined, 1 Hz |
| `resource_sample.ps1` | **Windows** | `resource_ext.csv` | CPU% + Working Set + Private, 1 Hz. No PSS/thermal (platform N/A) |

> Performance measurement support: **Windows + ARM (Pi)** (Mac is a product target but not a perf-measurement target).
> The analysis tools (`analyze_perf.py`·`perf_join.py`) **auto-detect** both platforms' CSV columns (process only what's present).

`throttled_hex` bits: low 4 bits (bit0 under-voltage · bit1 freq capping · bit2 throttling · **bit3 soft temp limit**) = **current state**,
high 4 bits (bit16–19) = **since-boot history**. "Was THIS measurement throttled?" is judged by the **low 4 bits (current)**.

---

## Typical flow

```bash
# measure (terminal A: app / terminal B: sampler)
./build/TimeGrapher
tools/resource_sample.sh -o resource_ext.csv

# analyze (after the measurement ends)
tools/verify_measurement.sh
tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv --correlate disp_paint_ms -o corr.csv
```
