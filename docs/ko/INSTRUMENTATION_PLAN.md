# 계측 계획·결과 (INSTRUMENTATION_PLAN)

> 짝 문서: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md) — "무엇을·왜·합격기준"
> 이 문서: 그 항목들을 **현재 코드의 어디에 어떻게 계측했는지** + 측정/분석 방법.
> 계측은 모두 **측정 전용**이며 제품 기능을 바꾸지 않는다. Windows / Raspberry Pi 모두 동작.

---

## 0. 로그 구조 (공통)

- 모든 측정값은 `Perf::log(section, qa, metric, value, unit, extra)` 한 줄로 기록된다.
- 출력: **`perf_log.csv`**(실행 작업 디렉터리, 매 실행 새로 씀) + 콘솔(qDebug) 동시.
- CSV 컬럼: `t_ms , section , qa , metric , value , unit , extra`
  - `t_ms` = 프로그램 시작 이후 단조시계(ms). 지연 계산의 공통 기준.
  - `section`(예 `A-1`) / `qa`(예 `QA-LT-01`) 로 **가이드·M1 문서와 1:1 연결**.
- 계측 모듈: [PerfInstrumentation.h](../../PerfInstrumentation.h) / [.cpp](../../PerfInstrumentation.cpp)
  (헤더 상단에 전체 태그↔문서 매핑표 있음). CPU·메모리·스로틀만 OS별 `#if` 분기.
- **로그 그룹별 ON/OFF**: 헤더의 `PERF_GRP_*` / `PERF_MASTER_ENABLE` 매크로를 `1`/`0`으로 바꿔
  리빌드하면 그룹 단위로 기록을 켜고 끌 수 있다(끄면 flush 오버헤드도 제거 → 관측자 효과↓).
  사용법·그룹↔metric 표는 [PERF_MEASUREMENT_OVERVIEW.md](PERF_MEASUREMENT_OVERVIEW.md) §3.5 참조.

### 추출 예시
```powershell
# 종단간 지연만 추출 → 평균/p95 계산
Import-Csv perf_log.csv | Where-Object section -eq 'A-1' | Measure-Object value -Average -Maximum
```
```bash
grep ",A-1," perf_log.csv | awk -F, '{print $5}' | sort -n   # Pi/Linux
```

---

## 1. 항목별 계측 현황 (가이드 §A~§I)

범례: ✅코드계측 / 🟡외부도구·수동절차 / ⬜미적용(현 시스템에 대상 없음)

| 가이드 | metric (CSV) | 상태 | 계측 위치(파일·함수) | 합격 기준 |
|--------|--------------|------|----------------------|-----------|
| **A-1** 종단간 하한 | `e2e_latency_ms` | ✅ | MainWindow.cpp `ProcessSamples`(replot 요청) ← 캡처시각 AudioWorker.cpp `ProcessAudioInput` | (하한·참고) |
| **A-1** ★진짜 종단간 | `e2e_full_ms` | ✅ | MainWindow.cpp `OnScopeReplotted`(afterReplot) | 평균 ≤50ms·최악 ≤100ms |
| **A-2** 캡처→처리 | `cap2proc_latency_ms` | ✅ | MainWindow.cpp `ProcessSamples`(처리 시작) | 단계 병목 식별 |
| **A-2** 처리→요청 | `proc2disp_latency_ms` | ✅ | MainWindow.cpp `ProcessSamples`(replot 요청) | 단계 병목 식별 |
| **A-2** 요청→실제페인트 | `disp_paint_ms` | ✅ | MainWindow.cpp `OnScopeReplotted`(afterReplot) | 단계 병목 식별 |
| **A-2** 백로그 | `backlog_samples` | ✅ | MainWindow.cpp `ProcessSamples`(진입) | 추세(증가=대기) |
| **A-3** UI 응답성 | `ui_loop_lag_ms` | ✅ | MainWindow.cpp `SamplePerfUiResponsiveness`(100ms 타이머) | ≤200ms |
| **A-4** 결함 인지 | `fault_sync_lost`·`detector_reset` | ✅(반자동) | MainWindow.cpp `ProcessSamples`(tg_process 직후) | 주입→로그 ≤2초 |
| **B-1** 캡처 드롭(추정) | `capture_gap_samples`·`capture_gap_growth` | ✅ | AudioWorker.cpp `ProcessAudioInput`(2초 블록) | 48k 0·96k 0·192k ≤1/60s |
| **B-1** 캡처 오류(장치 직접) | `audio_xrun`·`audio_state` | ✅ | AudioWorker.cpp `ProcessAudioInput`/`stateChangeAudioInput` (`QAudioSource::error()`) | xrun 미발생 |
| **B-2** 무중단 | (B-1 growth 로 관찰) | ✅ | 〃 | growth=0 |
| **B-4** 신호처리 단계별 | `dsp_hpf/env/detect/sync/total_ms` | ✅ | Timegrapher.cpp `tg_process`(단계별, 1초 집계) | DSP 병목 단계 식별 |
| **B-3** 실효 처리량 | `bg_sps/fps/spf`·`fg_sps/fps/spf` | ✅ | AudioWorker.cpp + MainWindow.cpp `ProcessSamples` | SPS≈설정sps |
| **C-1** CPU% | `cpu_percent` | ✅(앱내장) | MainWindow.cpp `SamplePerfResources`(1Hz) | 평균 ≤70% |
| **C-2** 스로틀(Pi) | `throttled_flag` | ✅(Pi전용) | 〃 → PerfInstrumentation `readThrottled`(vcgencmd) | 미발생 |
| **D-1/D-2** 메모리 | `rss_bytes` | ✅ | MainWindow.cpp `SamplePerfResources`(1Hz) | 30분 ↑≤200MB·누수없음 |
| **E-2** Onset/Peak 정밀도 | `onset_err_ms`·`peak_err_ms` | ✅(Sim) | MainWindow.cpp `ProcessSamples`(A/C 이벤트 대조) | Onset ≤0.5ms·Peak ≤0.2ms |
| **G-1** 측정 정확도 | `rate_err_s_per_d`·`beaterr_err_ms`·`amp_err_deg` | ✅(Sim) | MainWindow.cpp `DisplayResults` | ±1 s/d·±0.1ms·±5° |
| **G-2** 검출률 | `a_match`·`c_match`·`a_unmatched`·`gt_total` | ✅(Sim) | MainWindow.cpp `ProcessSamples`+`DisplayResults` | 검출률 ≥95%·FP ≤2% |
| **G-3** 잡음 강건성 | (G-1/G-2 를 잡음 ON/OFF 비교) | 🟡(절차) | 〃 + Sim `noise_peak_amplitude`/Live 60dB | 검출률 ≥80%·오차 ≤2배 |
| **E-1** 누적 타이밍 | — | 🟡(향후) | 단계별 타임스탬프 추가 필요(FR-SYS-4) | 누적 ≤1 sample |
| **F-1** 화면 갱신율(frame drop) | `paint_fps` (+`replot_req`) | ✅ | MainWindow.cpp `OnScopeReplotted`(1초 집계) | 부하 시 저하 ≤10% (탭 1→12는 탭 구현 후) |
| **F-2** 표시 일관성 | — | ⬜ | 단일 표시(탭 미구현) | 추후 |
| **H** TinyML | — | ⬜ | 미구현(탐구 과제) | — |
| **I** 배포/기동 | — | 🟡(체크리스트) | 운영 절차(AGC OFF 등) | ≤30분 |

