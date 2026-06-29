#include "CameraDisconnectCheck.h"
#include "WatchdogContext.h"
#include "IEventSink.h"
#include "WatchdogEvent.h"

void CameraDisconnectCheck::evaluate(const WatchdogContext &ctx, IEventSink &sink)
{
    // 카메라(vision)가 가동 중일 때만 감시. 비전 미사용/카메라 미 연결시 책임 아님
    if (!ctx.cameraActive) { mFired = false; return; }

    const bool removed = !ctx.cameraAlive;   // 장치 목록에서 제거됨(USB 분리)

    if (removed) {
        if (!mFired) {
            mFired = true;
            WatchdogEvent ev;
            ev.id          = WatchdogEventId::CameraDisconnect;
            ev.severity    = EventSeverity::Critical;
            ev.title       = QStringLiteral("Camera Lost");
            ev.message     = QStringLiteral("The camera (USB) was disconnected.");
            ev.timestampMs = ctx.nowMs;
            sink.post(ev);
        }
    } else {
        mFired = false;   // 카메라 복귀(목록 존재) → 재활성
    }
}
