// VisionWorker.cpp — 웹캠 캡처 + 1Hz TFLite 추론 구현.
#include "VisionWorker.h"

#include "TfliteApi.h"
#include "VisionConfig.h"
#include "VisionPreprocess.h"

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QDebug>
#include <QFile>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QSize>
#include <QTimer>
#include <QVideoSink>

#include "WatchdogState.h"   // liveness publish 대상
#include "WatchdogClock.h"   // wdNowMs() — 워치독과 동일 단조 시계

#include <algorithm>

namespace vision {

VisionWorker::VisionWorker(QObject *parent)
    : QObject(parent)
    , mTflite(std::make_unique<TfliteApi>())
{
}

VisionWorker::~VisionWorker()
{
    stop();
}

void VisionWorker::start()
{
    // ── TFLite 라이브러리/모델 로드(1회) ───────────────────────────────────────
    if (!mTflite->loadLibrary()) {
        qWarning() << "[vision] TFLite load failed:" << QString::fromStdString(mTflite->lastError());
        return;
    }
    QFile mf(QString::fromUtf8(kModelResource));
    if (!mf.open(QIODevice::ReadOnly)) {
        qWarning() << "[vision] cannot open embedded model:" << kModelResource;
        return;
    }
    mModelData = mf.readAll();   // 인터프리터 수명 동안 보관
    mf.close();
    if (!mTflite->initModel(mModelData.constData(),
                            static_cast<std::size_t>(mModelData.size()),
                            kNumThreads)) {
        qWarning() << "[vision] model init failed:" << QString::fromStdString(mTflite->lastError());
        return;
    }
    qInfo() << "[vision] model ready. input elems:" << mTflite->inputElementCount()
            << "output elems:" << mTflite->outputElementCount()
            << "| build model:" << (kUseInt8Model ? "int8(quantized)" : "fp32");

    // ② videoInputs 목록 변화 구독 — USB 카메라 분리/연결을 즉시 감지.
    //    카메라 오픈 여부와 무관하게 워커 수명 동안 유지 → 분리 후 재연결을 자동으로 감지한다.
    mDevices = new QMediaDevices(this);
    connect(mDevices, &QMediaDevices::videoInputsChanged, this, &VisionWorker::onVideoInputsChanged);

    // 캡처 세션 + 비디오 싱크는 워커 수명 동안 1회만 생성·유지한다.
    //  재연결 시 QCamera 만 교체하고 싱크(프레임 전달 경로)는 그대로 두어야
    mSession = new QMediaCaptureSession(this);
    mSink    = new QVideoSink(this);
    mSession->setVideoSink(mSink);
    connect(mSink, &QVideoSink::videoFrameChanged, this, &VisionWorker::onFrame);

    // 1Hz 추론 타이머(카메라가 열려 있을 때만 start/stop).
    mTimer = new QTimer(this);
    mTimer->setInterval(kInferIntervalMs);
    connect(mTimer, &QTimer::timeout, this, &VisionWorker::onTick);

    // 연결된 카메라가 있으면 즉시 오픈, 없으면 연결될 때까지 대기(아이콘 비활성).
    if (!QMediaDevices::videoInputs().isEmpty())
        openCamera();
    else
        qInfo() << "[vision] no webcam found — waiting for connection";
}

// 카메라 오픈 + 캡처/추론 시작. cameraActive/cameraAlive 를 워치독에 publish.
void VisionWorker::openCamera()
{
    if (mCamera || !mSession) return;   // 이미 열림 / 세션 미준비

    const QList<QCameraDevice> cams = QMediaDevices::videoInputs();
    if (cams.isEmpty()) return;
    const int camIdx = (kCameraId >= 0 && kCameraId < cams.size()) ? kCameraId : 0;
    const QCameraDevice dev = cams.at(camIdx);
    mActiveCamId = dev.id();   // ② 이 카메라의 분리를 추적

    mCamera = new QCamera(dev, this);
    mSession->setCamera(mCamera);

    // 1280×720 에 가장 가까운 포맷 선택(best effort)
    QCameraFormat best;
    int bestScore = -1;
    const auto formats = dev.videoFormats();
    for (const QCameraFormat &f : formats) {
        const QSize r = f.resolution();
        const int score = -(std::abs(r.width() - kCamWidth) + std::abs(r.height() - kCamHeight));
        if (score > bestScore) { bestScore = score; best = f; }
    }
    if (!best.isNull())
        mCamera->setCameraFormat(best);

    // 카메라 오류(분리 직후 재오픈 실패 등)를 콘솔에 남겨 진단을 돕는다.
    connect(mCamera, &QCamera::errorOccurred, this,
            [](QCamera::Error err, const QString &msg) {
                if (err != QCamera::NoError)
                    qWarning() << "[vision] camera error:" << msg;
            });

    { QMutexLocker lock(&mFrameMutex); mLatestFrame = QImage(); }   // 이전 프레임 잔상 제거

    mReady = true;
    mCamera->start();
    mTimer->start();

    if (mWatchdog) {
        const double now = wdNowMs();
        mWatchdog->lastCameraFrameMs.store(now, std::memory_order_relaxed);
        mWatchdog->cameraAlive.store(true, std::memory_order_relaxed);
        mWatchdog->cameraActive.store(true, std::memory_order_relaxed);
    }
    qInfo() << "[vision] webcam started:" << dev.description();
    emit cameraAvailabilityChanged(true);
}

// 카메라/캡처 정지(추론 중단). 세션/싱크/타이머/모델은 유지(재오픈 대비).
void VisionWorker::closeCamera()
{
    if (mTimer) mTimer->stop();
    mReady = false;
    if (mCamera) {
        mCamera->stop();
        if (mSession) mSession->setCamera(nullptr);   // 세션에서 분리(싱크는 유지)
        delete mCamera;
        mCamera = nullptr;
    }
    mActiveCamId.clear();
    QMutexLocker lock(&mFrameMutex);
    mLatestFrame = QImage();
}

void VisionWorker::stop()
{
    closeCamera();
    delete mSession; mSession = nullptr;
    delete mSink;    mSink    = nullptr;
    if (mWatchdog) mWatchdog->cameraActive.store(false, std::memory_order_relaxed);  // 감시 해제
}

// ② 카메라 열거 변화 → 분리/연결을 판정해 추론 스레드를 자동으로 정지/재개.
void VisionWorker::onVideoInputsChanged()
{
    const QList<QCameraDevice> cams = QMediaDevices::videoInputs();

    if (mCamera) {
        // 카메라가 열린 상태 — 활성 카메라가 아직 목록에 있는지 확인.
        bool present = false;
        for (const QCameraDevice &d : cams)
            if (d.id() == mActiveCamId) { present = true; break; }
        if (!present) {
            // USB 분리: 워치독에 분리 보고(모달 유지) → 추론 정지 → 아이콘 비활성.
            if (mWatchdog) mWatchdog->cameraAlive.store(false, std::memory_order_relaxed);
            closeCamera();
            qInfo() << "[vision] webcam disconnected — inference stopped";
            emit cameraAvailabilityChanged(false);
        }
    } else {
        // 카메라가 닫힌 상태 — 사용할 수 있는 카메라가 생기면 자동 재개.
        if (!cams.isEmpty()) {
            qInfo() << "[vision] webcam connected — restarting inference";
            openCamera();
        }
    }
}

void VisionWorker::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid()) return;
    // ① 프레임 도착 = 카메라 liveness — 오디오가 블록당 lastBlockMs 를 publish 하는 것과 동일.
    if (mWatchdog) {
        mWatchdog->lastCameraFrameMs.store(wdNowMs(), std::memory_order_relaxed);
        mWatchdog->cameraAlive.store(true, std::memory_order_relaxed);
    }
    QImage img = frame.toImage();
    if (img.isNull()) return;
    if (img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB888);
    QMutexLocker lock(&mFrameMutex);
    mLatestFrame = img;   // 최신 프레임만 보관(1Hz 샘플링이라 매 프레임 처리 안 함)
}

