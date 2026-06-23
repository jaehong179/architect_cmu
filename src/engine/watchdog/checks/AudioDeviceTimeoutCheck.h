#ifndef AUDIODEVICETIMEOUTCHECK_H
#define AUDIODEVICETIMEOUTCHECK_H
// AudioDeviceTimeoutCheck — 측정 장치(USB 오디오) 분리/무응답을 치명 이벤트로 보고.
//  ①+② 계층 방어를 단일 보고 지점으로 통합(중복 알림 방지):
//   · ② deviceAlive=false : QMediaDevices 가 장치 목록에서 사라짐을 감지(즉시·정밀, 분리)
//   · ① msSinceBlock > timeout : 블록이 일정 시간 안 옴(원인 불문 — 행/프리즈/케이블 불량)
#include "IWatchdogCheck.h"

class AudioDeviceTimeoutCheck : public IWatchdogCheck {
public:
    explicit AudioDeviceTimeoutCheck(double blockTimeoutMs = 1000.0) : mTimeoutMs(blockTimeoutMs) {}
    void evaluate(const WatchdogContext &ctx, IEventSink &sink) override;
    const char *name() const override { return "AudioDeviceTimeout"; }
private:
    double mTimeoutMs;
    bool   mFired = false;   // 에피소드당 1회. 장치 복귀 시 재무장.
};

#endif // AUDIODEVICETIMEOUTCHECK_H
