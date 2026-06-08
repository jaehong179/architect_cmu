# perf_log.csv 해독 가이드 — 컬럼·섹션·연결 사전

> `perf_log.csv` 한 줄 한 줄이 **무엇을 증명하려는 측정인지**, 그리고 **어느 문서와 연결**되는지 정리한 사전.
> 짝 문서: [PERF_VERIFICATION_GUIDE.md](PERF_VERIFICATION_GUIDE.md)(무엇을·왜·합격기준) · [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md)(코드 어디에 계측).

---

## 1. 컬럼 구조

CSV 한 행 = 측정 1건. 컬럼 7개:

| 컬럼 | 의미 | 예시 | 쓰임 |
|------|------|------|------|
| **t_ms** | 프로그램 시작 후 경과(단조시계, ms) | `7394.269` | 시간차 계산 기준. 같은 비트의 캡처시각·표시시각 차이 등 |
| **section** | **가이드 대분류 코드(A~I)** = "어떤 품질 측면인가" | `A-1` | PERF_VERIFICATION_GUIDE.md §A-1 로 바로 연결 |
| **qa** | **M1 품질속성 시나리오 ID** = "어떤 요구를 검증하나" | `QA-LT-01` | M1 QA Taxonomy 문서로 연결 |
| **metric** | 구체 측정 지표 이름 | `e2e_latency_ms` | 무엇을 쟀는지 |
| **value** | 측정값(숫자) | `42.31` | 실제 수치 |
| **unit** | 단위 | `ms` | 해석 단위 |
| **extra** | 부가정보(비교값·조건) | `meas=303.8;set=300.0` | 측정값↔설정값, 샘플레이트, 코어수 등 |

> 첫 2줄은 `#` 주석(세션 정보: 시작시각·OS·코어수), 3번째 줄이 헤더.
> `section`+`qa` 두 컬럼이 **로그 ↔ 가이드 ↔ M1 요구사항**을 잇는 열쇠다.

---

## 2. 섹션(A~I) = 품질 측면 (PERF_VERIFICATION_GUIDE 대분류)

| 코드 | 대분류 | 무엇을 증명하려는가 | 주 연결 QA |
|------|--------|---------------------|-----------|
| **A** | 응답속도/지연 (Latency) | "Live에서 실시간 관찰 가능"하다는 주장 | QA-LT-01, QA-RT-01, QA-US-01 |
| **B** | 처리량/캡처드롭 (Throughput) | 고샘플레이트에서 데이터를 흘리지 않는가 | QA-RT-02, QA-RT-01 |
| **C** | CPU/발열 | 기능 더 얹을 자원 여유가 있는가 | QA-EE-01 |
| **D** | 메모리 | OOM·누수 없이 지속 가용한가 | QA-RT-03, QA-SC-01 |
| **E** | 타이밍 정밀도 | 작은 시간차를 정밀히 잡는가 | QA-AC-02, QA-AC-03 |
| **F** | 렌더/스케일 | 탭↑ 시 반응성이 무너지지 않는가 | QA-SC-01, QA-CO-02 |
| **G** | 검출/측정 정확도 | 음향에서 정확한 값을 내는가 | QA-CO-01, QA-AC-01, QA-CO-04 |
| **H** | 온디바이스 AI | Pi에서 실시간 추론 가능한가 | QA-SE-01, QA-AC-04 |
| **I** | 배포/기동 | 데모를 재현 가능하게 띄우는가 | QA-DY-01 |

---

## 3. metric 사전 — 무엇을 증명·어디에 연결·합격 기준

> "모드" = 그 metric이 기록되는 조건. **Live**=마이크 실시간, **Sim**=합성(정답값 보유, PC가능), **항상**=모드 무관.

### A. 지연 / 응답성
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `e2e_latency_ms` | 종단간 **하한**: 캡처→replot *요청* 까지 | A-1·QA-LT-01 | ms | (하한, 참고) | **Live** |
| `e2e_full_ms` | ★**진짜 종단간**: 캡처→**실제 픽셀 그려짐** (요청+페인트 합) | A-1·QA-LT-01 | ms | 평균 ≤50, 최악 ≤100 | **Live** |
| `cap2proc_latency_ms` | 구간①: 캡처→처리 시작 | A-2·QA-LT-01 | ms | 단계 병목 식별 | **Live** |
| `proc2disp_latency_ms` | 구간②: 처리→replot *요청* | A-2·QA-LT-01 | ms | 단계 병목 식별 | 항상 |
| `disp_paint_ms` | 구간②-b: replot 요청→**실제 페인트 완료**(afterReplot) | A-2·QA-LT-01 | ms | 단계 병목 식별 | 항상 |
| `backlog_samples` | 아직 처리 못한 누적 샘플(대기량) | A-2·QA-LT-01 | samp | 증가=대기 발생 | 항상 |
| `ui_loop_lag_ms` | UI 이벤트루프 지연(버튼 반응성) | A-3·QA-RT-01 | ms | ≤200 | 항상 |
| `fault_sync_lost` / `detector_reset` / `sync_acquired` | 결함·동기 이벤트 **발생 시각** | A-4·QA-US-01 | event | 주입→로그 ≤2초 | 항상 |

