#include "AudioDeviceTimeoutCheck.h"
#include "WatchdogContext.h"
#include "IEventSink.h"
#include "WatchdogEvent.h"

void AudioDeviceTimeoutCheck::evaluate(const WatchdogContext &ctx, IEventSink &sink)
{
    const bool active = (ctx.mode == CaptureMode::Live) && ctx.measuring && !ctx.paused;
    if (!active) { mFired = false; return; }

    const bool removed = !ctx.deviceAlive;                  // ② 장치 목록에서 제거됨
    const bool stalled = ctx.msSinceBlock() > mTimeoutMs;   // ① 블록 타임아웃(원인 불문)

    if (removed || stalled) {
        if (!mFired) {
            mFired = true;
            WatchdogEvent ev;
            ev.id          = WatchdogEventId::AudioDeviceLost;
            ev.severity    = EventSeverity::Critical;
            ev.title       = QStringLiteral("Capture Device Lost");
            ev.message     = removed
                ? QStringLiteral("The capture device (USB) was disconnected during measurement.")
                : QStringLiteral("The capture device stopped responding (no audio for over 1 second).");
            ev.timestampMs = ctx.nowMs;
            sink.post(ev);
        }
    } else {
        mFired = false;   // 장치 복귀(블록 재도착 + 목록 존재) → 재무장
    }
}
