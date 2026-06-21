# Rate/Scope 탭 그래프 개선 계획

Rate/Scope 탭의 rate 오차 그래프와 실시간 scope 그래프를 **정확하게 표시**하고, **가독성·일관성**을 높이기 위한 코드 분석 및 개선 계획입니다.

> **대상 코드:** `src/ui/tabs/TabRateScope.cpp`, `src/ui/tabs/TabRateScope.h`, `src/engine/MeasurementEngine.cpp`  
> **목표:** 현재 측정 정보는 유지하면서, rate 흐름·trigger 기준·A/C 이벤트·amplitude 라벨을 더 빠르게 이해할 수 있게 한다.  
> **재사용 헬퍼:** `ReadoutBar.h`, `LegendBox.h`, `PlotHelpers.h`

---

## 1. 현재 구조 개요

Rate/Scope 탭은 [`TabRateScope.cpp`](../../src/ui/tabs/TabRateScope.cpp) 한 파일(369줄)에 두 개의 QCustomPlot을 담는다.

| 플롯 | 역할 | 데이터 소스 |
|------|------|------------|
| **RatePlot** | tic/toc Rate 오차(ms) 시계열 산포도 | `onMeasurement()` |
| **ScopePlot** | 실시간 엔벨로프 파형 + A/C 이벤트 마커 | `onWave()` |

```
TabRateScope (QVBoxLayout)
 ├─ ctlRow (QHBoxLayout)  ← "Scope Scale" 라벨 + QSpinBox(1~8)
 ├─ mRatePlot  (stretch 1)
 └─ mScopePlot (stretch 1)
```

### 1.1 데이터 흐름

**RatePlot** — `onMeasurement()`가 `MeasurementSnapshot`에서 `rateTicX/Y`, `rateTocX/Y`를 복사한다.

- 데이터 출처: `MeasurementEngine::computeRateError()`
- X값: `TimeMeasured` (세션 경과 초)
- Y값: `WrappedRateError` (−10 ms ~ +10 ms wrap)
- 최대 점 수: `ERROR_RATE_X_DATA_POINTS = 250` (순환 버퍼)

**ScopePlot** — `onWave()`가 `WaveBlock`에서 envelope와 event를 받아 그린다.

- `graph(0)`: rectified/envelope
- `graph(1)`: onset trigger threshold
- A event: 녹색 수직 점선
- C event: 빨간 수직 점선
- A-A 구간: 수평 화살표 + ms 라벨
- A-C 구간: 수평 화살표 + ms/amplitude 라벨
- 고샘플레이트: min/max decimation으로 피크 보존하며 렌더 점 수 축소
- 히스토리: `GRAPH_HISTORY_IN_SECONDS = 10` 초 기준 `purgeHistory()`

---

## 2. 발견된 문제

### 2.1 🔴 RatePlot X축 의미 불일치 (기능 버그 — P0)

`applyFixedRateXAxis()`는 RatePlot X축을 **항상 0.0 ~ 1.0 s**로 고정한다.

```cpp
mRatePlot->xAxis->setRange(0.0, kScopeWindowBaseSec);  // kScopeWindowBaseSec = 1.0
```

그러나 `MeasurementEngine`은 세션 경과 시간(`TimeMeasured`)을 X값으로 넣는다. 측정이 1초를 넘으면 rate 점들이 **화면 밖으로 사라진다**.

ScopePlot에는 이미 `syncScopeXAxis()` 슬라이딩 윈도우가 있으나, RatePlot에는 없다. 미관보다 우선순위가 높다 — rate 흐름을 보지 못하면 RatePlot의 본래 목적이 약해진다.

> **구현 시 주의:** tic/toc 데이터는 순환 버퍼(`addOrOverwrite`)이므로 벡터 순서가 시간순이 아닐 수 있다. 슬라이딩 윈도우 적용 시 **tic/toc 전체에서 최신 X값을 scan**해야 한다.

---

### 2.2 🔴 정보 계층 부재 — ReadoutBar 없음