---

## 2. 측정 실행 절차

### (1) 베이스라인 프로파일 — 가이드 §J 우선순위
1. **Live 모드 60초 ×3** (48k/96k/192k 각각, AGC OFF):
   → `A-1/A-2`(지연), `B-1/B-3`(드롭/처리량), `C/D`(CPU/메모리/스로틀) 자동 기록.
2. perf_log.csv 에서 `A-2` 로 **지연 단계 분해** 확인 → 청크 버퍼링·지연선 병목 규명.
3. 30분 연속 운용으로 `C-1`·`rss_bytes` 추세 → 헤드룸/누수/스로틀.

### (2) 정확도·정밀도(E·G) — PC에서 가능 (Pi 불필요)
1. **Sim 모드**로 BPH/Error/Amplitude/BeatError 설정 후 실행.
   → `G-1`(측정 vs 설정 오차), `E-2`(onset/peak 오차), `G-2`(검출률) 자동 기록.
2. 깨끗한 측정은 Realistic **체크 해제**(clean config)로, 강건성(G-3)은 Realistic/잡음 켜고 동일 로그 비교.

### (3) 분석 → 문서 갱신
- `(TBD)` 항목(QA-RT-02·RT-03·CO-01·LT-01·EXP-06)의 값을 측정 결과로 **확정/수정**.
- ⚠️ 멘토 주의: 측정값을 그대로 요구값으로 쓰지 말 것. 요구값은 평가기준에서, 측정은 타당성 판단 근거.

---

## 3. 산식 메모 (분석 시)
- **검출률(G-2)** = Σ`a_match` / `gt_total`(최종값). **FP** = Σ`a_unmatched`.
- **지연(A-1)** = ★**`e2e_full_ms`**(진짜 종단간, 페인트 포함)의 평균/중앙값/p95 로 판정. `e2e_latency_ms`는 하한(요청까지). 단계 합: `cap2proc + proc2disp + disp_paint = e2e_full`(per-event).
- **CPU%(C-1)** = 전 코어 정규화(0~100). `extra` 의 `cores=` 로 코어 수 확인.
- **드롭(B-1)** = `capture_gap_growth` 가 0 부근이면 정상, 지속 양수면 드롭.

---

## 4. 플랫폼 분기 요약
| 측정 | Windows | Raspberry Pi(Linux) |
|------|---------|---------------------|
| 시계/지연/처리량/드롭 | 공통(std::chrono, Qt) | 공통 |
| CPU% | GetProcessTimes | /proc/self/stat |
| RSS | GetProcessMemoryInfo(psapi) | /proc/self/statm |
| 스로틀 | N/A(항상 false) | vcgencmd get_throttled |

> 빌드 검증 완료: Windows(MinGW/Qt 6.11.1) 컴파일·링크 OK. Linux 분기는 `#if defined(Q_OS_LINUX)` 로 분리되어 Pi 빌드 대비 헤더(`<cstring>/<cstdlib>/<unistd.h>`) 포함.
