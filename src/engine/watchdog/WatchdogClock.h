#ifndef WATCHDOGCLOCK_H
#define WATCHDOGCLOCK_H
// 워치독 전용 단조 시계(ms).
//  PERF_NOW()는 PERF 비활성 빌드에서 0을 반환하므로 제품 기능인 워치독에는 쓸 수 없다.
//  steady_clock 은 항상 동작하고 되감기지 않아 타임아웃 판정에 적합하다.
#include <chrono>

inline double wdNowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

#endif // WATCHDOGCLOCK_H
