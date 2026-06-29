// VisionWorker.h — USB 웹캠에서 1Hz 로 watch-position 을 추론하는 워커(전용 QThread).
//   기존 TAudioWorker 와 동일한 QObject+QThread 패턴. MainWindow 가 스레드로 이동시켜 병렬 실행.
#ifndef VISION_WORKER_H
#define VISION_WORKER_H

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QVideoFrame>

#include <memory>
#include <vector>

class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QTimer;
class QMediaDevices;
struct WatchdogState;

namespace vision {

class TfliteApi;

class VisionWorker : public QObject
{
    Q_OBJECT
public:
    explicit VisionWorker(QObject *parent = nullptr);
    ~VisionWorker() override;

    void setWatchdogState(WatchdogState *state) { mWatchdog = state; }

public slots:
    // 스레드 시작 후 호출(카메라/모델 생성은 워커 스레드에서 이뤄져야 한다).
    void start();
    // 종료(카메라/타이머 정지).
    void stop();

signals:
    // 추론 결과(라벨 + 신뢰도). UI 연결용(현재는 단순 print 보조).
    void resultReady(const QString &label, float confidence);
    // 카메라 사용 가능/분리 상태 변화. UI 가 watch-position 아이콘 활성/비활성에 사용.
    void cameraAvailabilityChanged(bool available);

private slots:
    void onFrame(const QVideoFrame &frame);
    void onTick();
    void onVideoInputsChanged();   // ② videoInputs 목록 변화 → 카메라 분리/연결 자동 정지/재개

private:
    void openCamera();    // 카메라 오픈 + 캡처/추론 시작(cameraActive/Alive publish)
    void closeCamera();   // 카메라/캡처 정지(추론 중단)

private:
    std::unique_ptr<TfliteApi> mTflite;
    QCamera               *mCamera  = nullptr;
    QMediaCaptureSession  *mSession = nullptr;
    QVideoSink            *mSink    = nullptr;
    QTimer                *mTimer   = nullptr;

    WatchdogState         *mWatchdog = nullptr;  // liveness publish 대상(소유 안 함)
    QMediaDevices         *mDevices  = nullptr;  // videoInputs 변화 구독(분리 감지)
    QByteArray             mActiveCamId;         // 현재 열린 카메라 id(분리 판정 기준)

    QByteArray             mModelData;   // qrc 모델 바이트(인터프리터보다 오래 유지)
    QMutex                 mFrameMutex;
    QImage                 mLatestFrame; // RGB888 최신 프레임(저빈도 샘플링)
    std::vector<float>     mInput;       // 전처리 결과 버퍼(재사용)
    bool                   mReady = false;
};

} // namespace vision

#endif // VISION_WORKER_H
