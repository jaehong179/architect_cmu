#include "CameraDisconnectCheck.h"
#include "WatchdogContext.h"
#include "IEventSink.h"
#include "WatchdogEvent.h"

void CameraDisconnectCheck::evaluate(const WatchdogContext &ctx, IEventSink &sink)
{
    // 카메라(vision)가 가동 중일 때만 감시. 비전 미사용/미탑재면 책임 아님 → 재무장 후 빠진다.
    if (!ctx.cameraActive) { mFired = false; return; }

    const bool removed = !ctx.cameraAlive;                       // ② 장치 목록에서 제거됨
    const bool stalled = ctx.msSinceCameraFrame() > mTimeoutMs;  // ① 프레임 타임아웃(원인 불문)

    if (removed || stalled) {
        if (!mFired) {
            mFired = true;
            WatchdogEvent ev;
            ev.id          = WatchdogEventId::CameraDisconnect;
            ev.severity    = EventSeverity::Critical;
            ev.title       = QStringLiteral("Camera Lost");
            ev.message     = removed
                ? QStringLiteral("The camera (USB) was disconnected.")
                : QStringLiteral("The camera stopped responding (no video frames for over 10 seconds).");
            ev.timestampMs = ctx.nowMs;
            sink.post(ev);
        }
    } else {
        mFired = false;   // 카메라 복귀(프레임 재도착 + 목록 존재) → 재무장
    }
}
