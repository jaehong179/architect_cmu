// =============================================================================
//  PerfInstrumentation.cpp — 성능 검증 계측 구현 (측정 전용)
//  문서: docs/PERF_VERIFICATION_GUIDE.md  (각 측정의 의미는 헤더의 매핑표 참조)
//  Windows / Raspberry Pi(Linux) 양쪽 동작. OS 의존부는 #if 로 분기.
// =============================================================================
#include "PerfInstrumentation.h"

#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QThread>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>   // applicationDirPath() — 실행파일 옆에 CSV 고정
#include <QDir>
#include <chrono>
//  자원(CPU/메모리/온도)은 외부 스크립트(tools/perf_resources.sh)가 측정 → 이 파일엔 OS별 /proc·WinAPI include 불필요.

namespace {
// --- 로그 파일/동기화 (워커 스레드 + 메인 스레드 + 1Hz 타이머가 함께 기록) ---
QMutex             gLogMutex;
QFile              gLogFile;
QTextStream        gLogStream;
bool               gReady = false;
std::chrono::steady_clock::time_point gStart;   // nowMs() 의 기준점
qint64             gEpochMsAtT0 = 0;            // t_ms=0(=gStart) 순간의 벽시계 epoch(ms). 외부 로그와 시간 정렬용.

// --- 관측자 효과 최소화 ---
//  ① 콘솔 echo(qDebug)는 기본 OFF: 측정 중 매 줄 추가 QString 포맷·stderr I/O 제거. (디버깅 시 setConsoleEcho(true))
//  ② flush 는 매 줄이 아니라 주기적으로: 핫패스(초당 수십 회)에서 write 시스템콜 제거. 크래시 손실은 최대 kFlushIntervalMs.
bool               gConsoleEcho = false;
double             gLastFlushMs = 0.0;
constexpr double   kFlushIntervalMs = 1000.0;   // 1초마다 flush

// --- 로그 그룹 ON/OFF 게이트 (스위치는 PerfInstrumentation.h 의 PERF_GRP_* 매크로) ---
//  section 태그("A-1","B-4"…)를 그룹으로 매핑해, 그 그룹이 0이면 기록을 건너뛴다.
//  컴파일타임 상수(매크로)만 반환하므로 끈 그룹은 사실상 분기 비용도 없다.
inline bool sectionEnabled(const char *s)
{
#if !PERF_ENABLE
    (void)s; return false;                       // 마스터 OFF → 전체 차단(매크로가 호출 자체를 제거하지만 방어적)
#else
    if (!s || !s[0] || !s[1] || !s[2]) return true;   // 형식 밖이면 안전하게 허용
    const char grp = s[0];      // 'A'..'G'
    const char num = s[2];      // "A-1" → '1'
    switch (grp) {
        case 'A':
            if (num == '1' || num == '2') return PERF_GRP_LATENCY;   // A-1/A-2
            if (num == '3')               return PERF_GRP_UI;        // A-3
            if (num == '4')               return PERF_GRP_FAULT;     // A-4
            return true;
        case 'B':
            if (num == '1')               return PERF_GRP_CAPTURE;   // B-1
            if (num == '3')               return PERF_GRP_THROUGHPUT;// B-3
            if (num == '4')               return PERF_GRP_DSP;       // B-4
            return true;
        // 'C'(자원)·'D'(메모리)는 인앱 미측정(외부 스크립트) → 그룹 게이트 없음(기본 허용)
        case 'E': return PERF_GRP_PRECISION;                          // E-2
        case 'F': return PERF_GRP_FRAME;                             // F-1
        case 'G': return PERF_GRP_ACCURACY;                          // G-1/G-2
        default:  return true;                                        // 미분류는 허용
    }
#endif
}
}