void VisionWorker::onTick()
{
    if (!mReady || !mTflite->isReady()) return;

    QImage img;
    {
        QMutexLocker lock(&mFrameMutex);
        if (mLatestFrame.isNull()) return;
        img = mLatestFrame;   // implicit-shared copy
    }

    preprocessFrame(img.constBits(), img.width(), img.height(),
                    static_cast<int>(img.bytesPerLine()), mInput);

    std::vector<float> probs;
    if (!mTflite->invoke(mInput.data(), mInput.size(), probs)) {
        qWarning() << "[vision] invoke failed:" << QString::fromStdString(mTflite->lastError());
        return;
    }
    if (probs.empty()) return;

    int best = 0;
    for (int i = 1; i < static_cast<int>(probs.size()); ++i)
        if (probs[i] > probs[best]) best = i;
    const float conf = probs[best];

    QString label = (best < kNumClasses)
                        ? QString::fromUtf8(kLabels[best].data(), static_cast<int>(kLabels[best].size()))
                        : QStringLiteral("?");
    const bool uncertain = conf < kConfThresh;

    qInfo().noquote() << QStringLiteral("[vision] %1%2  (%3%)")
                             .arg(uncertain ? QStringLiteral("? ") : QString())
                             .arg(label)
                             .arg(QString::number(conf * 100.0f, 'f', 1));

    emit resultReady(label, conf);
}

} // namespace vision
