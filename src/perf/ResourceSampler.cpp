// ResourceSampler.cpp — 프로세스 자원(CPU%·RSS·온도) 1Hz 인앱 샘플러.
//  관측자 효과↓: 저빈도 + 가벼운 파일 읽기/WinAPI. 핫패스 아님(별도 타이머).
#include "ResourceSampler.h"
#include "PerfInstrumentation.h"
#include <QTimer>
#include <QThread>
#include <QFile>
#include <QByteArray>
#include <QList>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <psapi.h>
#elif defined(Q_OS_LINUX)
#  include <unistd.h>   // sysconf(_SC_CLK_TCK), _SC_PAGESIZE
#endif

ResourceSampler::ResourceSampler(QObject *parent, int periodMs)
    : QObject(parent), mPeriodMs(periodMs)
{
    mCores = QThread::idealThreadCount();
    if (mCores < 1) mCores = 1;
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ResourceSampler::sample);
    timer->start(mPeriodMs);
}

void ResourceSampler::sample()
{
    const double wall  = Perf::nowMs();
    const double cpuMs = readProcessCpuMs();

    // [C-1] CPU% = (이번 구간 CPU 시간 증가분 / 벽시계 경과) ÷ 코어수 ×100  (100%=전 코어 포화)
    if (mHaveLast && cpuMs >= 0.0 && wall > mLastWallMs) {
        double pct = (cpuMs - mLastCpuMs) / (wall - mLastWallMs) * 100.0 / (double)mCores;
        if (pct < 0.0) pct = 0.0;   // 시계 분해능/리셋 보호
        Perf::log("C-1","QA-EE-01","cpu_percent", pct, "%", QString("cores=%1").arg(mCores));
    }
    if (cpuMs >= 0.0) { mLastCpuMs = cpuMs; mLastWallMs = wall; mHaveLast = true; }

    // [D-1] RSS(상주 메모리)
    double rss = readRssBytes();
    if (rss >= 0.0)
        Perf::log("D-1","QA-RT-03","rss_bytes", rss, "bytes",
                  QString("mib=%1").arg(rss / 1048576.0, 0, 'f', 1));

    // [C-2] SoC 온도(Pi 전용). Windows/미지원은 음수 → 기록 생략.
    double temp = readSocTempC();
    if (temp >= 0.0)
        Perf::log("C-2","QA-EE-01","soc_temp_c", temp, "degC","");
}

// ─────────────────────────── 플랫폼별 원시 읽기 ───────────────────────────
#if defined(Q_OS_WIN)

double ResourceSampler::readProcessCpuMs()
{
    FILETIME create, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) return -1.0;
    // kernel/user 는 100ns 단위. user+kernel = 프로세스 누적 CPU 시간.
    auto to100ns = [](const FILETIME &ft) -> unsigned long long {
        return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    };
    unsigned long long ticks = to100ns(kernel) + to100ns(user);
    return (double)ticks / 10000.0;   // 100ns → ms
}

double ResourceSampler::readRssBytes()
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return -1.0;
    return (double)pmc.WorkingSetSize;   // 상주 메모리(워킹셋) ≈ RSS
}

double ResourceSampler::readSocTempC() { return -1.0; }   // Windows: N/A

#elif defined(Q_OS_LINUX)

double ResourceSampler::readProcessCpuMs()
{
    QFile f(QStringLiteral("/proc/self/stat"));
    if (!f.open(QIODevice::ReadOnly)) return -1.0;
    QByteArray s = f.readAll();
    // comm 필드(괄호)에 공백/괄호가 있을 수 있어 마지막 ')' 뒤부터 파싱.
    int rp = s.lastIndexOf(')');
    if (rp < 0) return -1.0;
    QList<QByteArray> f2 = s.mid(rp + 2).simplified().split(' ');
    // ')' 뒤 토큰: [0]=state(field3) ... utime=field14→idx11, stime=field15→idx12
    if (f2.size() < 13) return -1.0;
    bool ok1=false, ok2=false;
    unsigned long long utime = f2[11].toULongLong(&ok1);
    unsigned long long stime = f2[12].toULongLong(&ok2);
    if (!ok1 || !ok2) return -1.0;
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    return (double)(utime + stime) * 1000.0 / (double)hz;   // ticks → ms
}

double ResourceSampler::readRssBytes()
{
    QFile f(QStringLiteral("/proc/self/statm"));
    if (!f.open(QIODevice::ReadOnly)) return -1.0;
    QList<QByteArray> f2 = f.readAll().simplified().split(' ');
    if (f2.size() < 2) return -1.0;
    bool ok=false;
    unsigned long long residentPages = f2[1].toULongLong(&ok);   // field2 = resident set size(pages)
    if (!ok) return -1.0;
    long pg = sysconf(_SC_PAGESIZE);
    if (pg <= 0) pg = 4096;
    return (double)residentPages * (double)pg;
}

double ResourceSampler::readSocTempC()
{
    QFile f(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    if (!f.open(QIODevice::ReadOnly)) return -1.0;
    bool ok=false;
    double milli = f.readAll().trimmed().toDouble(&ok);   // 보통 millidegree-C
    if (!ok) return -1.0;
    return milli / 1000.0;
}

#else   // 기타 OS: 미지원

double ResourceSampler::readProcessCpuMs() { return -1.0; }
double ResourceSampler::readRssBytes()     { return -1.0; }
double ResourceSampler::readSocTempC()     { return -1.0; }

#endif
