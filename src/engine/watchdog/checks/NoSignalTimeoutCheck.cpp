#include "NoSignalTimeoutCheck.h"
#include "WatchdogContext.h"
#include "IEventSink.h"
#include "WatchdogEvent.h"

// 블록이 이 시간 내 도착했으면 "장치 정상"으로 본다(장치 끊김은 별도 체크가 담당).
static constexpr double kBlockFreshMs = 1500.0;

void NoSignalTimeoutCheck::evaluate(const WatchdogContext &ctx, IEventSink &sink)
{
    const bool active   = (ctx.mode == CaptureMode::Live) && ctx.measuring && !ctx.paused;
    const bool deviceOk = ctx.deviceAlive && (ctx.msSinceBlock() <= kBlockFreshMs);

    // 비측정/정지/장치문제면 이 체크의 책임이 아님 → 재무장하고 빠진다.
    if (!active || !deviceOk) { mFired = false; return; }

    if (ctx.msSinceBeat() > mTimeoutMs) {
        if (!mFired) {
            mFired = true;
            WatchdogEvent ev;
            ev.id          = WatchdogEventId::NoSignalTimeout;
            ev.severity    = EventSeverity::Warning;
            ev.title       = QStringLiteral("No Watch Signal");
            ev.message     = QStringLiteral("No watch signal detected for over 10 seconds. "
                                            "Check the watch placement and microphone.");
            ev.timestampMs = ctx.nowMs;
            sink.post(ev);
        }
    } else {
        mFired = false;   // 비트 재검출 → 재무장
    }
}
