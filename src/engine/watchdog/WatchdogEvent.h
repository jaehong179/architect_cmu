#ifndef WATCHDOGEVENT_H
#define WATCHDOGEVENT_H
// WatchdogEvent — 워치독이 감지한 상태 이벤트(값 객체).
//  이벤트가 severity/title/message 를 "스스로 서술"하므로 EventHandler 는 타입 분기 없이
//  표시만 한다(OCP). 새 이벤트 = EventId 추가 + IWatchdogCheck 구현 추가. 핸들러는 무수정.
#include <QString>
#include <QMetaType>

enum class EventSeverity { Info, Warning, Critical };

enum class WatchdogEventId {
    NoSignalTimeout,   // Live: 10초 이상 시계 신호(비트) 미검출
    AudioDeviceLost,   // 측정 장치(USB 오디오) 분리 또는 무응답
    CameraDisconnect,  // (확장 지점) 카메라 분리 — 카메라 모듈 생기면 사용
};

struct WatchdogEvent {
    WatchdogEventId id          = WatchdogEventId::NoSignalTimeout;
    EventSeverity   severity    = EventSeverity::Warning;
    QString         title;       // 모달 제목(Critical)
    QString         message;     // 사용자에게 보일 메시지
    double          timestampMs  = 0.0;  // 단조 시계(ms)
};

// 워커 스레드 → 메인 스레드 큐드 시그널로 전달되므로 메타타입 등록 필요(qRegisterMetaType).
Q_DECLARE_METATYPE(WatchdogEvent)

#endif // WATCHDOGEVENT_H
