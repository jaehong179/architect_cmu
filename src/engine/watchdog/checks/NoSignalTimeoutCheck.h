#ifndef NOSIGNALTIMEOUTCHECK_H
#define NOSIGNALTIMEOUTCHECK_H
// NoSignalTimeoutCheck — Live + 측정 중 + 장치 정상(블록 도착)인데 비트(A 이벤트)가
//  N초(기본 10초) 이상 없으면 경고. "장치는 살아있는데 시계 신호가 안 잡힘" 상황.
//  장치 자체가 끊긴 경우는 AudioDeviceTimeoutCheck 가 담당하므로 여기선 장치 정상일 때만 본다.
#include "IWatchdogCheck.h"

class NoSignalTimeoutCheck : public IWatchdogCheck {
public:
    explicit NoSignalTimeoutCheck(double timeoutMs = 10000.0) : mTimeoutMs(timeoutMs) {}
    void evaluate(const WatchdogContext &ctx, IEventSink &sink) override;
    const char *name() const override { return "NoSignalTimeout"; }
private:
    double mTimeoutMs;
    bool   mFired = false;   // 에피소드당 1회(스팸 방지). 신호 복귀 시 재무장.
};

#endif // NOSIGNALTIMEOUTCHECK_H
