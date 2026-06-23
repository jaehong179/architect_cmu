#include "WatchdogWorker.h"
#include "WatchdogState.h"
#include "WatchdogContext.h"
#include "WatchdogClock.h"
#include "IWatchdogCheck.h"
#include <QTimer>
#include <QDebug>

WatchdogWorker::WatchdogWorker(const WatchdogState *state, int tickMs, QObject *parent)
    : QObject(parent), mState(state), mTickMs(tickMs) {}

WatchdogWorker::~WatchdogWorker() = default;

void WatchdogWorker::addCheck(std::unique_ptr<IWatchdogCheck> check)
{
    if (check) mChecks.push_back(std::move(check));
}

void WatchdogWorker::start()
{
    if (mTimer) return;
    mTimer = new QTimer(this);                       // 워커 스레드 소유(이 슬롯이 워커 스레드에서 실행됨)
    connect(mTimer, &QTimer::timeout, this, &WatchdogWorker::tick);
    mTimer->start(mTickMs);
    qInfo().noquote() << "[watchdog] thread started, tick=" << mTickMs << "ms checks=" << int(mChecks.size());
}

void WatchdogWorker::tick()
{
    if (!mState) return;

    // WatchdogState(원자적) → 읽기전용 스냅샷. 잠금 없이 일관 스냅샷 빌드.
    WatchdogContext ctx;
    ctx.mode           = static_cast<CaptureMode>(mState->mode.load(std::memory_order_relaxed));
    ctx.measuring      = mState->measuring.load(std::memory_order_relaxed);
    ctx.paused         = mState->paused.load(std::memory_order_relaxed);
    ctx.deviceAlive    = mState->deviceAlive.load(std::memory_order_relaxed);
    ctx.sampleRateHz   = mState->sampleRateHz.load(std::memory_order_relaxed);
    ctx.totalSamples   = mState->totalSamples.load(std::memory_order_relaxed);
    ctx.lastBlockMs    = mState->lastBlockMs.load(std::memory_order_relaxed);
    ctx.lastBeatMs     = mState->lastBeatMs.load(std::memory_order_relaxed);
    ctx.sessionStartMs = mState->sessionStartMs.load(std::memory_order_relaxed);
    ctx.nowMs          = wdNowMs();

    for (auto &c : mChecks)
        if (c) c->evaluate(ctx, *this);
}
