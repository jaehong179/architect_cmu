#ifndef CAMERADISCONNECTCHECK_H
#define CAMERADISCONNECTCHECK_H

#include "IWatchdogCheck.h"

class CameraDisconnectCheck : public IWatchdogCheck {
public:
    void evaluate(const WatchdogContext &ctx, IEventSink &sink) override;
    const char *name() const override { return "CameraDisconnect"; }
private:
    bool   mFired = false;   // 에피소드당 1회. 카메라 복귀 시 재무장.
};

#endif // CAMERADISCONNECTCHECK_H