> ✅ **종단간을 두 구간으로 나눠 둘 다 측정한다** (replot이 `rpQueuedReplot`=지연 렌더이므로):
> - `e2e_latency_ms` = **하한** (캡처 → replot *요청* 까지; 메인스레드 처리 비용)
> - `disp_paint_ms` = 미뤄졌던 **실제 페인트 시간** (요청 → afterReplot 완료)
> - **`e2e_full_ms` = 진짜 종단간** = 캡처 → 실제 픽셀 = (위 둘의 합). **QA-LT-01 판정은 `e2e_full_ms` 로** 한다.
> ※ `afterReplot` 은 ScopePlot 의 실제 그리기 완료 신호. (SoundImage 페인트는 별개/병렬이라 미포함)

### B. 처리량 / 드롭
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `capture_gap_samples` | 기대 대비 부족 샘플(드롭 **추정**) | B-1·QA-RT-02 | samp | — | **Live** |
| `capture_gap_growth` | 2초간 부족 증가분(지속 증가=드롭) | B-1·QA-RT-02 | samp/2s | ≈0 | **Live** |
| `audio_xrun` | 장치가 **직접 보고**한 캡처 오류(xrun/overrun) — 변화 시만 기록 | B-1·QA-RT-02 | errcode | 미발생 | **Live** |
| `audio_state` | 캡처 장치 상태 전이(예: 예기치 않은 Idle) | B-1·QA-RT-02 | state | — | **Live** |

> 🎯 **오디오 드롭은 두 방식으로 본다**: ① `capture_gap`=시간-부족 **추정**(항상 동작), ② `audio_xrun`=`QAudioSource::error()`가 보고한 **실제 장치 오류**(Pi의 ALSA xrun도 Qt가 이 값으로 전달). `audio_xrun` errcode: 1=Open·2=IO·3=Underrun·4=Fatal. (Qt가 ALSA 핸들을 소유해 `snd_pcm_status` 직접 호출은 불가 → Qt error()로 대체)
| `bg_sps`/`bg_fps`/`bg_spf` | 백그라운드(캡처) 실효 처리량 | B-3·QA-RT-02 | samp/s 등 | SPS≈설정sps | **Live** |
| `fg_sps`/`fg_fps`/`fg_spf` | 전경(핸들러+렌더) 실효 처리량 | B-3·QA-RT-01 | samp/s 등 | 안정 | 항상 |
| `dsp_hpf_ms` | ★신호처리: HPF(고역통과) 단계 시간 | B-4·QA-RT-01 | ms | 병목 식별 | 항상 |
| `dsp_env_ms` | ★신호처리: 엔벨로프+지연선 단계 | B-4·QA-RT-01 | ms | 병목 식별 | 항상 |
| `dsp_detect_ms` | ★신호처리: 검출(Detector) 단계 | B-4·QA-RT-01 | ms | 병목 식별 | 항상 |
| `dsp_sync_ms` | ★신호처리: 동기(BPH·sync·이벤트출력) 단계 | B-4·QA-RT-01 | ms | 병목 식별 | 항상 |
| `dsp_total_ms` | ★신호처리 전체(=위 4단계 합) | B-4·QA-RT-01 | ms | proc2disp 중 DSP 비중 | 항상 |

> 📐 `dsp_*` 는 `tg_process` 안에서 단계별로 측정해 **1초마다 평균**(value)+**최대**(extra `max=`)로 기록.
> → "신호처리 지연의 주범이 어느 단계(HPF/엔벨로프/검출/동기)인가"를 Pi에서 분리 확인. `dsp_total`은 `proc2disp` 중 순수 DSP 몫.

### C. CPU / 발열
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `cpu_percent` | 프로세스 CPU%(전 코어 정규화) | C-1·QA-EE-01 | % | 평균 ≤70 | 항상 |
| `throttled_flag` | Pi 서멀 스로틀 발생 비트마스크 | C-2·QA-EE-01 | bitmask | 미발생(0) | **Pi 전용** |

