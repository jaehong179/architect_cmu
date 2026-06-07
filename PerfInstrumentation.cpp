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
#include <chrono>

#if defined(Q_OS_WIN)
  #include <windows.h>
  #include <psapi.h>            // GetProcessMemoryInfo (§D-1, Windows 분기)
#elif defined(Q_OS_LINUX)
  #include <unistd.h>           // sysconf, _SC_CLK_TCK, _SC_PAGESIZE
  #include <cstdio>             // fopen, fgets, fscanf (/proc 읽기)
  #include <cstring>            // strrchr, strtok (/proc/self/stat 파싱)
  #include <cstdlib>            // strtoul
  #include <QProcess>           // vcgencmd 호출 (§C-2, Pi 분기)
#endif

namespace {
// --- 로그 파일/동기화 (워커 스레드 + 메인 스레드 + 1Hz 타이머가 함께 기록) ---
QMutex             gLogMutex;
QFile              gLogFile;
QTextStream        gLogStream;
bool               gReady = false;
std::chrono::steady_clock::time_point gStart;   // nowMs() 의 기준점

// --- 로그 그룹 ON/OFF 게이트 (스위치는 PerfInstrumentation.h 의 PERF_GRP_* 매크로) ---
//  section 태그("A-1","B-4"…)를 그룹으로 매핑해, 그 그룹이 0이면 기록을 건너뛴다.
//  컴파일타임 상수(매크로)만 반환하므로 끈 그룹은 사실상 분기 비용도 없다.
inline bool sectionEnabled(const char *s)
{
#if !PERF_MASTER_ENABLE
    (void)s; return false;                       // 마스터 OFF → 전체 차단
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
        case 'C': return PERF_GRP_RESOURCES;                          // C-1/C-2
        case 'D': return PERF_GRP_MEMORY;                             // D-1
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
    // 현재 작업 디렉터리에 CSV 생성(매 실행 새로 씀). 분석 시 grep/스프레드시트로 사용.
    gLogFile.setFileName("perf_log.csv");
    if (!gLogFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "[PERF] perf_log.csv 열기 실패 — 콘솔(qDebug)로만 기록";
        gReady = false;
        return;
    }
    gLogStream.setDevice(&gLogFile);
    // 파일 헤더: 컬럼 정의 + 문서 연결 안내 (CSV 첫 줄들은 '#' 주석)
    gLogStream << "# PERF measurement log — see docs/PERF_VERIFICATION_GUIDE.md\n";
    gLogStream << "# session=" << sessionTag
               << " start=" << QDateTime::currentDateTime().toString(Qt::ISODate)
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
    qInfo() << "[PERF] 계측 로그 시작 → perf_log.csv";
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
        gLogStream.flush();   // 크래시/스로틀 상황에서도 데이터 보존
    }
    // 콘솔에도 남겨 실시간 관찰 가능 (문서 태그 [section/qa] 그대로 노출)
    qDebug().noquote() << QString("[PERF %1/%2] %3=%4 %5 %6")
                              .arg(section).arg(qa).arg(metric)
                              .arg(value, 0, 'f', 3).arg(unit).arg(extra);
}

int cpuCoreCount()
{
    int n = QThread::idealThreadCount();
    return n > 0 ? n : 1;
}

