#ifndef RESOURCESAMPLER_H
#define RESOURCESAMPLER_H
// [PERF · §C/§D] 프로세스 자원(CPU% · 메모리 RSS · SoC 온도) 저빈도(기본 1Hz) 인앱 샘플러.
//  과거엔 자원 지표를 외부 도구(psrecord/pidstat/vcgencmd)로만 측정했으나(관측자 효과 우려),
//  1초 주기의 파일 읽기(/proc·/sys) 또는 WinAPI 호출은 오버헤드가 µs 수준이라 측정 대상을
//  사실상 오염시키지 않는다. 덕분에 Raspberry Pi 단독 실행 시에도 앱이 직접 CPU%/메모리/온도를
//  perf_log.csv 에 남길 수 있다(외부 도구 동시 실행 불필요).
//
//  측정 항목:
//    C-1 cpu_percent  프로세스 CPU%(전 코어 정규화: 100% = 모든 코어 포화)   [Win/Linux]
//    C-2 soc_temp_c   SoC 온도(°C)  /sys/class/thermal/thermal_zone0/temp     [Pi/Linux 전용]
//    D-1 rss_bytes    프로세스 RSS(상주 메모리)                                [Win/Linux]
//  스로틀 플래그(vcgencmd get_throttled)는 서브프로세스가 필요(블로킹·PATH 의존)해
//  여전히 외부 도구로 둔다 — 런북: docs/*/PERF_VERIFICATION_GUIDE.md
#include <QObject>
class QTimer;

class ResourceSampler : public QObject
{
    Q_OBJECT
public:
    explicit ResourceSampler(QObject *parent = nullptr, int periodMs = 1000);

private slots:
    void sample();   // 타이머 발화 시: CPU%/RSS/온도를 읽어 Perf 에 기록

private:
    // 플랫폼별 원시 읽기. 실패/미지원 시 음수 반환(해당 줄은 기록 생략).
    double readProcessCpuMs();   // 프로세스 누적 CPU 시간(user+sys), ms
    double readRssBytes();       // 상주 메모리, bytes
    double readSocTempC();       // SoC 온도, °C (Pi 전용; 그 외 -1)

    int    mPeriodMs;
    int    mCores    = 1;
    // CPU% 는 누적 CPU 시간의 '구간 증가분'을 벽시계 경과로 나눠 산출 → 직전 표본 보관
    double mLastWallMs = 0.0;
    double mLastCpuMs  = 0.0;
    bool   mHaveLast   = false;
};

#endif // RESOURCESAMPLER_H