다른 탭(`TabBeatNoiseScope`, `TabSyncSweepScope` 등)은 상단에 `ReadoutBar`(RATE | BEAT ERROR | AMPLITUDE | BPH)가 있다. Rate/Scope 탭만 없는 불일관 상태이며, **가장 기본 탭**이므로 오히려 readout이 가장 필요하다.

---

### 2.3 🟡 ScopePlot Y축 매 프레임 출렁임

`onWave()` 끝에서 `mScopePlot->yAxis->rescale()`을 매번 호출한다. envelope peak가 순간적으로 변하면 Y축 범위가 계속 바뀌어 화면이 흔들린다.

`TabBeatNoiseScope`의 `smoothPeak()` 패턴(상승 즉시·하강 천천히)을 차용하면 안정적이다.

---

### 2.4 🟡 범례·설명 부족

`Rectified`, `Trigger`, `Tic Rate`, `Toc Rate` 이름은 QCustomPlot 기본 범례에 있으나, A/C marker·수평 화살표·ms/amplitude 라벨의 의미는 별도 설명이 없다. 초보 사용자에게 A/C/trigger/rectified envelope 관계가 직관적이지 않다.

기존 `LegendBox` 헬퍼(`makeLegendBox`)를 다른 탭과 동일하게 추가할 수 있다.

---

### 2.5 🟡 시각적 품질 — 색상 / 스타일

| 위치 | 현재 코드 | 문제 |
|------|-----------|------|
| ScopePlot graph(0) (파형) | `Qt::blue` + `QColor(0,0,255,20)` 채움 | 채도 과잉, 앱 내 다른 탭(황금 envelope)과 불일치 |
| ScopePlot graph(1) (임계선) | `Qt::red`, 1px | 데이터 선과 동일 두께 → 구분 어려움 |
| RatePlot tic 산점 | `Qt::red`, 3px disc | Qt 기본 red(255,0,0)는 너무 선명 |
| RatePlot toc 산점 | `Qt::blue`, 3px disc | Qt 기본 blue(0,0,255)는 너무 선명 |
| 수직 마커 A | `Qt::green` DashLine 2px | A/C 형태 동일 → 색각 이외 구분력 낮음 |
| 수직 마커 C | `Qt::red` DashLine 2px | 위와 동일 |
| 수평 브라켓 | `Qt::black` SolidLine | 텍스트 라벨과 같은 색 → 시각 계층 부재 |
| 텍스트 라벨 | `setPen(QPen(color))` 테두리, 배경 없음 | 파형 위 겹치면 읽기 어려움 |

**핵심:** Qt 기본색만 사용 → "무지개 그래프"처럼 보이고, `TabBeatNoiseScope` 등과 팔레트가 어긋난다.

---

### 2.6 🟡 레이아웃 / 컨트롤 UX

#### 컨트롤 바
```
[Scope Scale] [SpinBox 1-8]     ← 텍스트만, 단위 없음
```
- "Scope Scale 4 = 0.25초 창"이라는 의미를 사용자가 알 수 없음
- 현재 창 범위(예: `0.25 s`)가 어디에도 표시되지 않음
- 값이 커질수록 `windowSec = 1.0 / scale` → **더 좁은** 창. scale보다 zoom/window length에 가깝다

#### 플롯 비율
- 두 플롯이 `stretch(1:1)` 동등 분할 → 실시간 파형(ScopePlot)이 상대적으로 작음
- Y축 레이블 `"Rate Error (milliseconds)"` — 축 이름과 단위 표기 혼재

#### 마커 텍스트
```cpp
textLabel->setPen(QPen(color));   // 테두리 = 지저분함
// text = " 12.34 ms "  (앞뒤 공백, 소수 2자리)
```

---

### 2.7 🟢 코드 구조 — DRY / 일관성

`setupPlots()` + `applyFixedRateXAxis()`에 걸친 4줄 폰트 패턴이 반복된다.

```cpp
QFont tickFont = mRatePlot->xAxis->tickLabelFont();
tickFont.setPointSize(10);
mRatePlot->xAxis->setTickLabelFont(tickFont);
QFont labelFont = mRatePlot->xAxis->labelFont();
labelFont.setPointSize(10);
mRatePlot->xAxis->setLabelFont(labelFont);
```

