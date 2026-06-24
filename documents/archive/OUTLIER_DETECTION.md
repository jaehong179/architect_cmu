# 이상치 검출 (Outlier Detection)

> **rate** 측정에서 그 시계가 보여온 정상 추세에서 **갑자기 벗어난 비트**를 데이터 적응형으로 검출하고,
> ① rate(RLS 회귀)·추세 적합에서 **제외**하며, ② 해당 A이벤트에 **플래그를 박아** 점으로 표시한다.
> 현재 운영은 **rate 단일 지표**. beat error·amplitude 는 로직을 보존하되 토글로 비활성(언제든 부활).
> 코드: `src/core/stats/RollingOutlier.h`, `src/engine/MeasurementEngine.*`, `src/engine/CaptureController.cpp`,
> 표시 = `TabRateScope`/`TabWaveformCompare`(Paperstrip)/`TabEscapementAnalyzer`/`TabBeatErrorTrace`.

---

## 0. 핵심 성질 — "표시 출력"이 아니라 "원시 비트값" 기반

- 검출 입력 = 엔진이 비트마다 계산하는 **원시 측정값**(스무딩/평균 전): 비트 타이밍오차(`InstTimingError`, s).
- 롤링 윈도우는 **baseline(중앙값+MAD) 산출 용도**일 뿐, 그래프에 그려지는 평균/데시메이션 출력과 무관.
- 따라서 "이상치를 평균/회귀에서 제외 → 표시값이 이상치에 안 끌림" 이 가능하다.

---

## 1. 검출 알고리즘 — 로버스트 롤링 (median + MAD + MAD floor)

[`RollingOutlier`](../../src/core/stats/RollingOutlier.h) (순수 C++, Qt 비의존):

- 최근 N개 값의 **중앙값(median)** 과 **MAD(median absolute deviation)** 로 modified z-score 계산:
  ```
  mz = 0.6745 · |v − median| / max(MAD, madFloor) ,   이상치 ⇔ mz > k
  ```
- **왜 median/MAD?** 평균·표준편차는 이상치 한 점에 휘둘리지만, median/MAD 는 **robust** 해서
  "정상 패턴에서 벗어난 값"을 안정적으로 가려낸다. 롤링이라 드리프트는 따라가고 급변만 튄다.
- 판정은 새 값 `v` 를 baseline 에 넣기 **전** 분포로 하고, 그 뒤 baseline 에 추가(자기 자신에 안 휘둘림).
- **MAD floor**: baseline 이 비정상적으로 조용해(MAD≈0) 미세 잔차로 `mz` 가 폭주하는 가짜 양성을 막는다.
  평소 MAD 보다 충분히 작게 두면 collapse 때만 작동. `MAD≈0` 이면 판정 보류(false-storm 방지).
- 파라미터: `RollingOutlier(window, k, warmup, madFloor)`.

---

## 2. Rate 검출 — 추세 제거(detrend) 후 잔차로

`InstTimingError` 는 시계 rate 만큼 **우상향 추세 + 곡률**이 있어, 원시값에 median/MAD 를 그대로 쓰면
정상적으로 추세를 따라간 값까지 이상치로 오검출하거나, 추세에 묻혀 급변을 놓친다. 그래서:

1. **단기 추세 적합**: rate 전용 짧은 윈도우 RLS `DetrendTic/DetrendToc`(window=12)로 국소 직선 추세를 예측
   (`RollingLeastSquares::Predict` → `ŷ = ȳ + slope·(x − x̄)`).
2. **잔차로 검출**: `residual = InstTimingError − 추세예측` 을 `RollingOutlier{60, 4.0, 12, 5e-6}` 에 push.
   → 위치 무관 균일 민감도. 잔차는 0 주변 정상신호라 윈도우를 길게(60) 둬도 안전.
3. **추세 오염 방지**: 이상치 비트는 detrend 적합(`DetrendTic/Toc->AddPoint`)에도 안 넣는다.

| 파라미터 | 값 | 의미 |
|---|---|---|
| window | 60 | 잔차 baseline 길이 |
| k | **4.0** | 임계(robust σ 배수). 진짜(z≫9)와 곡률/노이즈 가짜(z≈3) 사이를 가름 |
| warmup | 12 | 초기 baseline 형성 전 판정 보류 |
| madFloor | **5e-6 s** (≈0.005 ms) | 관측 정상 MAD(~0.02ms)의 1/3 — collapse 때만 작동 |

> beat error·amplitude 검출은 코드(`mBeat.Outlier`/`mAmp.Outlier`, 평평 신호라 detrend 없이 원시값)는 그대로 두되
> `MeasurementEngine.cpp` 상단 `kBeatOutlierEnabled/kAmpOutlierEnabled=false` 로 비활성.
> `&&` 단락이라 `push()` 자체가 호출 안 됨(계산 부하 0). `true` 로 바꾸고 리빌드하면 즉시 부활.

---

## 3. 평균/회귀에서 제외 (표시값 보호)

- **Rate**: 이상치면 `RlsTicRate/RlsTocRate->AddPoint()` **건너뜀** → rate(RLS 기울기)가 한 비트에 안 휘둘림.
  단 Rate/Scope 산점도의 점(xTic/yTic)은 **표시는 유지**(원시 비트가 보이도록).
- beat error·amplitude: 검출 비활성이라 `LastOutlier` 가 항상 false → **항상 평균에 포함**(제외 없음).

---

## 4. 표시 전파 — 두 경로

검출은 **엔진 1곳**. 점 탭은 자체 검출 없이 엔진 결과만 사용(일원화·일관성). 전파는 두 가지:

