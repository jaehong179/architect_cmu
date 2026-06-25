# Health Grade & Overhaul Projection — Rationale

This document explains the design basis and thresholds behind the two core judgments in the Timegrapher History front end: the **Health Grade** and the **overhaul (service) date projection**. The logic lives in `src/api/watchApi.js` (`computeHealth`, `projectOverhaulDate`, `normalize`); display happens in `App.jsx` / `HealthGrade.jsx` / `AmplitudeTrend.jsx`.

The thresholds are grounded in the COSC (Swiss Official Chronometer Testing Institute) chronometer standard (ISO 3159) and common timegrapher servicing practice. See *Sources* at the end.

---

## 1. Design principle: separate "accuracy" from "health"

The values a timegrapher outputs mean different things. The distinction commonly used in the trade:

| Value | What it reflects | A health indicator? |
|---|---|---|
| Rate (s/d) | **Accuracy / regulation** | No — freely adjustable |
| Amplitude (°) | **Movement condition** (lubrication, mainspring, power delivery) | **Yes** |
| Beat error (ms) | **Balance / escapement alignment** | **Yes** |
| Positional delta (Δ rate, s/d) | **Consistency of poise / wear state** | **Yes** |

> "Rate tells you accuracy, amplitude tells you health, and beat error tells you balance." — common timegrapher interpretation guidance

**Key decision:** absolute rate only tells you how fast or slow the watch runs, and it can be changed at any time by regulation. A watch running +12 s/d can be mechanically perfect. So **rate is excluded from the health grade** and shown only as a separate "Accuracy / Regulation" item. The health grade is determined solely by **amplitude, positional delta, and beat error**.

---

## 2. Metric definitions (`normalize`)

The following derived values are computed from the per-position raw measurements in the server/mock data.

- **Horizontal amplitude `horizontalAmplitude`** = mean of DU (Dial Up) and DD (Dial Down) amplitude. Falls back to the all-position average if neither is present.
  - *Why horizontal:* amplitude varies strongly by position (vertical positions are structurally 20–50° lower). Convention evaluates **full-wind horizontal** amplitude, so the headline uses the horizontal value rather than an all-position average (the old approach). This keeps a rule like "≥270° is healthy" consistent in meaning.
- **Positional delta `positionalDelta`** = (max per-position rate) − (min per-position rate). This is the **spread**, not the max absolute value.
  - *Why delta:* a watch reading +8 in every position (delta 0) is healthy, just fast. To gauge health you look at **consistency across positions (Δ)**, not absolute rate. COSC also treats positional difference as a core criterion.
- **Max beat error `maxBeatError`** = the maximum beat error across positions (ms).
- **Rate `summary.rate`** = the DU-first reference rate (then DD, then first position). Display only.

---

## 3. Health Grade (`computeHealth`)

### 3.1 Method — "worst-of"

Each of the three indicators is scored on a 4-level scale (4 = Excellent … 1 = Warning), and the **lowest** of the three becomes the overall grade.

> Rationale: mechanical health is governed by its *weakest link*. No matter how good the amplitude is, a beat error of 1.5 ms means the watch is not healthy. So we take the minimum, not the average.

| Overall score | Grade | Color | Meaning |
|---|---|---|---|
| 4 | **Excellent** | green | All three indicators in the top band |
| 3 | **Good** | blue | All within the healthy range |
| 2 | **Fair** | amber | One or more indicators in the caution band — monitor |
| 1 | **Service** | red | One or more indicators below threshold — service recommended |

The UI shows the overall grade together with the **limiting factor** (what dragged the grade down), e.g. *Driven by Amplitude: 254°*.

### 3.2 Per-indicator thresholds and basis

**Amplitude (horizontal, full-wind assumed), °**

| Band | Score | Basis |
|---|---|---|
| ≥ 270 | 4 | A serviced movement's full-wind horizontal amplitude is typically **270–320°**; 280–320 is ideal, 250–300 is good. |
| 230 – 270 | 3 | Normal operating range. |
| 200 – 230 | 2 | Range where service-due signals begin. |
| < 200 | 1 | **Low amplitude in all positions** is the classic service signal. 200° adopted as the service-recommended line. |

**Positional delta Δ (max − min rate), s/d**

| Band | Score | Basis |
|---|---|---|
| ≤ 8 | 4 | Chronometer-grade consistency. COSC constrains positional variation tightly (mean variation ≤2 s/d, horizontal–vertical difference −6/+8 s/d). |
| ≤ 15 | 3 | Practical "good" range for a well-serviced watch. |
| ≤ 25 | 2 | Acceptable, but poise/wear check advised. |
| > 25 | 1 | Suggests condition issues — poor poise, escapement wear, etc. |

**Beat error (max), ms**