색상도 `Qt::red/blue/green` 하드코딩 → `Theme::` 같은 상수 테이블 없음.

> `PlotHelpers.h`는 *"시각에 영향 없는 동작만"* 의도이므로, `applyAxisStyle` 추가는 DRY에 도움이 되나 **낮은 우선순위**다.

---

## 3. 개선 방향

### 3.1 P0 — RatePlot X축 슬라이딩 윈도우

ScopePlot의 `syncScopeXAxis()` 패턴을 RatePlot에 적용한다.

**권장안:**
- X축을 최근 **10초** 슬라이딩 윈도우로 변경 (`GRAPH_HISTORY_IN_SECONDS` 재사용)
- `onMeasurement()`에서 tic/toc 데이터의 최신 X값을 찾아 `setRange(latest, windowSec, Qt::AlignRight)` 적용
- X축 라벨: `time (s)` 유지 (필요 시 `session time (s)`)

**기대 효과:**
- rate 점이 1초 이후에도 계속 보임
- tic/toc rate timing error의 최근 추세를 바로 확인 가능

---

### 3.2 P1 — ReadoutBar 추가

다른 탭과 동일하게 상단에 `ReadoutBar`를 추가한다.

```cpp
mBar = new ReadoutBar(this);
lay->insertWidget(0, mBar);   // 최상단

// onMeasurement() 에서:
mBar->update(snap);
```

**제안 레이아웃:**

```text
+--------------------------------------------------+
| RATE | BEAT ERROR | AMPLITUDE | BPH              |  ← ReadoutBar
+--------------------------------------------------+
| Legend / guide (접이식)                          |  ← LegendBox
+--------------------------------------------------+
| Scope Zoom: [SpinBox]   Window: 0.25 s           |  ← 컨트롤 바
+--------------------------------------------------+
| RatePlot                              stretch 1  |
+--------------------------------------------------+
| ScopePlot                             stretch 2  |
+--------------------------------------------------+
```

---

### 3.3 P1 — LegendBox (접이식 설명) 추가

`makeLegendBox()`로 짧은 그래프 읽기 안내를 추가한다.

| 항목 | 설명 |
|------|------|
| Tic/Toc Rate | 빨강/파랑 점 = tic/toc timing error |
| Rectified | envelope / 정류된 신호 |
| Trigger | onset 검출 기준선 |
| A marker | beat 시작 이벤트 |
| C marker | lock/drop 이벤트 |
| A-C label | ms 간격과 계산된 amplitude |

접이식이므로 그래프 영역 확보와 학습 비용 절감을 동시에 달성한다.

---

### 3.4 P2 — 시각 스타일 정리

`TabBeatNoiseScope`와 어울리는 팔레트를 적용한다 (`TabRateScope.cpp` 상단 `namespace Theme`).

```cpp
namespace Theme {
    // ScopePlot 파형 — BeatNoiseScope 황금 envelope 계열
    constexpr QColor kEnvelope  {120, 110,   0};
    constexpr QColor kEnvFill   {235, 215,   0, 120};
    constexpr QColor kThreshold {220, 120,   0};   // 주황 임계선 (red envelope와 구분)
    // 이벤트 마커
    constexpr QColor kMarkerA   {  0, 160,   0};   // A: beat 시작
    constexpr QColor kMarkerC   {220,  40,  40};   // C: lock/drop
    constexpr QColor kBracket   {100, 100, 120};   // 수평 브라켓 (회보라)
    constexpr QColor kLabelBg   {255, 255, 255, 200};
    // RatePlot scatter
    constexpr QColor kTicColor  {200,  40,  40};
    constexpr QColor kTocColor  { 40,  80, 200};
    // 그리드 / 0선
    constexpr QColor kZeroLine  { 80,  80,  80};
    constexpr QColor kGrid      {210, 210, 215};
}
```

