#ifndef PERFINSTRUMENTATION_H
#define PERFINSTRUMENTATION_H
// =============================================================================
//  PerfInstrumentation  —  성능 검증 계측 모듈  (★측정 전용, 제품 기능 아님★)
// -----------------------------------------------------------------------------
//  [무엇을 위한 파일인가]
//    docs/PERF_VERIFICATION_GUIDE.md 의 각 검증 항목(§A~§I) 값을 "현재 구현된
//    시스템"에서 정확히 측정·예측하기 위한 로깅 인프라이다. 요구사항을 새로
//    구현하는 것이 아니라, 기존 동작에 계측(instrumentation)만 덧붙인다.
//
//  [로그 사용법]
//    모든 측정값은 아래 한 줄로 기록된다:
//        Perf::log(section, qa, metric, value, unit, extra)
//    → CSV 파일(perf_log.csv) + 콘솔(qDebug) 에 동시에 남는다.
//    CSV 컬럼:  t_ms , section , qa , metric , value , unit , extra
//    'section'(예 "A-1") 과 'qa'(예 "QA-LT-01") 로 PERF_VERIFICATION_GUIDE.md /
//    M1 문서를 바로 찾을 수 있다. (grep "A-1" perf_log.csv → 종단간 지연만 추출)
//
//  [플랫폼]  Windows / Raspberry Pi(Linux) 모두 지원.
//            CPU·메모리·스로틀 측정만 OS별로 분기(#if)하고, 나머지는 공통.
//
//  ── 태그 ↔ 문서(PERF_VERIFICATION_GUIDE.md) ↔ M1 QA 매핑 ─────────────────────
//   section  qa         metric                 측정 의미 (가이드 항목)
//   A-1      QA-LT-01   e2e_latency_ms         종단간 하한(캡처→replot요청), Live §A-1
//   A-1      QA-LT-01   e2e_full_ms            ★진짜 종단간(캡처→실제 페인트), Live §A-1
//   A-2      QA-LT-01   cap2proc_latency_ms    캡처→처리 지연(최신샘플), Live    §A-2
//   A-2      QA-LT-01   proc2disp_latency_ms   처리→replot요청 지연             §A-2
//   A-2      QA-LT-01   disp_paint_ms          replot요청→실제 페인트(afterReplot) §A-2
//   A-2      QA-LT-01   backlog_samples        미처리 누적 샘플(백로그)         §A-2
//   B-1      QA-RT-02   capture_gap_samples    기대대비 부족 샘플(드롭 추정)    §B-1
//   B-1      QA-RT-02   capture_gap_growth     2초간 부족 증가분(드롭 신호)     §B-1/B-2
//   B-1      QA-RT-02   audio_xrun             장치 직접보고 캡처오류(xrun, 변화시) §B-1
//   B-1      QA-RT-02   audio_state            캡처 장치 상태 전이              §B-1
//   B-3      QA-RT-02   bg_sps/bg_fps/bg_spf   실효 처리량(백그라운드 캡처)     §B-3
//   B-3      QA-RT-01   fg_sps/fg_fps/fg_spf   실효 처리량(전경 핸들러/렌더)    §B-3/A-3
//   B-4      QA-RT-01   dsp_hpf/env/detect/    ★신호처리(tg_process) 단계별     §B-4
//                       sync/total_ms          처리시간 (1초 평균, extra=max)
//   F-1      QA-SC-01   paint_fps              초당 실제 화면 갱신(frame drop)  §F-1
//   C-1      QA-EE-01   cpu_percent            프로세스 CPU%(전 코어 정규화)    §C-1
//   C-2      QA-EE-01   throttled_flag         Pi 서멀 스로틀 플래그(Win=N/A)   §C-2
//   D-1      QA-RT-03   rss_bytes              프로세스 RSS(메모리 사용량)      §D-1/D-2
//   A-3      QA-RT-01   ui_loop_lag_ms         UI 이벤트루프 지연(응답성)       §A-3
//   A-4      QA-US-01   fault_sync_lost/...    결함/관측 이벤트 발생 시각       §A-4
//   E-2      QA-AC-02   onset_err_ms/peak_err  검출 vs 정답 식별 오차(Sim)      §E-2
//   G-1      QA-CO-01   rate/beaterr/amp_err   측정값 - 설정값 오차(Sim)        §G-1
//   G-2      QA-AC-01   a_match/c_match/...    검출 성공/실패·정답 총수(Sim)    §G-2
//  ---------------------------------------------------------------------------
//  ※ E-2/G-1/G-2 는 Sim 모드 정답값(ground-truth) 대조로만 산출(PC에서 측정 가능).
//    G-3(잡음 강건성)은 잡음 ON/OFF 로 같은 로그를 비교하면 된다(추가 코드 불필요).
// =============================================================================
#include <QString>
#include <QtGlobal>