| Band | Score | Basis |
|---|---|---|
| ≤ 0.4 | 4 | Factory-fresh / post-service level (0.0–0.3 excellent). |
| ≤ 0.7 | 3 | Good; consistent with the "< 0.5 ms excellent" guideline. |
| ≤ 1.0 | 2 | Practical acceptable limit for daily wear. |
| > 1.0 | 1 | Balance/hairspring misalignment — a service item. Vintage wear can realistically be 1.0–2.0 ms. |

---

## 4. Overhaul (service) date projection (`projectOverhaulDate`)

### 4.1 Assumption and method

**Assumption:** amplitude declines over time due to lubricant aging and wear, and once it drops below the service-recommended line (200°) timekeeping reliability degrades.

**Method:**
1. Fit a **linear regression** to the (time, horizontal amplitude) points from the measurement history.
2. Extrapolate the line to find when amplitude reaches **200°** — the projected service date.

Formula: `t_service = (200 − intercept) / slope`

### 4.2 Statistical gating (why we don't project carelessly)

Amplitude swings by tens of degrees depending on **wind state, temperature, and magnetization** at the time of measurement. Fitting a line to a few points risks fitting noise rather than a trend. So a projected date is only shown when **all** of the following hold (`DEFAULTS`):

| Gate | Value | Reason |
|---|---|---|
| Min measurements | 3 (`min_projection_points`) | Two points are always a line — not a trend |
| Min span | 90 days (`min_projection_days`) | Short windows let noise dominate the trend |
| Goodness of fit R² | ≥ 0.6 (`min_projection_r2`) | The line must explain ≥60% of the variance |
| Min decline | ≥ 10°/yr (`min_decline_deg_per_year`) | Only when there is a meaningful downward trend |

Additionally, if the projected date is **in the past or more than 10 years out**, it is treated as unrealistic extrapolation and not shown.

### 4.3 Three states

- **`projected`** — gates passed. Shows the date plus **confidence (R²)**, annual decline rate (°/yr), and a "rough estimate, affected by wind state and temperature" caveat.
- **`stable`** — enough data but no meaningful downward trend → "Stable — no service date projected."
- **`insufficient`** — too few measurements / too short a span → explains what more is needed.

### 4.4 Confidence labeling

R² ≥ 0.9 → high · ≥ 0.75 → moderate · otherwise → low confidence. The estimate is **always labeled "rough estimate."**

---

## 5. Configuration summary (`DEFAULTS`)

| Key | Value | Purpose |
|---|---|---|
| `baseline_amplitude` | 290° | Healthy reference amplitude (top of chart) |
| `critical_threshold_amplitude` | 200° | Service-recommended line (chart dashed line + projection target) |
| `min_projection_points` | 3 | Min measurements to project |
| `min_projection_days` | 90 | Min span to project |
| `min_projection_r2` | 0.6 | Min goodness of fit to project |
| `min_decline_deg_per_year` | 10°/yr | Min decline slope to project |

Because normal amplitude ranges differ by movement (caliber), these values are designed as **adjustable heuristics**.

---

## 6. Limitations and caveats

- **Thresholds are standards, not hard lines.** 200° amplitude, 1.0 ms beat error, etc. are reasonable cutoffs grounded in industry practice and COSC — not universal physical constants applicable to every caliber.
- **Full-wind assumption.** Amplitude evaluation assumes full-wind horizontal. If the watch was under-wound at measurement, amplitude reads low — so controlling/recording measurement conditions is ideal.
- **Linear decay assumption.** Real amplitude decay is often non-linear. The projection is a trend-based reference and should be weighed alongside the conventional service interval (typically 4–6 years, varies by movement).
- **External disturbances.** A transient factor — winter oil viscosity, magnetization — can swing a single measurement point sharply. R² gating filters some of this, but not all.

---

## Sources

- [COSC — Chronometer Certified](https://www.cosc.swiss/certified-chronometer)
- [COSC — Wikipedia (ISO 3159, seven criteria)](https://en.wikipedia.org/wiki/COSC)
- [WatchGecko — Complete Guide to COSC Certification](https://www.watchgecko.com/blogs/magazine/guide-cosc)
- [Tufina — Understanding 270–310° Amplitude](https://tufinawatches.com/blogs/news/understanding-270-310-degrees-amplitude-in-watches)
- [Beyond the Dial — Interpreting Timegrapher Results](https://www.beyondthedial.com/post/collector-guide-interpreting-timegrapher-results/)
- [Rotate Watches — What Is Beat Error (acceptable values)](https://rotatewatches.com/blogs/blog/what-is-beat-error-in-a-watch)
- [Rotate Watches — How to Use a Timegrapher](https://rotatewatches.com/blogs/blog/how-to-use-timegrapher)
