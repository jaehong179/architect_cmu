# Raspberry Pi 성능 측정 체크리스트

> **목적**: PC에서 사전 검증한 계측을 **Pi에서 최종 QA 판정**으로 돌리기 위한 단계별 절차.
> 관련 문서: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md)(합격기준·외부 측정 런북) · [PERF_LOG_GUIDE.md](PERF_LOG_GUIDE.md)(로그 해독) · [PERF_CODE_MAP.md](PERF_CODE_MAP.md)(코드 위치).
>
> 역할 구분: **PC = 사전 테스트**(계측 동작·알고리즘 정확도 검증, 완료) / **Pi = 최종 성능 판정**(지연·CPU·메모리·스로틀의 실제 수치).

---

## ★ 측정 구조 (중요 — 무엇을 어디서 재나)

자원(CPU·메모리·스로틀)은 **앱 밖에서** 잰다. 프로세스 안에서 재면 측정 행위 자체가 CPU·메모리를 먹어
결과를 오염시키기 때문(관측자 효과). 앱 내부 계측은 **밖에서 못 보는 의미론적 지표만** 담당한다.

| 무엇 | 어디서 | 산출물 |
|------|--------|--------|
| 지연(A)·정확도(G)·FPS(F)·백로그(E)·UI 루프 지연(A-3) | **앱 내부** | `build/perf_log.csv` |
| 메모리(PSS·Private_Dirty·RSS) + 발열·스로틀(온도·클럭·throttled) | **외부** `tools/resource_sample.sh` (합본) | `resource_ext.csv` |
| CPU% | **외부** `htop`(빠른 확인) 또는 설치형 도구 | — |

→ 세 로그의 시간축은 `tools/perf_join.py` 로 자동 정렬한다(아래 3-4). 정렬 앵커는 `perf_log.csv`
헤더의 `epoch_ms_t0`(= 단조시계 t_ms=0 의 벽시계 epoch[ms]).

---

## 0. 사전 준비

- [ ] 소스 동기화 (PC에서 작업한 전체 트리를 Pi로)
- [ ] **빌드 확인**:
  ```bash
  cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
  ./build_run.sh . --no-run        # 또는: cmake --build build --parallel
  ```
  - 자원 측정 코드가 외부로 빠져, `PerfInstrumentation.cpp` 에는 OS별 `/proc`·WinAPI·`vcgencmd` 분기가 없다(빌드 단순).
- [ ] **AGC OFF 확인** (CON-OP-01, 필수 — 안 끄면 측정 왜곡):
  ```bash
  alsamixer   # F4(Capture) → Auto Gain Control 을 [MM](off) 로
  ```
- [ ] **발열 정리 (서멀 측정 정확도)** — 유휴에서도 76~79°C·스로틀 이력(0xe0000)이 관측됨:
  ```bash
  # (1) 냉각(방열판/팬) 물리 확인  (2) 재부팅으로 스로틀 이력 클리어
  sudo reboot
  # 재부팅 후 클린 상태 확인 ↓
  vcgencmd measure_temp        # 가급적 ~60°C 이하에서 시작
  vcgencmd get_throttled       # ★ throttled=0x0 이어야 함 (아니면 냉각 부족)
  ```

---

## 1. 측정 원칙 (멘토 피드백 반영 — 깨끗한 측정)

- [ ] **측정 중 모달 대화상자·모드 전환 금지**: Start "녹음?" 대화상자·Live↔Sim 전환이 이벤트 루프를 멈춰
  `ui_loop_lag` 4초·`e2e_full` 470ms 같은 **거대 이상치**를 만든다. → 측정 구간엔 손대지 말 것.
- [ ] **한 번에 한 모드**만. Start 시 "녹음?" 은 **No**.
- [ ] 각 측정 **30초+**, 첫 회 워밍업 제외, **평균 + 중앙값 + p95/최악** 으로 본다.
- [ ] 결과는 `e2e_full_ms`(진짜 종단간) 로 판정. `e2e_latency_ms` 는 하한(참고).
- [ ] **외부 샘플러(메모리·발열)는 앱 Start 전부터 켜서 Stop 후까지** 계속 돌린다(시간축 정렬 위해).

---

## 2. 측정 실행 (터미널 3개)

### 터미널 A — 앱
```bash
cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
./build/TimeGrapher              # 재빌드하며 띄우려면: ./build_run.sh .
```

