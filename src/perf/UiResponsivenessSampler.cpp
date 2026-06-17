// UiResponsivenessSampler.cpp — UI 이벤트 루프 응답성 측정(SRP).
#include "UiResponsivenessSampler.h"
#include "PerfInstrumentation.h"
#include <QTimer>

UiResponsivenessSampler::UiResponsivenessSampler(QObject *parent, int periodMs)
    : QObject(parent), mPeriodMs(periodMs)
{
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &UiResponsivenessSampler::sample);
    timer->start(mPeriodMs);
}

// [PERF · §A-3 · QA-RT-01] 명목 주기 대비 '초과 지연'을 기록. 값이 클수록 메인 스레드가
//  막혀 UI 가 늦게 반응한다는 뜻.
void UiResponsivenessSampler::sample()
{
    double now = PERF_NOW();
    if (mHave) {
        double lag = (now - mLastMs) - (double)mPeriodMs;   // 명목 주기 대비 초과분
        if (lag < 0.0) lag = 0.0;
        PERF_LOG("A-3","QA-RT-01","ui_loop_lag_ms", lag, "ms","");
    }
    mLastMs = now;
    mHave   = true;
}
