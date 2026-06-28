#ifndef CAMERADISCONNECTCHECK_H
#define CAMERADISCONNECTCHECK_H
// CameraDisconnectCheck — 측정용 USB 웹캠 분리/무응답을 치명 이벤트로 보고.
//  AudioDeviceTimeoutCheck 와 동일한 ①+② 계층 방어를 단일 보고 지점으로 통합(중복 알림 방지):
//   · ② cameraAlive=false        : QMediaDevices 가 videoInputs 목록에서 사라짐을 감지(즉시·정밀, 분리)
//   · ① msSinceCameraFrame>timeout: 프레임이 일정 시간 안 옴(원인 불문 — 행/프리즈/케이블 불량)
//  cameraActive(=vision 워커가 카메라를 연 상태)일 때만 동작 → 비전 미사용/미탑재면 no-op.
#include "IWatchdogCheck.h"

class CameraDisconnectCheck : public IWatchdogCheck {
public:
    explicit CameraDisconnectCheck(double frameTimeoutMs = 10000.0) : mTimeoutMs(frameTimeoutMs) {}
    void evaluate(const WatchdogContext &ctx, IEventSink &sink) override;
    const char *name() const override { return "CameraDisconnect"; }
private:
    double mTimeoutMs;
    bool   mFired = false;   // 에피소드당 1회. 카메라 복귀 시 재무장.
};

#endif // CAMERADISCONNECTCHECK_H
