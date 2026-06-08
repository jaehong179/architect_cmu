# Raspberry Pi 성능 측정 체크리스트

> **목적**: PC에서 사전 검증한 계측을 **Pi에서 최종 QA 판정**으로 돌리기 위한 단계별 절차.
> 관련 문서: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md)(합격기준) · [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md)(로그 해독) · [PERF_CODE_MAP.md](PERF_CODE_MAP.md)(코드 위치).
>
> 역할 구분: **PC = 사전 테스트**(계측 동작·알고리즘 정확도 검증, 완료) / **Pi = 최종 성능 판정**(지연·CPU·메모리·스로틀의 실제 수치).

---

## 0. 사전 준비 (최초 1회)

- [ ] 소스 동기화 (PC에서 작업한 `TimeGrapher/` 전체를 Pi로)
- [ ] **빌드 확인** (PC에선 Windows 분기만 컴파일됨 → Pi에서 Linux 분기 첫 컴파일 확인):
  ```bash
  cd TimeGrapher && cmake -B build-pi -S . && cmake --build build-pi
  ```
  - Linux 분기 사용 헤더: `<unistd.h> <cstdio> <cstring> <cstdlib> <QProcess>` (PerfInstrumentation.cpp) — 표준 POSIX+Qt
  - CMake: `psapi`는 `IF(WIN32)` 로 묶여 Pi 빌드 무관, Pi는 `asound` 링크
- [ ] **AGC OFF 확인** (CON-OP-01, 필수 — 안 끄면 측정 왜곡):
  ```bash
  alsamixer   # F4(Capture) → Auto Gain Control 을 [MM](off) 로
  ```

---

## 1. 측정 원칙 (멘토 피드백 반영 — 깨끗한 측정)

- [ ] **측정 중 모달 대화상자·모드 전환 금지**: PC 측정에서 Start "녹음?" 대화상자·Live↔Sim 전환이 이벤트 루프를 멈춰 `ui_loop_lag` 4초·`e2e_full` 470ms 같은 **거대 이상치**를 만들었다. → 측정 구간엔 손대지 말 것.
- [ ] **한 번에 한 모드**만. Start 시 "녹음?" 은 **No**.
- [ ] 각 측정 **30초+**, 첫 회 워밍업 제외, **평균 + 중앙값 + p95/최악** 으로 본다.
- [ ] 결과는 `e2e_full_ms`(진짜 종단간) 로 판정. `e2e_latency_ms` 는 하한(참고).

---

## 2. 측정 시나리오

### 2-1. Live 모드 — 지연·드롭·자원·스로틀 (성능 본판정)
- [ ] Mode = **Live**, 샘플레이트 = **48k → 96k → 192k** 각각
- [ ] Start → "녹음?" No → **60초 방치** → Stop (각 레이트 3회)
- [ ] 채워지는 것: `e2e_full_ms`·`e2e_latency`·`cap2proc`·`proc2disp`·`disp_paint`·`backlog`(A) / `capture_gap`·`bg_*`·`fg_*`(B) / `cpu_percent`·`throttled_flag`·`rss_bytes`(C·D) / `ui_loop_lag`(A-3)

### 2-2. 30분 지속 — 메모리·열 안정성
- [ ] Live(또는 Sim) **30분 연속** 방치 → 종료
- [ ] 보는 것: `rss_bytes` 추세(누수 vs 포화), `throttled_flag`(서멀), `cpu_percent`

### 2-3. Sim 모드 — 정확도/검출률 (알고리즘, PC와 동일하나 재확인)
- [ ] Mode = **Sim**, BPH/Amplitude/Error/BeatError 설정 → Start(No) → **1000비트+** (≈2분)
- [ ] 채워지는 것: `onset_err`·`peak_err`(E) / `rate_err`·`beaterr_err`·`amp_err`·`a_match`·`gt_total`(G)

---

## 3. 로그 분석 (Pi 터미널)

`perf_log.csv` 는 실행 디렉터리에 생성. 핵심 명령:
```bash
# ★진짜 종단간 (QA-LT-01 판정) — 중앙값/p95
grep ",e2e_full_ms," perf_log.csv | awk -F, '{print $5}' | sort -n | \
  awk '{a[NR]=$1} END{print "n="NR, "median="a[int(NR/2)], "p95="a[int(NR*0.95)], "max="a[NR]}'

# 단계 분해 (어디가 병목?)
for m in cap2proc_latency_ms proc2disp_latency_ms disp_paint_ms; do \
  echo -n "$m: "; grep ",$m," perf_log.csv | awk -F, '{s+=$5;n++} END{print s/n" ms avg"}'; done

# 캡처 드롭
grep ",capture_gap_growth," perf_log.csv | awk -F, '{print $5}'   # 0 부근=정상, 지속 증가=드롭

# 메모리 추세 (MB)
grep ",rss_bytes," perf_log.csv | awk -F, '{print $1/1000" s "$5/1048576" MB"}'

# 스로틀 (Pi 전용 — 0 이외면 서멀/저전압 발생)
grep ",throttled_flag," perf_log.csv

# CPU
grep ",cpu_percent," perf_log.csv | awk -F, '{s+=$5;n++;if($5>m)m=$5} END{print "avg="s/n"% max="m"%"}'

# 검출률 (Sim)
echo "검출률 = $(grep -c ',a_match,' perf_log.csv) / $(grep ',gt_total,' perf_log.csv | tail -1 | awk -F, '{print $5}')"
```

---

## 4. 합격 기준 (이 값과 대조 → M1 `(TBD)` 확정)

| 항목 | metric | 목표 |
|------|--------|------|
| 종단간 지연 | `e2e_full_ms` | 평균 ≤50ms · 최악 ≤100ms |
| 드롭 | `capture_gap_growth` | 48k=0 · 96k=0 · 192k ≤1/60s |
| CPU | `cpu_percent` | 평균 ≤70% |
| 스로틀 | `throttled_flag` | 미발생(0) |
| 메모리 | `rss_bytes` | 30분 증가 ≤200MB · 누수 없음 |
| Onset/Peak | `onset_err`/`peak_err` | ≤0.5ms / ≤0.2ms |
| 측정 정확도 | `rate/beaterr/amp_err` | ±1 s/d · ±0.1ms · ±5° |
| 검출률 | `a_match`/`gt_total` | ≥95% · FP ≤2% |
| UI 응답 | `ui_loop_lag_ms` | ≤200ms |

> ⚠️ 멘토 주의: 측정값을 그대로 요구값으로 쓰지 말 것. 측정은 "목표가 달성 가능한가 / 어떤 설계가 필요한가" 판단 근거.

---

## 5. PC 사전 테스트에서 이미 확인된 것 (Pi에서 재확인 불필요 / 참고)
- ✅ 계측 파이프라인 동작·로그 형식·값 타당성(NaN/이상치 없음)
- ✅ 알고리즘 정확도(E·G) — PC=Pi 동일: onset 0.08ms·peak 0.03ms·rate ±0.3·amp +3.5°(편향)·검출 97%(긴 런)
- ✅ 종단간 분해(`e2e_full = e2e_latency + disp_paint`) — **페인트가 지연의 주범**임 확인 → Pi에선 더 커질 것
- ⚠️ Pi에서 새로 볼 것: **실제 지연 수치(특히 disp_paint↑↑)**, **스로틀(C-2, Pi 전용)**, **30분 메모리**