// ---------------------------------------------------------------------------
//  CPU% — §C-1 (QA-EE-01 "평균 CPU ≤70%").
//  '직전 호출 이후' 평균을 전 코어 기준(0~100)으로 환산한다.
//  ★1Hz 타이머 등 단일 스레드에서만 호출★ (정적 상태 사용).
// ---------------------------------------------------------------------------
#if defined(Q_OS_WIN)
double sampleProcessCpuPercent()
{
    static bool      have = false;
    static ULONGLONG prevProc100ns = 0;   // 커널+유저 누적 (100ns 단위)
    static ULONGLONG prevWall100ns = 0;

    FILETIME ftC, ftE, ftK, ftU, ftNow;
    if (!GetProcessTimes(GetCurrentProcess(), &ftC, &ftE, &ftK, &ftU)) return -1.0;
    GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG k = ((ULONGLONG)ftK.dwHighDateTime << 32) | ftK.dwLowDateTime;
    ULONGLONG u = ((ULONGLONG)ftU.dwHighDateTime << 32) | ftU.dwLowDateTime;
    ULONGLONG proc = k + u;
    ULONGLONG wall = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;

    double pct = 0.0;
    if (have && wall > prevWall100ns) {
        double dProc = (double)(proc - prevProc100ns);
        double dWall = (double)(wall - prevWall100ns);
        pct = (dProc / (dWall * cpuCoreCount())) * 100.0;   // 전 코어 정규화
    }
    prevProc100ns = proc; prevWall100ns = wall; have = true;
    return pct;
}
#elif defined(Q_OS_LINUX)
double sampleProcessCpuPercent()
{
    static bool   have = false;
    static double prevProcSec = 0.0;
    static double prevWallMs  = 0.0;

    // /proc/self/stat 필드 14(utime)+15(stime), clock tick 단위 → 초로 환산
    long ticks = sysconf(_SC_CLK_TCK); if (ticks <= 0) ticks = 100;
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return -1.0;
    // 14,15번째 필드만 필요. comm 필드에 공백/괄호가 있을 수 있어 ')' 이후부터 파싱.
    char buf[1024];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1.0; }
    fclose(f);
    char *p = strrchr(buf, ')');
    if (!p) return -1.0;
    p += 2; // ") " 다음 = state 필드 시작
    unsigned long utime = 0, stime = 0;
    int field = 3; // state=3 부터 카운트 (pid=1, comm=2)
    char *tok = strtok(p, " ");
    while (tok) {
        if (field == 14) utime = strtoul(tok, nullptr, 10);
        else if (field == 15) { stime = strtoul(tok, nullptr, 10); break; }
        tok = strtok(nullptr, " ");
        field++;
    }
    double procSec = (double)(utime + stime) / (double)ticks;
    double wallMs  = nowMs();

    double pct = 0.0;
    if (have && wallMs > prevWallMs) {
        double dProc = procSec - prevProcSec;
        double dWall = (wallMs - prevWallMs) / 1000.0;
        pct = (dProc / (dWall * cpuCoreCount())) * 100.0;   // 전 코어 정규화
    }
    prevProcSec = procSec; prevWallMs = wallMs; have = true;
    return pct;
}
#else
double sampleProcessCpuPercent() { return -1.0; }
#endif

// ---------------------------------------------------------------------------
//  RSS(상주 메모리) — §D-1 (QA-RT-03 "30분 후 증가 ≤200 MB·누수 없음")
// ---------------------------------------------------------------------------
#if defined(Q_OS_WIN)
qint64 sampleProcessRssBytes()
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (qint64)pmc.WorkingSetSize;   // Windows: Working Set = RSS 상당
    return -1;
}
#elif defined(Q_OS_LINUX)
qint64 sampleProcessRssBytes()
{
    // /proc/self/statm 의 2번째 값 = resident pages → × 페이지 크기
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return -1;
    long pages_total = 0, pages_res = 0;
    if (fscanf(f, "%ld %ld", &pages_total, &pages_res) != 2) { fclose(f); return -1; }
    fclose(f);
    long pageSize = sysconf(_SC_PAGESIZE); if (pageSize <= 0) pageSize = 4096;
    return (qint64)pages_res * (qint64)pageSize;
}
#else
qint64 sampleProcessRssBytes() { return -1; }
#endif

// ---------------------------------------------------------------------------
//  서멀 스로틀 — §C-2 (QA-EE-01). Pi 전용. Windows 는 N/A(false).
// ---------------------------------------------------------------------------
#if defined(Q_OS_LINUX)
bool readThrottled(unsigned &outFlag)
{
    // `vcgencmd get_throttled` → "throttled=0x50000" 형태. bit0=현재저전압, bit2=현재스로틀 등.
    QProcess proc;
    proc.start("vcgencmd", QStringList() << "get_throttled");
    if (!proc.waitForFinished(500)) { return false; }   // vcgencmd 없으면 측정 불가
    QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    int eq = out.indexOf('=');
    if (eq < 0) return false;
    bool ok = false;
    unsigned v = out.mid(eq + 1).toUInt(&ok, 0);   // 0x.. 자동 인식
    if (!ok) return false;
    outFlag = v;
    return true;
}
#else
bool readThrottled(unsigned &outFlag) { outFlag = 0; return false; }   // Windows: N/A
#endif

} // namespace Perf