### 터미널 B — 자원 샘플러 (메모리+발열 합본, 앱 뜬 직후 바로)
```bash
cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
tools/resource_sample.sh -o resource_ext.csv   # PSS+온도+클럭+스로틀 1초 주기, Ctrl+C 로 종료
```
> ★ 앱 뜨면 **바로** 켤 것 — 늦게 켜면 그 앞 구간 자원 데이터가 빈다(지난 측정은 ~40초 늦어 앞부분 누락).

### 앱에서 시나리오 수행 (터미널 A 창)

**2-1. Live 모드 — 지연·드롭·자원·스로틀 (성능 본판정)**
- [ ] Mode = **Live**, 샘플레이트 = **48k → 96k → 192k** 각각
- [ ] Start → "녹음?" No → **60초 방치** → Stop (각 레이트 3회)
- [ ] 내부 CSV에 채워짐: `e2e_full_ms`·`e2e_latency`·`cap2proc`·`proc2disp`·`disp_paint`·`backlog`(A·E) / `capture_gap`·`bg_*`·`fg_*`(B) / `ui_loop_lag`(A-3) / `paint_fps`(F)
- [ ] 외부 CSV(B·C)에 동시 기록: PSS·온도·스로틀

**2-2. 30분 지속 — 메모리·열 안정성**
- [ ] Live(또는 Sim) **30분 연속** 방치 + 탭 12개 전환 반복 → 종료
- [ ] 보는 것: `resource_ext.csv` PSS 추세(누수 vs 포화) · 온도/클럭/스로틀

**2-3. Sim 모드 — 정확도/검출률 (알고리즘, PC와 동일하나 재확인)**
- [ ] Mode = **Sim**, BPH/Amplitude/Error/BeatError 설정 → Start(No) → **1000비트+** (≈2분)
- [ ] 내부 CSV에 채워짐: `onset_err`·`peak_err`(E) / `rate_err`·`beaterr_err`·`amp_err`·`a_match`·`gt_total`(G)

### 종료
- [ ] 앱 **정상 종료**(perf_log.csv flush·close 됨) → 터미널 B에서 **Ctrl+C**

---

## 3. 로그 분석

> 📌 **권장: 툴 2개면 끝난다.** 상세 해석법은 [PERF_ANALYSIS_TOOLS.md](PERF_ANALYSIS_TOOLS.md).
> ```bash
> tools/verify_measurement.sh                                            # ① 기록 정합성 점검
> tools/analyze_perf.py build/perf_log.csv --resource resource_ext.csv   # ② 통계+QA판정+발열영향 (워밍업 자동 제외)
> ```
> 아래 3-1~3-4는 수동/세부 확인용(툴 없이 grep·awk로 직접 볼 때).

### 3-1. 내부 CSV 점검 (먼저)
```bash
cd /home/lg/Desktop/TimeGrapher_Perf/architect_cmu
# 자원 지표가 빠졌는지(외부로 이전) + 정렬 앵커가 있는지 확인
grep -E "cpu_percent|rss_bytes|throttled_flag" build/perf_log.csv   # → 아무것도 안 나와야 정상
grep "epoch_ms_t0" build/perf_log.csv                              # → 앵커 한 줄 보여야 정상
```

### 3-2. 내부 지표 (perf_log.csv)
```bash
# ★진짜 종단간 (QA-LT-01 판정) — 중앙값/p95
grep ",e2e_full_ms," build/perf_log.csv | awk -F, '{print $5}' | sort -n | \
  awk '{a[NR]=$1} END{print "n="NR, "median="a[int(NR/2)], "p95="a[int(NR*0.95)], "max="a[NR]}'

# 단계 분해 (어디가 병목?)
for m in cap2proc_latency_ms proc2disp_latency_ms disp_paint_ms; do \
  echo -n "$m: "; grep ",$m," build/perf_log.csv | awk -F, '{s+=$5;n++} END{print s/n" ms avg"}'; done

# 캡처 드롭 (0 부근=정상, 지속 증가=드롭)
grep ",capture_gap_growth," build/perf_log.csv | awk -F, '{print $5}'

# UI 응답성
grep ",ui_loop_lag_ms," build/perf_log.csv | awk -F, '{print $5}' | sort -n | \
  awk '{a[NR]=$1} END{print "median="a[int(NR/2)], "p95="a[int(NR*0.95)], "max="a[NR]}'

# 검출률 (Sim)
echo "검출률 = $(grep -c ',a_match,' build/perf_log.csv) / $(grep ',gt_total,' build/perf_log.csv | tail -1 | awk -F, '{print $5}')"
```