| 대상 | 현재 | 권장 |
|------|------|------|
| Tic Rate | `Qt::red` | `Theme::kTicColor` |
| Toc Rate | `Qt::blue` | `Theme::kTocColor` |
| Envelope | blue fill | `Theme::kEnvelope` + `Theme::kEnvFill` |
| Trigger | `Qt::red` | `Theme::kThreshold` |
| A marker | green dash | `Theme::kMarkerA`, **SolidLine 2px** |
| C marker | red dash | `Theme::kMarkerC`, **DashLine 1px** |
| Text label | black + pen border | dark gray text + `Theme::kLabelBg` |

**마커 A/C 스타일 차별화 (색 + 형태):**
```cpp
pen.setStyle(isEventA ? Qt::SolidLine : Qt::DashLine);
pen.setWidth(isEventA ? 2 : 1);
```

**텍스트 라벨 개선 (`addText()`):**
```cpp
textLabel->setPen(Qt::NoPen);
textLabel->setBrush(Theme::kLabelBg);
textLabel->setPadding(QMargins(4, 1, 4, 1));
textLabel->setFont(QFont("monospace", 9));
// "12.3 ms" — 소수 1자리, 여백은 padding으로
text = QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
```

**RatePlot 0선·그리드 (선택, `setupPlots()`):**
```cpp
mRatePlot->yAxis->setZeroLinePen(QPen(Theme::kZeroLine, 1, Qt::SolidLine));
mRatePlot->xAxis->grid()->setPen(QPen(Theme::kGrid, 1, Qt::DotLine));
mRatePlot->yAxis->grid()->setPen(QPen(Theme::kGrid, 1, Qt::DotLine));
```

**Y축 라벨 정리:** `"Rate error"` + tick formatter로 ms 단위 분리.

---

### 3.5 P2 — Scope Y축 안정화

`mScopePlot->yAxis->rescale()` 대신 `smoothPeak` 기반 범위를 사용한다.

```cpp
// TabRateScope.h 멤버 추가
double mScopePeakNorm = 0.0;

// TabBeatNoiseScope 와 동일 패턴
static double smoothPeak(double &norm, double inst) {
    if (inst > norm) norm = inst;
    else norm = 0.92 * norm + 0.08 * inst;
    if (norm < 1e-9) norm = 1e-9;
    return norm;
}

// onWave() 에서:
const double ymax = smoothPeak(mScopePeakNorm, currentMax);
mScopePlot->yAxis->setRange(0, ymax * 1.12);
// mScopePlot->yAxis->rescale();  ← 제거
```

`onResetSession()`에서 `mScopePeakNorm = 0.0` 초기화.

---

### 3.6 P3 — Scope Scale / 레이아웃 UX

**컨트롤 명칭·표시:**
- 라벨을 `Scope Zoom` 또는 `Scope Window`로 변경
- 컨트롤 옆에 현재 창 크기 QLabel 추가: `"Window: 0.25 s"`
- 장기: ms 단위 QComboBox (`1000 ms`, `500 ms`, `250 ms`, `125 ms`)
- 초기 구현: 기존 `QSpinBox` 유지 + 라벨/Window 표시만 추가 (최소 변경)

**플롯 비율:** ScopePlot stretch **2**, RatePlot stretch **1**.

---

### 3.7 P4 — 축 스타일 헬퍼 DRY (선택)

`PlotHelpers.h`에 추가:

```cpp
inline void applyAxisStyle(QCPAxis *axis, int fontSize = 10)
{
    QFont tf = axis->tickLabelFont(); tf.setPointSize(fontSize);
    axis->setTickLabelFont(tf);
    QFont lf = axis->labelFont(); lf.setPointSize(fontSize);
    axis->setLabelFont(lf);
}
```

호출: `PlotHelpers::applyAxisStyle(mScopePlot->xAxis);` 등 — 기존 8줄 → 2줄.

---

## 4. 구현 우선순위 요약

