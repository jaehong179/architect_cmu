#ifndef EVENTHANDLER_H
#define EVENTHANDLER_H
// EventHandler — 워치독 이벤트를 받아 사용자 알림으로 표시(SRP).
//  severity 별 분기만 한다(이벤트 타입 분기 없음 → OCP):
//   · Critical → 모달 팝업 QMessageBox (닫기 전까지 유지)
//   · Warning/Info → 비모달 팝업 QMessageBox (측정 흐름을 막지 않음, 사용자가 닫음)
#include <QObject>
#include "WatchdogEvent.h"

class QMainWindow;

class EventHandler : public QObject {
    Q_OBJECT
public:
    explicit EventHandler(QMainWindow *window, QObject *parent = nullptr);

public slots:
    void onEvent(const WatchdogEvent &ev);

private:
    QMainWindow *mWindow;
};

#endif // EVENTHANDLER_H
