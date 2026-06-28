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
    // ── TFLite 라이브러리/모델 로드 ───────────────────────────────────────────
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

    // ── 카메라 선택/오픈 ──────────────────────────────────────────────────────
    const QList<QCameraDevice> cams = QMediaDevices::videoInputs();
    if (cams.isEmpty()) {
        qWarning() << "[vision] no webcam found";
        return;
    }
    const int camIdx = (kCameraId >= 0 && kCameraId < cams.size()) ? kCameraId : 0;
    const QCameraDevice dev = cams.at(camIdx);
    mActiveCamId = dev.id();   // ② 이 카메라의 분리를 추적

    mCamera  = new QCamera(dev, this);
    mSession = new QMediaCaptureSession(this);
    mSink    = new QVideoSink(this);
    mSession->setCamera(mCamera);
    mSession->setVideoSink(mSink);

    // 1280×720 에 가장 가까운 포맷 선택(best effort)
    QCameraFormat best;
    int bestScore = -1;
    const auto formats = dev.videoFormats();
    for (const QCameraFormat &f : formats) {
        const QSize r = f.resolution();
        qInfo().noquote() << QStringLiteral("[vision] supported format: %1x%2 @ %3-%4fps")
                                 .arg(r.width())
                                 .arg(r.height())
                                 .arg(f.minFrameRate())
                                 .arg(f.maxFrameRate());
        const int score = -(std::abs(r.width() - kCamWidth) + std::abs(r.height() - kCamHeight));
        if (score > bestScore) { bestScore = score; best = f; }
    }
    if (!best.isNull())
        mCamera->setCameraFormat(best);

    connect(mSink, &QVideoSink::videoFrameChanged, this, &VisionWorker::onFrame);

    // ② videoInputs 목록 변화 구독 — USB 카메라 분리를 즉시 감지(오디오 DevicePresenceMonitor 동일 패턴).
    mDevices = new QMediaDevices(this);
    connect(mDevices, &QMediaDevices::videoInputsChanged, this, &VisionWorker::onVideoInputsChanged);

    mTimer = new QTimer(this);
    mTimer->setInterval(kInferIntervalMs);
    connect(mTimer, &QTimer::timeout, this, &VisionWorker::onTick);

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
}

void VisionWorker::stop()
{
    if (mTimer) { mTimer->stop(); }
    if (mCamera) { mCamera->stop(); }
    mReady = false;
    if (mWatchdog) mWatchdog->cameraActive.store(false, std::memory_order_relaxed);  // 감시 해제
}

// ② 카메라 열거 변화 → 활성 카메라가 목록에 아직 있는지 검사해 cameraAlive 갱신.
void VisionWorker::onVideoInputsChanged()
{
    refreshCameraPresence();
}

void VisionWorker::refreshCameraPresence()
{
    if (!mWatchdog || mActiveCamId.isEmpty()) return;
    bool present = false;
    const QList<QCameraDevice> cams = QMediaDevices::videoInputs();
    for (const QCameraDevice &d : cams)
        if (d.id() == mActiveCamId) { present = true; break; }
    mWatchdog->cameraAlive.store(present, std::memory_order_relaxed);
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
