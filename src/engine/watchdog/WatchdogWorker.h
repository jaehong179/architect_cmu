#ifndef WATCHDOGWORKER_H
#define WATCHDOGWORKER_H
// WatchdogWorker — 워치독 스레드 워커(cross-cutting 감시의 주기 엔진).
//  자기 스레드의 QTimer 로 주기마다(tick) WatchdogState 를 스냅샷(WatchdogContext)으로 만들고,
//  등록된 모든 IWatchdogCheck::evaluate() 를 실행한다. Check 가 raise 한 이벤트는 IEventSink 로
//  받아 eventRaised 시그널(큐드)로 메인 스레드에 넘긴다.
//   · OCP: 체크 리스트(addCheck)가 단일 등록 지점.
//   · 스레드: tick 은 워커 스레드에서 atomics 만 읽음 → 잠금 없음. 알림 표시는 메인에서.
#include <QObject>
#include <vector>
#include <memory>
#include "IEventSink.h"
#include "WatchdogEvent.h"

class QTimer;
struct WatchdogState;
class IWatchdogCheck;

class WatchdogWorker : public QObject, public IEventSink {
    Q_OBJECT
public:
    explicit WatchdogWorker(const WatchdogState *state, int tickMs = 500, QObject *parent = nullptr);
    ~WatchdogWorker() override;

    // 스레드 시작 전(메인 스레드)에서만 호출 — 이후 추가 금지(워커 스레드가 리스트를 읽으므로).
    void addCheck(std::unique_ptr<IWatchdogCheck> check);

    // IEventSink — Check 가 호출. 워커 스레드에서 시그널 emit(메인으로 큐잉).
    void post(const WatchdogEvent &ev) override { emit eventRaised(ev); }

public slots:
    void start();   // 워커 스레드에서 QTimer 생성·구동(thread.started 에 연결)

signals:
    void eventRaised(const WatchdogEvent &ev);

private slots:
    void tick();

private:
    const WatchdogState *mState;
    int     mTickMs;
    QTimer *mTimer = nullptr;
    std::vector<std::unique_ptr<IWatchdogCheck>> mChecks;
};

#endif // WATCHDOGWORKER_H