### (A) A이벤트에 박는 플래그 — Paperstrip·Escapement·Beat Error (권장 경로)
```
엔진 onAEvent → mRate.LastOutlier 확정
      │  (CaptureController 가 aEvent() 직후 mEngine.lastRateOutlier() 조회)
      ▼
WaveEvent.outlier = true  (그 A이벤트 자체에 박음)
      │  broadcastWave
      ▼
점 탭들이 onset/beat 누적 시 event.outlier 를 병렬 보관 → 그 점만 주황으로
```
- **왜 sample 매칭이 아니라 플래그?** 엔진 A이벤트는 `sample_index + sub_sample_offset`(실수)라
  `(uint64_t)` 절단 시 offset 부호에 따라 onset 의 정수 `sample_index` 와 ±1 어긋나 매칭이 불안정했다.
  검출 직후 **이벤트에 직접 박으면** 부동소수점 오차가 원천적으로 없다.

### (B) 엔진 점별 링 마스크 — Rate/Scope
- Rate/Scope x 는 시간이 아니라 **링버퍼 인덱스**(덮어쓰기)라 (A) 대신 점별 마스크를 쓴다.
- 엔진이 `yTicOut/yTocOut`(이상치면 그 y, 아니면 NaN)을 같은 링 위치에 동기 기록 →
  `snapshot.rateTicOutY/rateTocOutY` 로 전달 → 이상치 점만 주황으로.

> snapshot 의 `rateOutlier/beatErrorOutlier/amplitudeOutlier` 플래그는 라인 음영(아래 5절)용으로 남아 있으나 현재 미사용.

---

## 5. 마킹 — 탭별

| 탭 | 좌표 | 마킹 | 상태 |
|---|---|---|---|
| **Rate/Scope** | 링 인덱스 | tic·toc 이상치 점 **주황 디스크** | ✅ |
| **Paperstrip** (Waveform Compare) | 폴딩 위상 | tic·tac 이상치 점 **주황 디스크** | ✅ |
| **Escapement Analyzer** | 가운데 점열 | Tic·Tac 이상치 비트 **주황 디스크** | ✅ |
| **Beat Error Display** | beat# | tic·toc trace 위 이상치 비트에 **주황 점 겹침**(선 연속성 유지) | ✅ |
| **Long-Term / Trace** | 시간 | rate·amp 이상치 시각 **배경 빨강 음영** | ⛔ off (`kShowAnomalyShade=false`) |

- 주황 = `QColor(255,140,0)` `ssDisc`. 4개 점 탭이 같은 검출 결과를 쓰므로 **동일 이상치 집합**.
- 라인 음영(LongTerm/Trace): 코드·토글은 보존하되 기본 off. off 일 때는 누적(`detect`/`anomX`)·그리기
  모두 건너뛰어 **불필요 계산을 안 한다**. `kShowAnomalyShade=true` 로 부활.

---

## 6. 리셋

- **세션 리셋**: 검출기 baseline·`LastOutlier`·detrend·탭별 플래그 버퍼 모두 초기화.
- **BPH 재동기**(`computeRateError` 재동기 블록): 측정 baseline 과 함께 검출기·detrend 도 reset.

---

## 7. 튜닝 노브

| 목적 | 조정 (위치) |
|---|---|
| 더/덜 민감 | rate `k` ↓/↑ — `MeasurementEngine.h` 의 `RollingOutlier Outlier{60, k, 12, 5e-6}` |
| collapse 가짜 차단 강도 | `madFloor` (같은 줄) |
| 추세 적합 반응성 | `DetrendTic/Toc` 윈도우(12) |
| beat/amp 부활 | `MeasurementEngine.cpp` `kBeatOutlierEnabled/kAmpOutlierEnabled=true` |
| 라인 음영 부활 | `TabLongTermPerformance.h`/`TabTraceDisplay.h` `kShowAnomalyShade=true` |

---

## 8. 파일 맵

```
src/core/stats/RollingOutlier.h         검출 알고리즘(median+MAD+floor, Qt 비의존)
src/core/stats/RollingLeastSquares.*    Predict() — detrend 추세 예측
src/engine/MeasurementEngine.{h,cpp}    rate 검출(detrend 잔차) + RLS 제외 + LastOutlier + 점별 마스크
src/engine/CaptureController.cpp        검출 직후 WaveEvent.outlier 박기(점 탭 공용 플래그)
src/ui/tabs/MeasurementModel.h          WaveEvent.outlier + snapshot 플래그/마스크 포인터
src/ui/MainWindow.cpp                    Results → snapshot 복사(점별 마스크 포함)
src/ui/tabs/TabRateScope.cpp            이상치 점 주황(점별 마스크 경로)
src/ui/tabs/TabWaveformCompare.*        Paperstrip 이상치 점 주황(event.outlier)
src/ui/tabs/TabEscapementAnalyzer.*     가운데 점열 이상치 주황(event.outlier)
src/ui/tabs/TabBeatErrorTrace.cpp       beat# trace 이상치 주황 겹침(event.outlier)
src/ui/tabs/TabLongTermPerformance.*    라인 배경 음영(현재 off)
src/ui/tabs/TabTraceDisplay.*           라인 배경 음영(현재 off)
```

---

## 9. 요약

- **데이터 적응형**(고정 임계 없음) **median+MAD+floor** 로 rate 의 **detrend 잔차**에서 이상치 검출(엔진 1곳).
- 이상치는 **rate 회귀에서 제외** → 표시값이 안 흔들림.
- 전파: **A이벤트에 플래그를 박아**(부동소수점 매칭 제거) 4개 점 탭이 동일하게 **주황 점** 표시.
  Rate/Scope 만 링 인덱스 특성상 엔진 점별 마스크 경로.
- beat error·amplitude 와 라인 음영은 **토글로 비활성**(코드 보존, 계산 부하 0, 즉시 부활).
