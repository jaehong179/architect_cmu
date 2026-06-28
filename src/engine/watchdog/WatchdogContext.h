#ifndef WATCHDOGCONTEXT_H
#define WATCHDOGCONTEXT_H
// WatchdogContext — 매 틱 워치독이 WatchdogState 에서 빌드하는 읽기전용 스냅샷.
//  Check 는 오직 이 구조만 본다 → 공유 상태 수집 방식과 분리(DIP), 판정 로직은 순수·테스트 가능.
#include <cstdint>
#include "WatchdogState.h"   // CaptureMode

struct WatchdogContext {
    CaptureMode mode           = CaptureMode::None;
    bool        measuring      = false;
    bool        paused         = false;
    bool        deviceAlive    = true;
    int         sampleRateHz   = 0;
    uint64_t    totalSamples   = 0;
    double      nowMs          = 0.0;
    double      lastBlockMs    = 0.0;
    double      lastBeatMs     = 0.0;
    double      sessionStartMs = 0.0;

    bool        cameraActive      = false;
    bool        cameraAlive       = true;
    double      lastCameraFrameMs = 0.0;

    double msSinceBlock() const { return nowMs - lastBlockMs; }
    double msSinceBeat()  const { return nowMs - lastBeatMs; }
    double msSinceStart() const { return nowMs - sessionStartMs; }
    double msSinceCameraFrame() const { return nowMs - lastCameraFrameMs; }
};

#endif // WATCHDOGCONTEXT_H
