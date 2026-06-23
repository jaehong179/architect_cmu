#ifndef IWATCHDOGCHECK_H
#define IWATCHDOGCHECK_H
// IWatchdogCheck — 점검 1조건 = 객체 1개(SRP).
//  워치독이 매 틱 evaluate() 를 호출하고, 조건이 충족되면 sink.post(event) 로 이벤트를 raise 한다.
//  새 이벤트 추가 = 이 인터페이스 구현 1개 + WatchdogWorker::addCheck() 1줄(OCP).
//  → Watchdog/EventHandler/기존 Check 는 일절 수정하지 않는다.
struct WatchdogContext;
class IEventSink;

class IWatchdogCheck {
public:
    virtual ~IWatchdogCheck() = default;
    virtual void evaluate(const WatchdogContext &ctx, IEventSink &sink) = 0;
    virtual const char *name() const = 0;
};

#endif // IWATCHDOGCHECK_H