### 3-3. 외부 지표 (resource_ext.csv — 합본)
> 합본 CSV 컬럼: `epoch_s,pss_kb,pss_mb,private_dirty_kb,rss_kb,temp_c,arm_mhz,throttled_hex,now_throttling,ever_throttled`
> ★ "이번 측정이 스로틀됐나"는 `ever_throttled`(부팅 후 이력, 측정 전 발생 포함) 가 아니라
>   **측정 중 실제 스로틀 = `throttled_hex` 하위4비트(또는 `now_throttling`)** 로 판정해야 정확하다.
```bash
# 메모리 누수: 시작 대비 마지막 PSS 증가량(MB)  ($3=pss_mb)
awk -F, 'NR==2{s=$3} NR>1{e=$3} END{printf "PSS 시작=%.1fMB 끝=%.1fMB 증가=%.1fMB\n", s, e, e-s}' resource_ext.csv

# 발열: 최고온도·최저클럭 + '측정 중' 실제 스로틀 비율  ($6=temp_c $7=arm_mhz $9=now_throttling)
python3 - resource_ext.csv <<'PY'
import csv,sys
n=now=0; t=[]; c=[]
for r in csv.DictReader(open(sys.argv[1])):
    n+=1; t.append(float(r['temp_c'])); c.append(int(r['arm_mhz']))
    if int(r['throttled_hex'],16)&0xF: now+=1
print(f"최고온도={max(t):.1f}°C  최저클럭={min(c)}MHz  측정중스로틀={now}/{n} ({now/n*100:.0f}%)")
PY
```
> 주의: 분석용 `awk` 비트연산(`and()`/`strtonum()`)은 mawk(기본)에 없어 위 스로틀 집계는 python 으로 처리한다.

### 3-4. 시간축 정렬 (내부 ↔ 외부)
```bash
# 통합 롱포맷(epoch_ms,t_ms,source,metric,value)
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv -o joined.csv

# 상관: 지연 스파이크가 발열/스로틀과 겹쳤나
tools/perf_join.py build/perf_log.csv --resource resource_ext.csv \
    --correlate e2e_full_ms --tolerance 1500 -o corr.csv
```

---

## 4. 합격 기준 (이 값과 대조 → M1 `(TBD)` 확정)

| 항목 | 출처 / metric | 목표 |
|------|---------------|------|
| 종단간 지연 | `perf_log.csv` `e2e_full_ms` | 평균 ≤50ms · 최악 ≤100ms |
| 드롭 | `perf_log.csv` `capture_gap_growth` | 48k=0 · 96k=0 · 192k ≤1/60s |
| UI 응답 | `perf_log.csv` `ui_loop_lag_ms` | ≤200ms |
| Onset/Peak | `perf_log.csv` `onset_err`/`peak_err` | ≤0.5ms / ≤0.2ms |
| 측정 정확도 | `perf_log.csv` `rate/beaterr/amp_err` | ±1 s/d · ±0.1ms · ±5° |
| 검출률 | `perf_log.csv` `a_match`/`gt_total` | ≥95% · FP ≤2% |
| **메모리** | **`resource_ext.csv` PSS** | 30분 증가 ≤200MB · 누수 없음 |
| **스로틀** | **`resource_ext.csv` 측정중 스로틀**(throttled_hex 하위4비트/now_throttling) | 미발생(0%) |
| **CPU** | **외부(htop/pidstat)** | 평균 ≤70% |

> ⚠️ 멘토 주의: 측정값을 그대로 요구값으로 쓰지 말 것. 측정은 "목표가 달성 가능한가 / 어떤 설계가 필요한가" 판단 근거.

---

## 5. PC 사전 테스트에서 이미 확인된 것 (Pi에서 재확인 불필요 / 참고)
- ✅ 계측 파이프라인 동작·로그 형식·값 타당성(NaN/이상치 없음)
- ✅ 알고리즘 정확도(E·G) — PC=Pi 동일: onset 0.08ms·peak 0.03ms·rate ±0.3·amp +3.5°(편향)·검출 97%(긴 런)
- ✅ 종단간 분해(`e2e_full = e2e_latency + disp_paint`) — **페인트가 지연의 주범**임 확인 → Pi에선 더 커질 것
- ⚠️ Pi에서 새로 볼 것: **실제 지연 수치(특히 disp_paint↑↑)**, **발열·스로틀(외부, Pi 전용)**, **30분 메모리(PSS)**