### D. 메모리
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `rss_bytes` | 프로세스 상주 메모리(누수·증가) | D-1·QA-RT-03 | bytes | 30분 ↑≤200MB·누수없음 | 항상 |

### E. 타이밍 정밀도 (Sim 정답 대조)
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `onset_err_ms` | 검출 A(onset) vs 정답 A 오차 | E-2·QA-AC-02 | ms | ≤0.5 | **Sim** |
| `peak_err_ms` | 검출 C vs 정답 C 오차 | E-2·QA-AC-02 | ms | ≤0.2 | **Sim** |

### G. 검출 / 측정 정확도 (Sim 정답 대조)
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `rate_err_s_per_d` | 측정 rate − 설정 rate | G-1·QA-CO-01 | s/d | ±1 | **Sim** |
| `beaterr_err_ms` | 측정 beat error − 설정 | G-1·QA-CO-01 | ms | ±0.1 | **Sim** |
| `amp_err_deg` | 측정 amplitude − 설정 | G-1·QA-CO-01 | deg | ±5 | **Sim** |
| `a_match`/`c_match` | A/C 검출 성공(정답과 매칭) | G-2·QA-AC-01 | event | — | **Sim** |
| `a_unmatched`/`c_unmatched` | 검출 실패/오검출(FP) | G-2·QA-AC-01 | event | FP ≤2% | **Sim** |
| `gt_total` | 정답 비트 누적 수(검출률 분모) | G-2·QA-AC-01 | beats | — | **Sim** |

> **F(렌더/스케일)·H(AI)·I(배포)** 는 현재 미계측(탭/AI 미구현, 배포는 절차). [INSTRUMENTATION_PLAN.md](INSTRUMENTATION_PLAN.md) §1 참조.

---

### F. 렌더 / 프레임 (frame drop)
| metric | 무엇을 증명 | section·qa | 단위 | 합격 기준 | 모드 |
|--------|-------------|-----------|------|-----------|------|
| `paint_fps` | 초당 **실제 화면 갱신 횟수**(afterReplot). 부하 시 떨어지면 frame drop | F-1·QA-SC-01 | frame/s | 탭↑ 시 저하 ≤10% | 항상 |

> 🎞 `extra`의 `replot_req` = 요청 수. **요청 > paint 는 정상**(rpQueuedReplot coalescing). 진짜 frame drop = 부하로 `paint_fps`가 뚝 떨어질 때. (자세히 → [PERF_MEASUREMENT_OVERVIEW §1.5](PERF_MEASUREMENT_OVERVIEW.md))

## 4. 산식 (집계 시)
- **검출률(G-2)** = Σ`a_match` ÷ `gt_total`(최종값). **FP** = Σ`a_unmatched`.
- **종단간 지연(A-1)** = `e2e_latency_ms` 의 평균·p95·최악. 단계 합 ≈ `cap2proc` + `proc2disp`.
- **정밀도(E-2)** = `onset_err_ms`·`peak_err_ms` 의 **절대값** 평균/최악.
- **정확도(G-1)** = `*_err_*` 의 부호 포함 평균(=편향) + 절대값(=오차폭).
- **드롭(B-1)** = `capture_gap_growth` 가 지속 양수면 드롭.
- **메모리(D-1)** = `rss_bytes` 의 시작→끝 증가량, 우상향 추세 = 누수 의심.

## 5. 모드별로 무엇이 채워지나
| 모드 | 채워지는 섹션 | 비는 섹션(정상) | 용도 |
|------|---------------|-----------------|------|
| **Sim** | A-2(`backlog`·`proc2disp`)·A-3·A-4·B-3(`fg_*`)·C·D·**E·G** | `cap2proc`·`e2e`(A)·B-1·`bg_*` (모두 Live 전용) | 정확도/정밀도 검증(PC 가능) |
| **Live** | A-1·A-2(전체)·A-3·A-4·B(전체)·C·D | E·G(정답값 없음) | 실측 지연/드롭(마이크 필요) |

> ⚠️ 현재 PC 측정값(CPU·지연)은 **참고용**. QA 목표는 **라즈베리 파이 기준**이므로 성능(A·B·C·D)은 Pi에서 재측정해야 한다.

## 6. 빠른 추출 예
```powershell
# 종단간 지연 평균/최대 (Live 측정 후)
Import-Csv perf_log.csv | ? section -eq 'A-1' | Measure-Object value -Average -Maximum
# 검출률
$r=Import-Csv perf_log.csv; ($r|? metric -eq 'a_match').Count / [double]($r|? metric -eq 'gt_total')[-1].value
```