| 우선순위 | 영역 | 변경 내용 | 관련 파일 |
|---------|------|-----------|-----------|
| 🔴 P0 | RatePlot X축 | 슬라이딩 윈도우 (최근 10초) | `TabRateScope.cpp/.h` |
| 🔴 P1 | ReadoutBar | 상단 readout 추가 | `TabRateScope.cpp/.h` |
| 🔴 P1 | LegendBox | A/C/trigger 설명 (접이식) | `TabRateScope.cpp` |
| 🟡 P2 | 시각 스타일 | `Theme::` 팔레트, 마커 A/C 차별화, 텍스트 배경 | `TabRateScope.cpp` |
| 🟡 P2 | Y축 | `smoothPeak` 안정화 | `TabRateScope.cpp/.h` |
| 🟡 P2 | 그리드 | 0선 강조, 점선 그리드 (선택) | `TabRateScope.cpp` |
| 🟢 P3 | 레이아웃 | ScopePlot stretch 2:1, Window 라벨, Zoom 명칭 | `TabRateScope.cpp/.h` |
| 🟢 P4 | 코드 | `PlotHelpers::applyAxisStyle` DRY (선택) | `PlotHelpers.h`, `TabRateScope.cpp` |

---

## 5. 권장 1차 구현 순서

> **P0 + P1을 먼저** 적용하면 기능 정확성과 UX 일관성을 최소 변경으로 확보한다.  
> 이후 P2(시각) → P3(레이아웃) → P4(DRY) 순으로 진행한다.

1. RatePlot X축 슬라이딩 윈도우 (10초)
2. `ReadoutBar` 상단 추가
3. `LegendBox` 접이식 추가
4. `addText()` 반투명 배경 + padding
5. Scope Y축 `smoothPeak`
6. `Theme::` 색상 팔레트 + A/C 마커 스타일 차별화
7. ScopePlot stretch 2:1 + Window 라벨 + Zoom 명칭
8. 0선·그리드, `applyAxisStyle` (선택)

각 항목은 서로 독립적이므로 필요한 항목만 골라 순차 적용해도 된다.

---

## 6. 예상 코드 변경 범위

### 6.1 `TabRateScope.h` 추가 멤버

```cpp
ReadoutBar *mBar = nullptr;
double mScopePeakNorm = 0.0;
double mRateWindowSec = 10.0;   // GRAPH_HISTORY_IN_SECONDS 와 동기

// helper (private):
void applyRateXAxisFromData(const QVector<double>& tx, const QVector<double>& ox);
double smoothScopeYAxis(double currentPeak);
```

### 6.2 `TabRateScope.cpp` 주요 변경 위치

| 위치 | 변경 |
|------|------|
| 생성자 | `ReadoutBar`, `LegendBox` 추가, stretch 2:1, Window QLabel |
| `setupPlots()` | `Theme::` 색상, grid, legend, axis font |
| `onMeasurement()` | `mBar->update(snap)`, RatePlot X축 동기화 |
| `onWave()` | current peak 계산, Y축 smoothing |
| `addText()` | 배경/padding/font |
| `addVerticalMarker()` | A/C 스타일 차별화 |
| `onResetSession()` | `mScopePeakNorm` 초기화 |

### 6.3 기존 공용 헬퍼 재사용

새 추상화를 크게 만들기보다 기존 헬퍼를 재사용한다.

- `ReadoutBar.h` — `mBar->update(snap)`
- `LegendBox.h` — `makeLegendBox(tableHtml, parent)`
- `PlotHelpers.h` — (선택) `applyAxisStyle`
- `TabBeatNoiseScope.cpp` — `smoothPeak()` 패턴 참조

---

## 7. 검증 계획

### 기능
- Live / Playback / Sim에서 Rate/Scope 탭 정상 표시
- RatePlot 점이 **1초 이후에도** 계속 보이는지 확인
- Scope Scale / Zoom 변경 시 X축 창 정상 변경
- Reset 후 두 그래프·readout·Y축 smoothing 상태 초기화

### 시각
- A/C marker·라벨이 envelope와 겹쳐도 읽히는지
- Trigger line과 envelope 색상 구분
- Y축 과도한 출렁임 없음
- LegendBox 접었을 때 그래프 영역 충분

### 성능
- 48 / 96 / 192 / 384 kHz에서 scope replot 끊김 없음
- min/max decimation 동작 유지
- `PERF_ENABLE` 시 `scopeReplotted()` 연결 유지
