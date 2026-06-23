#ifndef IEVENTSINK_H
#define IEVENTSINK_H
// IEventSink — 이벤트 게시 경계(DIP).
//  Check 는 "누가 이벤트를 처리하는지" 모른 채 이 인터페이스로만 raise 한다.
//  → Check 는 UI·스레드·핸들러에 의존하지 않는다(테스트 가능, 순수).
struct WatchdogEvent;

class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void post(const WatchdogEvent &ev) = 0;
};

#endif // IEVENTSINK_H
