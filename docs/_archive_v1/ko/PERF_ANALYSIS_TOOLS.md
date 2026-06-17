# 성능 측정 분석 도구 (Analysis Tools)

> 측정 산출물(`build/perf_log.csv` + `resource_ext.csv`)을 **검증·분석**하는 `tools/` 스크립트 모음.
> 측정 절차는 → [PI_MEASUREMENT_CHECKLIST.md](PI_MEASUREMENT_CHECKLIST.md), 합격기준은 → [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md).
> 모두 **설치 불필요**(bash/awk + 파이썬 표준 라이브러리). pandas 안 씀.

---

## 한눈에 (측정 끝나면 이 두 줄)

```bash
tools/verify_measurement.sh                                            # ① 제대로 기록됐나
tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv   # ② 통계·판정·발열영향
```

---

## 1. `verify_measurement.sh` — 기록 정합성 점검

측정값이 **제대로 기록됐는지**(합격/불합격 판정이 아니라) 자동 점검.

```bash
tools/verify_measurement.sh [perf_log.csv] [resource_ext.csv]   # 인자 없으면 기본 경로
```

점검 항목:
- **내부**: 정렬 앵커(`epoch_ms_t0`) 존재 · 자원지표(cpu/rss/throttle) 부재(외부로 이전됨) · 지표 행수 · NaN/inf 없음
- **외부**: PSS·온도가 합리적 범위로 채워짐 · `now_throttling` 이 `throttled_hex` 와 일치(비트로직 정상)
- **정렬**: `perf_join.py` 가 두 로그를 합치는지
- 메모리는 **증가량 평가 없이** 기록 여부(min/max)만 본다.

---

## 2. `analyze_perf.py` — 통합 분석 리포트 ★

지표별 통계 + QA 목표 대비 + 외부 자원 요약 + 발열 영향을 한 번에.

```bash
tools/analyze_perf.py [perf_log.csv] [--resource resource_ext.csv] [--bucket <지표>]
```

출력:
1. **지표별 통계** — n · 평균 · 중앙 · p95 · p99 · 최악. (error 지표는 절대값 최대를 '최악'으로)
2. **QA 목표 대비** — 알려진 지표(`ui_loop_lag`·`rate/beaterr/amp_err`·`e2e_full`·`onset/peak_err`)에 ✅/❌.
3. **외부 자원** — PSS(증가량은 표시만, 평가 안 함)·최고온도·클럭범위·측정중 스로틀 비율.
4. **발열 영향** — 지정 지연 지표를 **클럭대별로 분해**. 클럭 무관하게 값이 같으면 *CPU 클럭 바운드가 아님*(렌더/GPU 바운드).

★ **워밍업 자동 제외**: 각 지표의 **첫 2샘플(`WARMUP_SKIP`)을 무조건 버린다.** 시작 직후 미수렴 이상치
(예: `rate_err` 첫 2샘플이 -3.5로 튐)를 통계·판정에서 제거하기 위함. 헤더에 제외 행수를 표시한다.

> ⚠️ 즉석 `awk`의 함정: `sort -n | tail` 식은 **최대 양수**만 잡아 **음수 이상치(-3.5)를 놓친다.**
> 이 툴은 절대값 기준으로 보므로 양/음 양쪽 이상치를 다 잡는다. (그래서 손 awk 대신 이 툴을 쓴다.)

---

## 3. `perf_join.py` — 내부↔외부 시간축 정렬

단조시계(`t_ms`)와 벽시계(`epoch_s`)를 `epoch_ms_t0` 앵커로 묶는다: `event_epoch_ms = epoch_ms_t0 + t_ms`.

```bash
# 통합 롱포맷(epoch_ms,t_ms,source,metric,value)
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv -o joined.csv

# 상관: 내부 지표 이벤트마다 '그 시각의' 온도/메모리 최근접 첨부
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv --correlate disp_paint_ms --tolerance 1500 -o corr.csv
```
- `--resource` 합본 CSV는 메모리·발열 컬럼을 한 파일에서 읽는다(개별 `--mem`/`--thermal` 도 가능).
- 헤더는 이름 기반 파싱이라 컬럼 순서가 달라도 안전.

---

## 4. 외부 샘플러 (측정 시 사용)

| 스크립트 | 플랫폼 | 출력 | 비고 |
|----------|--------|------|------|
| `resource_sample.sh` | **ARM/Pi (Linux)** | `resource_ext.csv` | 메모리(PSS)+발열 합본, 1Hz |
| `resource_sample.ps1` | **Windows** | `resource_ext.csv` | CPU%+WorkingSet+Private, 1Hz. PSS·발열 없음(플랫폼 미지원) |

> 성능 측정 지원: **Windows + ARM(Pi)** (Mac 제품은 지원하나 성능 측정 대상 아님).
> 분석 도구(`analyze_perf.py`·`perf_join.py`)는 두 플랫폼 CSV 컬럼을 **자동 인식**한다(있는 지표만 처리).

`throttled_hex` 비트: 하위4비트(bit0 저전압·bit1 주파수제한·bit2 스로틀·**bit3 연온도제한**)=**현재 상태**,
상위4비트(bit16~19)=**부팅 후 이력**. "이번 측정이 스로틀됐나"는 **하위4비트(현재)** 로 판정.

---

## 전형적 흐름

```bash
# 측정 (터미널 A: 앱 / 터미널 B: 샘플러)
./build/TimeGrapher
tools/resource_sample.sh -o resource_ext.csv

# 분석 (측정 종료 후)
tools/verify_measurement.sh
tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv --correlate disp_paint_ms -o corr.csv
```