namespace Perf {

double nowMs()
{
    // 단조 시계(절대 시각 아님). 캡처→처리→표시 지연(§A-1/A-2) 계산의 공통 기준.
    auto d = std::chrono::steady_clock::now() - gStart;
    return std::chrono::duration<double, std::milli>(d).count();
}

void init(const QString &sessionTag)
{
    QMutexLocker lock(&gLogMutex);
    gStart = std::chrono::steady_clock::now();
    // gStart(단조시계 0점)와 같은 순간의 벽시계 epoch(ms)를 즉시 포착 → 외부 epoch 로그와 정렬 기준.
    //  (두 호출 사이 간격은 µs 수준이라 초 단위로 움직이는 외부 자원 로그 정렬엔 무시 가능)
    gEpochMsAtT0 = QDateTime::currentMSecsSinceEpoch();
    gLastFlushMs = 0.0;   // 주기적 flush 기준점 리셋
    // 실행파일이 있는 디렉터리에 CSV 생성(매 실행 새로 씀). 실행한 작업 디렉터리(cwd)와
    // 무관하게 항상 같은 위치(바이너리 옆)에 남으므로, Qt Creator·셸·직접 실행 등
    // 어떤 경로로 띄워도 perf_log.csv 위치가 일정하다. 분석 시 grep/스프레드시트로 사용.
    const QString csvPath = QDir(QCoreApplication::applicationDirPath()).filePath("perf_log.csv");
    gLogFile.setFileName(csvPath);
    if (!gLogFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[PERF] CSV 열기 실패 — 콘솔(qDebug)로만 기록:" << csvPath;
        gReady = false;
        return;
    }
    gLogStream.setDevice(&gLogFile);
    // 파일 헤더: 컬럼 정의 + 문서 연결 안내 (CSV 첫 줄들은 '#' 주석)
    gLogStream << "# PERF measurement log — see docs/PERF_VERIFICATION_GUIDE.md\n";
    gLogStream << "# session=" << sessionTag
               << " start=" << QDateTime::currentDateTime().toString(Qt::ISODate)
               << " epoch_ms_t0=" << gEpochMsAtT0   // ★ t_ms=0 의 벽시계 epoch(ms): event_epoch_ms = epoch_ms_t0 + t_ms
#if defined(Q_OS_WIN)
               << " os=Windows"
#elif defined(Q_OS_LINUX)
               << " os=Linux/Pi"
#else
               << " os=other"
#endif
               << " cores=" << cpuCoreCount() << "\n";
    gLogStream << "t_ms,section,qa,metric,value,unit,extra\n";
    gLogStream.flush();
    gReady = true;
    qInfo() << "[PERF] 계측 로그 시작 →" << csvPath;
}

void shutdown()
{
    QMutexLocker lock(&gLogMutex);
    if (gReady) { gLogStream.flush(); gLogFile.close(); gReady = false; }
}

void log(const char *section, const char *qa, const char *metric,
         double value, const char *unit, const QString &extra)
{
    // [로그 ON/OFF] 이 section 의 그룹이 꺼져 있으면(PERF_GRP_*=0) 즉시 반환 →
    //  CSV·콘솔 기록 안 함 + 문자열 포맷·flush 오버헤드도 발생 안 함(관측자 효과↓).
    if (!sectionEnabled(section)) return;
    const double t = nowMs();
    QMutexLocker lock(&gLogMutex);
    if (gReady) {
        gLogStream << QString::number(t, 'f', 3) << ','
                   << section << ',' << qa << ',' << metric << ','
                   << QString::number(value, 'f', 4) << ',' << unit << ','
                   << extra << '\n';
        // [관측자 효과↓] 매 줄 flush 안 함 — 1초 주기로만 디스크에 내림(핫패스 write 시스템콜 제거).
        if (t - gLastFlushMs >= kFlushIntervalMs) { gLogStream.flush(); gLastFlushMs = t; }
    }
    // [관측자 효과↓] 콘솔 echo 는 기본 OFF (켜면 매 줄 QString 포맷·stderr I/O 발생).
    if (gConsoleEcho) {
        qDebug().noquote() << QString("[PERF %1/%2] %3=%4 %5 %6")
                                  .arg(section).arg(qa).arg(metric)
                                  .arg(value, 0, 'f', 3).arg(unit).arg(extra);
    }
}

// 콘솔 echo on/off (기본 OFF). 측정 중엔 OFF 권장, 실시간 디버깅 시에만 ON.
void setConsoleEcho(bool on) { QMutexLocker lock(&gLogMutex); gConsoleEcho = on; }

int cpuCoreCount()
{
    int n = QThread::idealThreadCount();
    return n > 0 ? n : 1;
}

// ---------------------------------------------------------------------------
//  CPU% · PSS(메모리) · SoC 온도 · 서멀 스로틀 — 앱 내부에서 측정하지 않는다.
//  이유: 외부에서 측정 가능한 자원 지표를 인앱으로 넣으면 (1) Pi 에서 불필요한 상시 부하가 생기고
//        (2) 스로틀은 어차피 vcgencmd(외부)가 필요하므로, '외부 스크립트 + 인앱 샘플러'를 둘 다
//        돌리는 모순이 된다. 따라서 자원은 외부 스크립트 하나로 통합한다.
//  → tools/perf_resources.sh 가 pidstat(CPU)·smaps_rollup(PSS)·vcgencmd(온도/스로틀)를 한 CSV 로 남기고,
//    이 perf_log.csv 헤더의 epoch_ms_t0 로 시간 정렬한다(분석 시 두 로그 병합).
//  앱 내부 계측은 '밖에서 못 보는' 의미론적 지표(지연·정확도·FPS·백로그·이벤트루프 지연)만 담당.
// ---------------------------------------------------------------------------

} // namespace Perf