// =============================================================================
//  ★ 로그 ON/OFF 설정 (그룹별 컴파일 스위치) ★
// -----------------------------------------------------------------------------
//  각 그룹을 1=기록 / 0=끔 으로 바꾸고 "리빌드"하면 그 그룹 로그만 켜지거나 꺼진다.
//  끄면 perf_log.csv·콘솔에 그 그룹이 남지 않고, 그 줄의 문자열 포맷팅·디스크 flush
//  오버헤드까지 사라진다 → 관측자 효과↓. (부하/Pi 측정 시 불필요한 그룹을 꺼서
//  꼭 봐야 할 지표의 측정 정밀도를 높이는 용도. 끄는 것은 '기록'만이며 제품 동작/연산은
//  그대로다.)  ※ 컴파일타임 스위치라 값을 바꾸면 다시 빌드해야 적용된다.
//
//  그룹 ↔ 문서 §섹션 ↔ metric  (자세한 건 위 매핑표 / docs/PERF_LOG_GUIDE.md):
//    PERF_GRP_LATENCY     §A-1/A-2  e2e_full·e2e_latency·cap2proc·proc2disp·disp_paint·backlog
//    PERF_GRP_UI          §A-3      ui_loop_lag
//    PERF_GRP_FAULT       §A-4      fault_sync_lost·detector_reset
//    PERF_GRP_CAPTURE     §B-1      capture_gap_samples/growth·audio_xrun·audio_state
//    PERF_GRP_THROUGHPUT  §B-3      bg_sps/fps/spf·fg_sps/fps/spf
//    PERF_GRP_DSP         §B-4      dsp_hpf/env/detect/sync/total
//    PERF_GRP_RESOURCES   §C-1/C-2  cpu_percent·throttled_flag
//    PERF_GRP_MEMORY      §D-1      rss_bytes
//    PERF_GRP_PRECISION   §E-2      onset_err·peak_err
//    PERF_GRP_FRAME       §F-1      paint_fps
//    PERF_GRP_ACCURACY    §G-1/G-2  rate/amp/beat_err·a_match/c_match·gt_total
// -----------------------------------------------------------------------------
#define PERF_MASTER_ENABLE   1   // 0 = 전체 로그 OFF (아래 그룹 설정 전부 무시)

#define PERF_GRP_LATENCY     1   // §A-1/A-2  지연(종단간·단계분해·백로그)
#define PERF_GRP_UI          1   // §A-3      UI 응답성
#define PERF_GRP_FAULT       1   // §A-4      결함 인지
#define PERF_GRP_CAPTURE     1   // §B-1      캡처 드롭/오류/상태
#define PERF_GRP_THROUGHPUT  1   // §B-3      실효 처리량(bg/fg)
#define PERF_GRP_DSP         1   // §B-4      신호처리 단계별 시간
#define PERF_GRP_RESOURCES   1   // §C-1/C-2  CPU%·스로틀
#define PERF_GRP_MEMORY      1   // §D-1      메모리(RSS)
#define PERF_GRP_PRECISION   1   // §E-2      onset/peak 정밀도
#define PERF_GRP_FRAME       1   // §F-1      화면 갱신율(frame drop)
#define PERF_GRP_ACCURACY    1   // §G-1/G-2  측정 정확도·검출률
// =============================================================================

namespace Perf {

// CSV 로그 파일 열기/닫기 — Main 시작/종료 시 1회. (sessionTag 는 파일 주석에 기록)
void   init(const QString &sessionTag);
void   shutdown();

// 단조 증가 시계(ms). 크로스플랫폼(std::chrono::steady_clock). 모든 지연 계산의 기준.
double nowMs();

// 한 줄 측정 기록 (CSV). section/qa 로 문서와 연결된다.
//  관측자 효과↓: flush 는 1초 주기, 콘솔 echo 는 기본 OFF (아래 setConsoleEcho).
void   log(const char *section, const char *qa, const char *metric,
           double value, const char *unit, const QString &extra = QString());

// 콘솔 echo(qDebug) on/off — 기본 OFF. 측정 중엔 OFF(관측자 효과 제거), 디버깅 시에만 ON.
void   setConsoleEcho(bool on);

// CPU%·RSS(메모리)·스로틀 같은 '자원' 지표는 앱 내부에서 측정하지 않는다.
//  관측자 효과를 피하려고 외부 도구(psrecord/pidstat/vcgencmd)로 측정한다.
//  런북: docs/*/PERF_VERIFICATION_GUIDE.md

// 논리 코어 수 (세션 헤더 기록용).
int    cpuCoreCount();

} // namespace Perf

#endif // PERFINSTRUMENTATION_H
