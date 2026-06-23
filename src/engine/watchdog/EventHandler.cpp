#include "EventHandler.h"
#include <QMainWindow>
#include <QMessageBox>
#include <QDebug>

EventHandler::EventHandler(QMainWindow *window, QObject *parent)
    : QObject(parent), mWindow(window) {}

void EventHandler::onEvent(const WatchdogEvent &ev)
{
    // [확증용 로그] 콘솔에서 발생 여부를 바로 확인(GUI와 독립).
    qInfo().noquote() << "[watchdog]"
                      << (ev.severity == EventSeverity::Critical ? "CRITICAL"
                          : ev.severity == EventSeverity::Warning ? "WARN" : "INFO")
                      << "id=" << int(ev.id) << "-" << ev.message;

    // 모두 모달 팝업으로 표시(닫기 전까지 유지). severity 로 아이콘만 분기.
    const QString title = ev.title.isEmpty() ? QStringLiteral("Notice") : ev.title;
    switch (ev.severity) {
    case EventSeverity::Critical:
        QMessageBox::critical(mWindow, title, ev.message);
        break;
    case EventSeverity::Warning:
        QMessageBox::warning(mWindow, title, ev.message);
        break;
    case EventSeverity::Info:
        QMessageBox::information(mWindow, title, ev.message);
        break;
    }
}
