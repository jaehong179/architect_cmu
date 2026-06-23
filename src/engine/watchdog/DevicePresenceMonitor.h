#ifndef DEVICEPRESENCEMONITOR_H
#define DEVICEPRESENCEMONITOR_H
// DevicePresenceMonitor — ② 장치 열거 변화 감시(메인 스레드).
//  QMediaDevices::audioInputsChanged(OS/드라이버 출처) 로 USB 오디오 장치 분리를 즉시 감지해
//  WatchdogState.deviceAlive 를 갱신만 한다. 실제 이벤트 판정/게시는 AudioDeviceTimeoutCheck 가
//  단일 지점에서 담당 → ①(블록 타임아웃)과 ②가 같은 이벤트로 합쳐져 중복 알림이 없다.
#include <QObject>
#include <QByteArray>

class QMediaDevices;
struct WatchdogState;

class DevicePresenceMonitor : public QObject {
    Q_OBJECT
public:
    explicit DevicePresenceMonitor(WatchdogState *state, QObject *parent = nullptr);

    void setActiveDevice(const QByteArray &deviceId);  // startLive 시 활성 캡처 장치 id
    void clearActiveDevice();                            // stop/비Live 시 추적 해제

private slots:
    void onAudioInputsChanged();

private:
    void refresh();   // 활성 장치가 현재 입력 목록에 있는지 → deviceAlive 갱신

    WatchdogState *mState;
    QMediaDevices *mDevices;
    QByteArray     mActiveId;
    bool           mTracking = false;
};

#endif // DEVICEPRESENCEMONITOR_H
