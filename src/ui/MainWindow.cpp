#include <QtGlobal>
#include "MainWindow.h"
#include "PositionChangeDialog.h"
#include "./ui_MainWindow.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QToolButton>
#include <QTabBar>
#include <QQuickWidget>
#include <QQmlContext>
#include <QFileDialog>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QTabWidget>
#include <QMouseEvent>
#include <QDebug>
#include <QtMath>
#include "WaveHeader.h"
#include "WavFileReader.h"
#include "PerfInstrumentation.h"
#include "UiResponsivenessSampler.h"
#include <QResizeEvent>
#include <QSettings>

#include "tabs/TabManager.h"
#include "tabs/TabRateScope.h"
#include "tabs/TabSoundPrint.h"
#include "tabs/TabTraceDisplay.h"
#include "tabs/TabVarioStability.h"
#include "tabs/TabSequenceDisplay.h"
#include "tabs/TabBeatNoiseScope.h"
#include "tabs/TabBeatErrorTrace.h"
#include "tabs/TabLongTermPerformance.h"
#include "tabs/TabEscapementAnalyzer.h"
#include "tabs/TabSpectrogram.h"
#include "tabs/TabWaveformCompare.h"
#include "tabs/TabSyncSweepScope.h"
#include "tabs/TabFilterViews.h"
#include "tabs/ReadoutBar.h"
#include "WarmupOverlay.h"

#if defined(Q_OS_LINUX)
#include "LinuxAudio.h"
#elif defined(Q_OS_WIN)
#include "WindowsAudio.h"
#endif

#ifdef ENABLE_VISION
#include <QThread>
#include "VisionWorker.h"          // [vision] 웹캠 1Hz watch-position 추론 워커
#endif

#ifdef ENABLE_DIAG
#include <QThread>
#include "DiagWorker.h"            // [diag] Stop 시 t1/t3 고장유형 진단 워커
#include "diag/DiagCatalog.h"      // [diag] 고장유형 카탈로그(제목/원인/조치/예시)
#include "diag/DiagBanner.h"       // [diag] 진단 완료 알림 배너
#include "diag/DiagDetailDialog.h" // [diag] 진단 상세 가이드 창
#endif

#define  LIVE     0
#define  PLAYBACK 1
#define  SIM      2

#define PLAYBACK_OR_SIM_PCM             "Playback/Sim"
#define PREF_NAME_WELSHI                "Welshi USB"
#define PREF_NAME_CHINESE_GENERIC       "Chinese Generic USB"
#define WINDOWS_SOUND_ENDPOINT_NAME      "USB PnP Sound Device"
#define WINDOWS_SOUND_MIC_NAME           "USB PnP Sound Device"
#define WINDOWS_SOUND_MIC_PERCENT_VOLUME 50

#define LINUX_SOUND_CARD_NAME           "USB PnP Sound Device"
#define LINUX_SOUND_MIC_NAME            "Mic Capture Volume"
#define LINUX_SOUND_AGC_NAME            "Auto Gain Control"
#define LINUX_SOUND_MIC_PERCENT_VOLUME  50

static QString RenameAudioDevices[][2] =
{
  {"USB PnP Sound Device",        PREF_NAME_WELSHI},
  {"C-Media USB Headphone Set",   PREF_NAME_CHINESE_GENERIC},
  {"CM108 Audio Controller Mono", PREF_NAME_WELSHI},
  {"Audio Adapter Mono",          PREF_NAME_CHINESE_GENERIC}
};

static QString PreferredAudioDevices[] =
{
  PREF_NAME_WELSHI,
  PREF_NAME_CHINESE_GENERIC,
  "Cubilux HA-3",
  "CUBILUX CA7"
};

static int ManualAutoBPH[]={0, //Auto
                    3600,  6000,  7200,  7380,  7440,  7800,  9000,  9100, 10800, 11880,
                    12000, 12342, 12480, 12600, 13320, 13440, 13500, 14000, 14040, 14160,
                    14200, 14280, 14400, 14520, 14580, 14760, 14850, 15000, 15360, 15600,
                    16200, 16320, 16800, 17196, 11258, 17280, 17186, 17897, 18000, 18049,
                    18514, 19332, 19440, 19800, 20160, 20222, 20944, 21000, 21031, 21306,
                    21600, 25200, 28800, 32400, 36000, 43200};

static int SimBPH[]={3600,  6000,  7200,  7380,  7440,  7800,  9000,  9100, 10800, 11880,
                     12000, 12342, 12480, 12600, 13320, 13440, 13500, 14000, 14040, 14160,
                     14200, 14280, 14400, 14520, 14580, 14760, 14850, 15000, 15360, 15600,
                     16200, 16320, 16800, 17196, 11258, 17280, 17186, 17897, 18000, 18049,
                     18514, 19332, 19440, 19800, 20160, 20222, 20944, 21000, 21031, 21306,
                     21600, 25200, 28800, 32400, 36000, 43200};

static int AveragingPeriodList[]={2,4,8,10,12,20,20,30,40,50,60,120,240};
static constexpr qint64 kDetectedPositionHoldMs = 3000;

int MainWindow::sequenceIndexFromDetectedPosition(const QString &detectedLabel)
{
    const QString raw = detectedLabel.trimmed().toUpper();
    if (raw.isEmpty() || raw == QStringLiteral("?"))
        return -1;

    const int underscore = raw.indexOf(QLatin1Char('_'));
    const QString suffix = (underscore >= 0 && (underscore + 1) < raw.size())
        ? raw.mid(underscore + 1)
        : raw;

    if (suffix == QStringLiteral("DU") || suffix == QStringLiteral("CH")) return 0;
    if (suffix == QStringLiteral("DD") || suffix == QStringLiteral("CB")) return 1;
    if (suffix == QStringLiteral("CR") || suffix == QStringLiteral("9H")) return 2;
    if (suffix == QStringLiteral("CL") || suffix == QStringLiteral("6H")) return 3;
    if (suffix == QStringLiteral("CU") || suffix == QStringLiteral("3H")) return 4;
    if (suffix == QStringLiteral("CD") || suffix == QStringLiteral("12H")) return 5;
    return -1;
}

void MainWindow::updateDetectedPositionUiSync(const QString &detectedLabel)
{
    if (!mSequenceDisplay)
        return;

    if (mIsPaused)
        return;

    const QString normalizedLabel = detectedLabel.trimmed().toUpper();

    const int detectedIndex = sequenceIndexFromDetectedPosition(detectedLabel);
    if (detectedIndex < 0) {
        mDetectedStableCandidateIndex = -1;
        mDetectedStableTimer = QElapsedTimer();
        if (mDetectedPosition != normalizedLabel) {
            mDetectedPosition = normalizedLabel;
            emit detectedPositionChanged();
        }
        return;
    }

    if (mDetectedStableCandidateIndex != detectedIndex) {
        mDetectedStableCandidateIndex = detectedIndex;
        mDetectedStableTimer.restart();
        return;
    }

    if (!mDetectedStableTimer.isValid() || mDetectedStableTimer.elapsed() < kDetectedPositionHoldMs)
        return;

    // Treat as a real position change only when the stable candidate differs
    // from the last confirmed position.
    if (mDetectedConfirmedIndex == detectedIndex) {
        if (mDetectedPosition != normalizedLabel) {
            mDetectedPosition = normalizedLabel;
            emit detectedPositionChanged();
        }
        return;
    }

    mDetectedConfirmedIndex = detectedIndex;

    if (mDetectedPosition != normalizedLabel) {
        mDetectedPosition = normalizedLabel;
        emit detectedPositionChanged();
    }

    // Apply only after the same detected position is held for >= 3 seconds.
    qInfo().noquote() << QStringLiteral("[pos-sync] source=vision confirmedLabel=%1 confirmedIndex=%2")
                             .arg(normalizedLabel)
                             .arg(detectedIndex);
    mSequenceDisplay->setCurrentPositionByIndex(detectedIndex);

    if (mActivePositionDialog && mActivePositionDialog->isVisible()) {
        mActivePositionDialog->accept();
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    QDir temp;
    mCurrentDir = QDir::current();
    temp=mCurrentDir;
    temp.cd("../../TimeGrapherTestFilesWeishiMic");
    if (temp.exists()) mCurrentDir=temp;
    mCurrentSamplesPerSecond=48000;
    mLiftAngle=52;

    ui->setupUi(this);
    this->setWindowTitle("TimeGrapher");
    statusBar()->hide();

    // Hide legacy Results label and setup styled ReadoutBar
    ui->Results->hide();
    mReadoutBar = new ReadoutBar(ui->CentralWidget);
    mReadoutBar->show();

    // Reposition GraphicsTabWidget to make room for ReadoutBar

    // Load static lists for models
    LoadBPH();
    LoadSimBPH();
    LoadAverageingPeriod();
    LoadAudioDevices();

    // [측정 대기] Warm-up delay 옵션 리스트 (0 = Off)
    mWarmupDelayList << "0s" << "5s" << "10s" << "15s" << "20s";

    {
        QSettings settings;
        mWatchId = settings.value(QStringLiteral("watchId"), QStringLiteral("rolex_123")).toString();
        mEngineer = settings.value(QStringLiteral("engineer"), QStringLiteral("Taehoon")).toString();
    }

    // ----------------------------------------------------
    // QML Control Panel Embedding (QQuickWidget)
    // ----------------------------------------------------
    mControlPanelQuickWidget = new QQuickWidget(this);
    mControlPanelQuickWidget->rootContext()->setContextProperty("cppBackend", this);
    mControlPanelQuickWidget->setSource(QUrl(QStringLiteral("qrc:/qml/src/ui/ControlPanel.qml")));
    mControlPanelQuickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);

    mPositionTiming = new PositionTimingModel(this);
    mPositionSequence = new PositionSequenceController(this);
    mPositionSequence->setTimingModel(mPositionTiming);
    connect(mPositionSequence, &PositionSequenceController::phaseChanged,
            this, &MainWindow::onPositionPhaseChanged);
    connect(mPositionSequence, &PositionSequenceController::measurementWindowEnded,
            this, &MainWindow::onPositionMeasurementEnded);
    connect(mPositionSequence, &PositionSequenceController::warmupRequested,
            this, &MainWindow::onSequenceWarmupRequested);
    connect(mPositionSequence, &PositionSequenceController::currentPositionIndexChanged,
            this, [this](int idx) {
        qInfo().noquote() << QStringLiteral("[pos-sync] source=sequence targetIndex=%1")
                                 .arg(idx);
        qInfo().noquote() << QStringLiteral("[pos-sync] source=sequence dropdownOverrideIgnored=true");
    });

    QVBoxLayout *cpLayout = new QVBoxLayout(ui->ControlPanelPlaceholder);
    cpLayout->setContentsMargins(0, 0, 0, 0);
    cpLayout->addWidget(mControlPanelQuickWidget);

    // Initial geometry sync: use the whole available central area (no fixed tab height).
    onControlPanelToggled(mControlPanelCollapsed);

    DisplayResults();

#if PERF_ENABLE
    new UiResponsivenessSampler(this);
#endif

    RegisterDisplayTabs();

    mCapture = new CaptureController(&mEngine, mTabManager, this);
    connect(mCapture, &CaptureController::statusMessage,  this, [this](const QString &m){ statusBar()->showMessage(m); });
    connect(mCapture, &CaptureController::measurementReady, this, &MainWindow::DisplayResults);
    connect(mCapture, &CaptureController::watchdogEvent, this, &MainWindow::onWatchdogEvent);
    // [워치독] 이벤트 → EventHandler(severity 별 상태바/모달). 워커 스레드에서 큐드 전달.
    mEventHandler = new EventHandler(this, this);
    connect(mCapture, &CaptureController::watchdogEvent, mEventHandler, &EventHandler::onEvent);
    connect(mCapture, &CaptureController::playbackDoneReadingFile, this, &MainWindow::HandlePlaybackDoneReadingFile);
    connect(mCapture, &CaptureController::simDone,                 this, &MainWindow::HandleSimDone);
    
    mCapture->setUseConset(mUseConset);

#if PERF_ENABLE
    if (mRateScope) connect(mRateScope, &TabRateScope::scopeReplotted, mCapture, &CaptureController::onScopeReplotted);
#endif

#ifdef ENABLE_VISION
    // [vision] 웹캠 watch-position 추론을 전용 스레드에서 병렬 실행(초당 1회 → 결과 print).
    //  기존 오디오 워커와 동일한 QObject+QThread 패턴. UI/측정 핫패스와 독립.
    mVisionThread = new QThread(this);
    mVisionWorker = new vision::VisionWorker();
    mVisionWorker->moveToThread(mVisionThread);
    // [vision · 워치독] 카메라 liveness 를 워치독 공유상태에 publish → USB 카메라 분리 시 모달 알림
    //  (오디오 장치 분리와 동일 동작). 워커 스레드 이동 직후·start 전에 주입.
    if (mCapture) mVisionWorker->setWatchdogState(mCapture->watchdogState());
    connect(mVisionThread, &QThread::started,  mVisionWorker, &vision::VisionWorker::start);
    connect(mVisionThread, &QThread::finished, mVisionWorker, &QObject::deleteLater);

    // [vision · UI] release 빌드에서 시계 방향을 눈으로 바로 확인하기 위한 최소 표시.
    //  상태바 우측에 permanent QLabel 하나만 추가(캡처 statusMessage 영역과 겹치지 않음).
    //  결과 시그널(resultReady)은 워커 스레드 → 큐 연결로 메인 스레드에서 안전하게 갱신.
    auto *visionLabel = new QLabel(QStringLiteral("watch: --"), this);
    statusBar()->addPermanentWidget(visionLabel);
    connect(mVisionWorker, &vision::VisionWorker::resultReady, this,
            [this, visionLabel](const QString &label, float conf) {
                visionLabel->setText(QStringLiteral("watch: %1 (%2%)")
                                         .arg(label)
                                         .arg(QString::number(conf * 100.0f, 'f', 0)));
                updateDetectedPositionUiSync(label);
            });

    // [vision · UI] 카메라 분리 시 watch-position 인식이 멈추므로 아이콘을 비활성(Camera Disconnected)으로
    //  되돌린다. 재연결 시에는 추론이 재개되어 다음 결과가 도착하면 아이콘이 다시 활성화된다.
    connect(mVisionWorker, &vision::VisionWorker::cameraAvailabilityChanged, this,
            [this, visionLabel](bool available) {
                if (!available) {
                    visionLabel->setText(QStringLiteral("watch: --"));
                    updateDetectedPositionUiSync(QString());
                }
            });

    mVisionThread->start();
#endif

#ifdef ENABLE_DIAG
    // 결과/에러는 워커 스레드 → 큐 연결로 메인 스레드에서 처리.
    //  결과: 화면 우측 상단 배너로 알림 → 배너 클릭 시 가운데 상세 가이드 창.
    mDiagBanner = new DiagBanner(ui->GraphicsTabWidget);
    connect(mDiagBanner, &DiagBanner::clicked, this, [this]() {
        if (mLastDiagKey.isEmpty()) return;
        DiagDetailDialog dlg(mLastDiagKey, mLastDiagConf, this);
        dlg.exec();
    });

    // [diag] Stop 시 t1/t3(rateTicY/rateTocY) 로 고장유형을 진단하는 전용 스레드.
    //  슬라이딩 윈도우 보팅 추론을 워커 스레드에서 수행 → UI/측정 핫패스 비차단.
    //  큐 연결로 QVector<double> 를 넘기므로 메타타입 등록이 필요하다.
    qRegisterMetaType<QVector<double>>("QVector<double>");
    mDiagThread = new QThread(this);
    mDiagWorker = new diag::DiagWorker();
    mDiagWorker->moveToThread(mDiagThread);
    connect(mDiagThread, &QThread::started,  mDiagWorker, &diag::DiagWorker::init);
    connect(mDiagThread, &QThread::finished, mDiagWorker, &QObject::deleteLater);

    connect(mDiagWorker, &diag::DiagWorker::resultReady, this,
            [this](const QString &label, float conf, int windows) {
                Q_UNUSED(windows);
                mLastDiagKey  = label;
                mLastDiagConf = conf;
                const diagui::DiagEntry *e = diagui::lookup(label);
                const QString title   = e ? e->title   : label;
                const bool    healthy = e ? e->healthy : false;
                // 정상(healthy)일 때는 배너를 표시하지 않는다.
                if (mDiagBanner && !healthy) mDiagBanner->showResult(title, healthy, conf);
            });
    connect(mDiagWorker, &diag::DiagWorker::error, this,
            [this](const QString &message) { statusBar()->showMessage(message); });

    mDiagThread->start();
#endif
}

MainWindow::~MainWindow()
{
#ifdef ENABLE_VISION
    if (mVisionThread) {
        // 워커 stop() 을 워커 스레드에서 동기 실행 후 스레드 종료(카메라/타이머 정리).
        if (mVisionWorker)
            QMetaObject::invokeMethod(mVisionWorker, "stop", Qt::BlockingQueuedConnection);
        mVisionThread->quit();
        mVisionThread->wait();
    }
#endif
#ifdef ENABLE_DIAG
    if (mDiagThread) {
        mDiagThread->quit();
        mDiagThread->wait();
    }
#endif
    delete ui;
}

void MainWindow::RegisterDisplayTabs(void)
{
    mTabManager = new TabManager(ui->GraphicsTabWidget, this);
    mTabManager->addWaveSink(&mWaveHistory);

    mSequenceDisplay = new TabSequenceDisplay(this);
    mSequenceDisplay->setTimingModel(mPositionTiming);
    mSequenceDisplay->setWatchIdProvider([this]() { return watchId(); });
    mSequenceDisplay->setEngineerProvider([this]() { return engineer(); });
    connect(this, &MainWindow::isRunningChanged, mSequenceDisplay, [this]() {
        mSequenceDisplay->onRunningStateChanged(this->isRunning());
    });
    connect(mSequenceDisplay, &TabSequenceDisplay::allPositionsMeasured,
            this, &MainWindow::onAllPositionsMeasured);
    mTabManager->registerTab(mSequenceDisplay);

    auto *ltpTab = new TabLongTermPerformance(this);
    connect(ltpTab, &TabLongTermPerformance::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(ltpTab, &TabLongTermPerformance::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(ltpTab);

    mRateScope = new TabRateScope(this);
    mRateScope->setHistory(&mWaveHistory);
    connect(mRateScope, &TabRateScope::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(mRateScope, &TabRateScope::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(mRateScope);

    mTabManager->registerTab(new TabSoundPrint(this));

    auto *traceTab = new TabTraceDisplay(this);
    connect(traceTab, &TabTraceDisplay::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(traceTab, &TabTraceDisplay::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(traceTab);

    mTabManager->registerTab(new TabVarioStability(this));

    auto *bnsTab = new TabBeatNoiseScope(this);
    bnsTab->setHistory(&mWaveHistory);
    connect(bnsTab, &TabBeatNoiseScope::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(bnsTab, &TabBeatNoiseScope::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(bnsTab);

    auto *bedTab = new TabBeatErrorTrace(this);
    mBedTab = bedTab;
    connect(bedTab, &TabBeatErrorTrace::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(bedTab, &TabBeatErrorTrace::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(bedTab);

    auto *escTab = new TabEscapementAnalyzer(this);
    escTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(escTab);

    auto *specTab = new TabSpectrogram(this);
    specTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(specTab);

    auto *wcmpTab = new TabWaveformCompare(this);
    wcmpTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(wcmpTab);

    auto *sweepTab = new TabSyncSweepScope(this);
    sweepTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(sweepTab);

    auto *filterTab = new TabFilterViews(this);
    filterTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(filterTab);

    // Apply touch-friendly stylesheet and use custom corner navigation buttons.
    ui->GraphicsTabWidget->setUsesScrollButtons(false);
    ui->GraphicsTabWidget->setElideMode(Qt::ElideNone);
    ui->GraphicsTabWidget->setStyleSheet(QStringLiteral(
        "QTabWidget::pane {"
        "    border: 1px solid #2e2e3a;"
        "    background: #18181f;"
        "}"
        "QTabBar::tab {"
        "    background: #252530;"
        "    color: #b0b0b0;"
        "    border: 1px solid #2e2e3a;"
        "    border-bottom: none;"
        "    border-top-left-radius: 6px;"
        "    border-top-right-radius: 6px;"
        "    padding: 10px 18px;"
        "    font-size: 12px;"
        "    font-weight: bold;"
        "    min-width: 80px;"
        "}"
        "QTabBar::tab:selected {"
        "    background: #ab47bc;"
        "    color: #ffffff;"
        "    border-color: #ab47bc;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "    background: #323242;"
        "    color: #ffffff;"
        "}"
        "QTabWidget QToolButton {"
        "    background-color: #252530;"
        "    border: 1px solid #2e2e3a;"
        "    border-radius: 6px;"
        "    width: 32px;"
        "    height: 36px;"
        "    color: #ffffff;"
        "    font-weight: bold;"
        "}"
        "QTabWidget QToolButton:hover {"
        "    background-color: #ab47bc;"
        "}"
    ));

    auto *tabBar = ui->GraphicsTabWidget->tabBar();
    auto shiftTab = [this, tabBar](int step) {
        if (!tabBar) return;
        const int count = tabBar->count();
        if (count <= 0) return;
        const int next = qBound(0, tabBar->currentIndex() + step, count - 1);
        if (next == tabBar->currentIndex()) return;
        tabBar->setCurrentIndex(next);
    };

    QWidget *leftCorner = new QWidget(this);
    auto *ll = new QHBoxLayout(leftCorner); ll->setContentsMargins(6, 0, 0, 0); ll->setSpacing(0);
    auto *leftBtn = new QToolButton(leftCorner);
    leftBtn->setText(QStringLiteral("◀"));
    leftBtn->setToolTip(QStringLiteral("Move tabs left"));
    ll->addWidget(leftBtn);
    ui->GraphicsTabWidget->setCornerWidget(leftCorner, Qt::TopLeftCorner);

    QWidget *rightCorner = new QWidget(this);
    auto *cl = new QHBoxLayout(rightCorner); cl->setContentsMargins(0, 0, 6, 0); cl->setSpacing(8);
    // 전역 seek 라벨 제거 — 선택 시각은 각 그래프의 롤리팝 툴팁이 표시(중복 방지). mSeekLabel 은
    //  nullptr 로 남으며 updateSeekLabel()/clear 는 가드되어 안전한 no-op.
    auto *rightBtn = new QToolButton(rightCorner);
    rightBtn->setText(QStringLiteral("▶"));
    rightBtn->setToolTip(QStringLiteral("Move tabs right"));
    cl->addWidget(rightBtn);
    ui->GraphicsTabWidget->setCornerWidget(rightCorner, Qt::TopRightCorner);

    connect(leftBtn, &QToolButton::clicked, this, [shiftTab]() { shiftTab(-1); });
    connect(rightBtn, &QToolButton::clicked, this, [shiftTab]() { shiftTab(1); });

    // [측정 대기] WarmupOverlay 생성 (탭 위에 오버레이)
    mWarmupOverlay = new WarmupOverlay(ui->GraphicsTabWidget);
    connect(mWarmupOverlay, &WarmupOverlay::warmupFinished,
            this, &MainWindow::onWarmupFinished);
}

void MainWindow::updateSeekLabel(double absSample)
{
    if (!mSeekLabel) return;
    const int sr = mWaveHistory.sampleRate();
    const double t = sr > 0 ? absSample / (double)sr : 0.0;
    mSeekLabel->setText(QString("viewing  t=%1 s   #%2").arg(t, 0, 'f', 1).arg((qint64)absSample));
}

// =========================================================================
// Q_PROPERTY Getters & Setters Implementation
// =========================================================================
int MainWindow::currentMode() const 
{ 
    return mCurrentMode; 
}

void MainWindow::setCurrentMode(int mode) 
{
    if (mCurrentMode == mode) return;
    mCurrentMode = mode;
    emit currentModeChanged();

    // 모드 변경 시 오디오 장치 및 레이트 재배치
    if (mCurrentMode != LIVE) {
        SetAudioDevice(PLAYBACK_OR_SIM_PCM);
    } else {
        if (mDeviceIndex >= 0 && mDeviceIndex < mDeviceList.size()) {
            SetAudioDevice(mDeviceList[mDeviceIndex]);
        }
    }
}

double MainWindow::gain() const 
{ 
    return mGain; 
}

void MainWindow::setGain(double newGain) 
{
    if (qFuzzyCompare(mGain, newGain)) return;
    mGain = newGain;
    emit gainChanged();
    if (mCapture) {
        mCapture->setInputVolume(mGain); // Live 가동 중 실시간 게인 튜닝 적용
    }
}

QStringList MainWindow::deviceList() const 
{ 
    return mDeviceList; 
}

int MainWindow::deviceIndex() const 
{ 
    return mDeviceIndex; 
}

void MainWindow::setDeviceIndex(int idx) 
{
    if (mDeviceIndex == idx || idx < 0 || idx >= mAudioInputDevices.size()) return;
    mDeviceIndex = idx;
    emit deviceIndexChanged();

    PopulateSampleRates(mAudioInputDevices[mDeviceIndex]);
}

QStringList MainWindow::sampleRateList() const 
{ 
    return mSampleRateList; 
}

int MainWindow::sampleRateIndex() const 
{ 
    return mSampleRateIndex; 
}

void MainWindow::setSampleRateIndex(int idx) 
{
    if (mSampleRateIndex == idx || idx < 0 || idx >= mSampleRateList.size()) return;
    mSampleRateIndex = idx;
    emit sampleRateIndexChanged();

    mCurrentSamplesPerSecond = mSampleRateList[mSampleRateIndex].split(" ").first().toInt();
}

int MainWindow::averagingPeriodIndex() const 
{ 
    return mAveragingPeriodIndex; 
}

void MainWindow::setAveragingPeriodIndex(int idx) 
{
    if (mAveragingPeriodIndex == idx || idx < 0 || idx >= mAveragingPeriodList.size()) return;
    mAveragingPeriodIndex = idx;
    emit averagingPeriodIndexChanged();

    mAveragingPeriod = AveragingPeriodList[mAveragingPeriodIndex];
    if (mCapture) {
        mCapture->setEngineParams(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    }
}

void MainWindow::setWarmupDelayIndex(int idx)
{
    if (mWarmupDelayIndex == idx || idx < 0 || idx >= mWarmupDelayList.size()) return;
    mWarmupDelayIndex = idx;
    emit warmupDelayIndexChanged();
}

QString MainWindow::selectedWavFile() const 
{ 
    return mPlaybackFileName.isEmpty() ? "" : QFileInfo(mPlaybackFileName).fileName(); 
}

bool MainWindow::isRunning() const 
{ 
    return mIsRunning; 
}

bool MainWindow::isPaused() const
{
    return mIsPaused;
}

bool MainWindow::inWarmup() const
{
    return mInWarmup;
}

QString MainWindow::detectedPosition() const
{
    return mDetectedPosition;
}

bool MainWindow::recordSessionEnabled() const 
{ 
    return mRecordSessionEnabled; 
}

void MainWindow::setRecordSessionEnabled(bool enabled) 
{
    if (mRecordSessionEnabled == enabled) return;
    mRecordSessionEnabled = enabled;
    emit recordSessionEnabledChanged();
}

int MainWindow::detectorBphIndex() const 
{ 
    return mDetectorBphIndex; 
}

void MainWindow::setDetectorBphIndex(int idx) 
{
    if (mDetectorBphIndex == idx || idx < 0 || idx >= mBphList.size()) return;
    mDetectorBphIndex = idx;
    emit detectorBphIndexChanged();
}

int MainWindow::liftAngle() const 
{ 
    return (int)mLiftAngle; 
}

void MainWindow::setLiftAngle(int angle) 
{
    if ((int)mLiftAngle == angle) return;
    mLiftAngle = angle;
    emit liftAngleChanged();
    if (mCapture) {
        mCapture->setEngineParams(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    }
}

int MainWindow::simBphIndex() const 
{ 
    return mSimBphIndex; 
}

void MainWindow::setSimBphIndex(int idx) 
{
    if (mSimBphIndex == idx || idx < 0 || idx >= mSimBphList.size()) return;
    mSimBphIndex = idx;
    emit simBphIndexChanged();
    SyncDetectorBphToSimBph();
}

int MainWindow::simErrorRate() const 
{ 
    return mSimErrorRate; 
}

void MainWindow::setSimErrorRate(int val) 
{
    if (mSimErrorRate == val) return;
    mSimErrorRate = val;
    emit simErrorRateChanged();
}

int MainWindow::simAmplitude() const 
{ 
    return mSimAmplitude; 
}

void MainWindow::setSimAmplitude(int val) 
{
    if (mSimAmplitude == val) return;
    mSimAmplitude = val;
    emit simAmplitudeChanged();
}

double MainWindow::simBeatError() const 
{ 
    return mSimBeatError; 
}

void MainWindow::setSimBeatError(double val) 
{
    if (qFuzzyCompare(mSimBeatError, val)) return;
    mSimBeatError = val;
    emit simBeatErrorChanged();
}

bool MainWindow::simRealistic() const 
{ 
    return mSimRealistic; 
}

void MainWindow::setSimRealistic(bool val) 
{
    if (mSimRealistic == val) return;
    mSimRealistic = val;
    emit simRealisticChanged();
}

int MainWindow::highPassCutoff() const 
{ 
    return mHighPassCutoff; 
}

void MainWindow::setHighPassCutoff(int val) 
{
    if (mHighPassCutoff == val) return;
    mHighPassCutoff = val;
    emit highPassCutoffChanged();
}

bool MainWindow::useConset() const 
{ 
    return mUseConset; 
}

void MainWindow::setUseConset(bool val) 
{
    if (mUseConset == val) return;
    mUseConset = val;
    emit useConsetChanged();
    if (mCapture) {
        mCapture->setUseConset(mUseConset);
    }
}

QString MainWindow::watchId() const
{
    return mWatchId;
}

void MainWindow::setWatchId(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (mWatchId == trimmed) return;
    mWatchId = trimmed;
    QSettings settings;
    settings.setValue(QStringLiteral("watchId"), mWatchId);
    emit watchIdChanged();
}

QString MainWindow::engineer() const
{
    return mEngineer;
}

void MainWindow::setEngineer(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (mEngineer == trimmed) return;
    mEngineer = trimmed;
    QSettings settings;
    settings.setValue(QStringLiteral("engineer"), mEngineer);
    emit engineerChanged();
}

// =========================================================================
// QML Invokable Operations Implementation
// =========================================================================
void MainWindow::startSession()
{
    if (mCurrentMode == LIVE) {
        ConfigureSoundCard();
        LiveStart();
    } else if (mCurrentMode == PLAYBACK) {
        PlaybackStart();
    } else if (mCurrentMode == SIM) {
        SimStart();
    }
}

void MainWindow::stopSession()
{
    if (mCapture) {
        if (mCurrentMode == LIVE) {
            mCapture->stopLive();
        } else if (mCurrentMode == PLAYBACK) {
            mCapture->stopPlayback();
        } else if (mCurrentMode == SIM) {
            mCapture->stopSim();
        }
    }
    finishSession(true);
}

void MainWindow::togglePauseSession()
{
    if (!mIsRunning || mInWarmup)
        return;

    if (mIsPaused) {
        if (mCapture)    mCapture->setPaused(false);
        if (mTabManager) mTabManager->setPaused(false);
        if (mRateScope)  mRateScope->setPaused(false);
        if (mPositionSequence)
            mPositionSequence->resume();

        mIsPaused = false;
        emit isPausedChanged();

        if (mLastPhaseLabel.isEmpty() || mLastPhaseLabel == QStringLiteral("idle"))
            statusBar()->showMessage(QStringLiteral("Running"));
        else {
            const QString phaseText = (mLastPhaseLabel == QStringLiteral("stabilizing"))
                ? QStringLiteral("Stabilizing") : QStringLiteral("Measuring");
            statusBar()->showMessage(QStringLiteral("%1 %2 — %3 s remaining")
                .arg(phaseText, mLastPhaseName)
                .arg(mLastPhaseRemainingSec));
        }
    } else {
        if (mCapture)    mCapture->setPaused(true);
        if (mTabManager) mTabManager->setPaused(true);
        if (mRateScope)  mRateScope->setPaused(true);
        if (mPositionSequence)
            mPositionSequence->pause();

        mIsPaused = true;
        emit isPausedChanged();
        updatePauseStatusMessage();
    }
}

void MainWindow::clearPauseState(void)
{
    if (mCapture)    mCapture->setPaused(false);
    if (mTabManager) mTabManager->setPaused(false);
    if (mRateScope)  mRateScope->setPaused(false);

    if (mIsPaused) {
        mIsPaused = false;
        emit isPausedChanged();
    }
}

void MainWindow::updatePauseStatusMessage(void)
{
    if (!mIsPaused) {
        if (mLastPhaseLabel.isEmpty() || mLastPhaseLabel == QStringLiteral("idle"))
            statusBar()->showMessage(QStringLiteral("Running"));
        return;
    }

    if (mLastPhaseLabel.isEmpty() || mLastPhaseLabel == QStringLiteral("idle")) {
        statusBar()->showMessage(QStringLiteral("Paused"));
        return;
    }

    const QString phaseText = (mLastPhaseLabel == QStringLiteral("stabilizing"))
        ? QStringLiteral("Stabilizing") : QStringLiteral("Measuring");
    statusBar()->showMessage(QStringLiteral("Paused — %1 %2 — %3 s remaining")
        .arg(phaseText, mLastPhaseName)
        .arg(mLastPhaseRemainingSec));
}

void MainWindow::finishSession(bool runDiag)
{
    if (!mIsRunning)
        return;

    cancelWarmup();
    if (mPositionSequence)
        mPositionSequence->stop();
    if (mSequenceDisplay)
        mSequenceDisplay->setPhaseStatus(QString(), 0);

    SetGuiStopMode();

    AudioCloseCheck();
    if (mCurrentMode == PLAYBACK || mCurrentMode == SIM) {
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }

    statusBar()->showMessage("Stopped");

    if (runDiag)
        triggerDiagnosis();
}

void MainWindow::triggerDiagnosis()
{
#ifdef ENABLE_DIAG
    if (mCapture)
        mCapture->flushDetector();
    if (mDiagWorker) {
        const QVector<double> t1 = mEngine.ticY();
        const QVector<double> t3 = mEngine.tocY();
        QMetaObject::invokeMethod(mDiagWorker, "runDiagnosis", Qt::QueuedConnection,
                                  Q_ARG(QVector<double>, t1),
                                  Q_ARG(QVector<double>, t3));
    }
#endif
}

// =========================================================================
// [측정 대기] Warm-up Delay 로직
// =========================================================================
static const int kWarmupSecs[] = {0, 5, 10, 15, 20};

void MainWindow::onSequenceWarmupRequested(bool firstPosition)
{
    // [측정 대기] 포지션이 활성화되면 측정 전에 warm-up(자세 안정 대기)을 수행한다.
    //  - 첫 포지션(세션 시작): 기존처럼 어느 탭에서든 전역 warm-up.
    //  - 포지션 전환: 다른 그래프 탭에 영향을 주지 않도록 시퀀스 탭을 볼 때만 warm-up.
    //  warm-up 시간이 0이거나 대상이 아니면 곧바로 측정을 시작한다.
    const int delay = kWarmupSecs[mWarmupDelayIndex];
    const bool onSequenceTab = ui && ui->GraphicsTabWidget && mSequenceDisplay
        && ui->GraphicsTabWidget->currentWidget() == mSequenceDisplay;

    const bool doWarmup = (delay > 0) && (firstPosition || onSequenceTab);

    if (doWarmup) {
        mWarmupIsPositionChange = !firstPosition;   // 종료 처리에서 시퀀스 표 보존 여부 결정
        startWarmup(delay);                          // 종료 시 onWarmupFinished 가 측정 시작
        return;
    }

    // warm-up 없이 즉시 측정 시작.
    if (mPositionSequence)
        mPositionSequence->beginMeasuringNow();
    if (firstPosition)
        statusBar()->showMessage("Running");
}

void MainWindow::startWarmup(int seconds)
{
    mInWarmup = true;
    emit inWarmupChanged();
    if (mTabManager) mTabManager->setWarmup(true);
    mWarmupOverlay->startCountdown(seconds);
    statusBar()->showMessage("Warming up...");
}

void MainWindow::cancelWarmup()
{
    if (!mInWarmup) return;
    mInWarmup = false;
    emit inWarmupChanged();
    if (mWarmupOverlay) mWarmupOverlay->cancel();
    if (mTabManager) mTabManager->setWarmup(false);
    mWarmupIsPositionChange = false;
}

void MainWindow::onWarmupFinished()
{
    mInWarmup = false;
    emit inWarmupChanged();
    // [측정 대기] 워밍업 종료 시점을 rate 그래프 x축 원점(0)으로 → 워밍업 시간 갭 제거
    if (mCapture && mCurrentSamplesPerSecond > 0) {
        const double originSec = (double)mCapture->totalSamples() / (double)mCurrentSamplesPerSecond;
        mEngine.setPlotTimeOrigin(originSec);
    }
    // [측정 대기] 워밍업 동안 엔진은 이미 신호를 처리해 RLS·롤링평균이 수렴된 상태다.
    //  그 수렴 상태를 버리지 않고(=reset() 금지), 화면에 그려질 플롯 시리즈만 비운다.
    //  → 종료 직후 첫 점부터 안정된 값이 그려져 long-term 그래프가 튀지 않는다.
    mEngine.clearPlotsKeepState();
    if (mTabManager) {
        mTabManager->setWarmup(false);    // 게이트 해제 (이후에 broadcastReset이 탭에 도달함)
        // [측정 대기] 포지션 전환 warm-up 은 시퀀스 누적표(포지션별 측정값)를 보존한다.
        //  세션 시작 warm-up 은 표가 비어 있으므로 기존대로 전체 리셋.
        if (mWarmupIsPositionChange)
            mTabManager->broadcastResetExcept(mSequenceDisplay);
        else
            mTabManager->broadcastReset();
    }
    mWaveHistory.clear();                 // 스크롤백 이력 취소
    DisplayResults();                     // 수렴된 readout/그래프를 즉시 1회 반영
    statusBar()->showMessage("Running");
    mWarmupIsPositionChange = false;
    // [측정 대기] warm-up 종료 → 실제 측정 카운트다운 시작.
    if (mPositionSequence)
        mPositionSequence->beginMeasuringNow();
}

void MainWindow::refreshDevices()
{
    LoadAudioDevices();
}

// =========================================================================
// Back-end Engine Control Procedures
// =========================================================================
void MainWindow::ConfigureSoundCard(void)
{
#if defined(Q_OS_LINUX)
    LinuxSetSoundParameters(LINUX_SOUND_CARD_NAME,LINUX_SOUND_MIC_NAME,LINUX_SOUND_AGC_NAME,LINUX_SOUND_MIC_PERCENT_VOLUME);
#elif defined(Q_OS_WIN)
    WindowsSetSoundParameters(WINDOWS_SOUND_ENDPOINT_NAME,WINDOWS_SOUND_MIC_NAME,WINDOWS_SOUND_MIC_PERCENT_VOLUME);
#endif
}

void MainWindow::EventsReset(void)
{
    mReadoutFrozen = false;   // [MPS 포지션 전환] 세션 시작/정지 시 요약바 고정 해제
    mEngine.reset();
    DisplayResults();
}

void MainWindow::LoadAudioDevices(void)
{
    mAudioInputDevices = QMediaDevices::audioInputs();
    mDeviceList.clear();

    int RenameLen = sizeof RenameAudioDevices / sizeof RenameAudioDevices[0];
    for (const QAudioDevice &d : mAudioInputDevices)
    {
        QString Description = d.description();
        for (int i = 0; i < RenameLen; i++)
        {
            if (Description.contains(RenameAudioDevices[i][0], Qt::CaseSensitive))
            {
                Description = RenameAudioDevices[i][1];
                break;
            }
        }
        mDeviceList.append(Description);
    }
    mDeviceList.append(PLAYBACK_OR_SIM_PCM);
    emit deviceListChanged();

    // 기본 선호 입력 장치 자동 선택
    int selectedIdx = 0;
    int prefLen = std::size(PreferredAudioDevices);
    for (int i = 0; i < prefLen; i++)
    {
        int index = mDeviceList.indexOf(QRegularExpression(PreferredAudioDevices[i]));
        if (index != -1) {
            selectedIdx = index;
            break;
        }
    }
    setDeviceIndex(selectedIdx);
}

void MainWindow::LoadAverageingPeriod(void)
{
    mAveragingPeriodList.clear();
    auto length = std::size(AveragingPeriodList);
    for (size_t i = 0; i < length; i++)
    {
        mAveragingPeriodList.append(QString::asprintf("%ds", AveragingPeriodList[i]));
    }
    setAveragingPeriodIndex(4); // 20 Seconds 기본
}

void MainWindow::LoadBPH(void)
{
    mBphList.clear();
    auto length = std::size(ManualAutoBPH);
    for (size_t i = 0; i < length; i++)
    {
        if (ManualAutoBPH[i] != 0) mBphList.append(QString::number(ManualAutoBPH[i]));
        else mBphList.append("Auto BPH");
    }
    setDetectorBphIndex(0);
}

void MainWindow::LoadSimBPH(void)
{
    mSimBphList.clear();
    auto length = std::size(SimBPH);
    for (size_t i = 0; i < length; i++)
    {
        mSimBphList.append(QString::number(SimBPH[i]));
    }
    setSimBphIndex(52); // 21600 bph 기본
}

void MainWindow::PopulateSampleRates(const QAudioDevice &device)
{
    QList<int> standardRates = {48000, 96000, 192000, 384000};
    mSampleRateList.clear();
    mNumberofRates = 0;

    if (device.isNull()) {
        for (int rate : standardRates) {
            mSampleRateList.append(QString::number(rate) + " Hz");
            mAvalableRates[mNumberofRates] = rate;
            mNumberofRates++;
        }
    } else {
        for (int rate : standardRates) {
            QAudioFormat format;
            format.setSampleRate(rate);
            format.setChannelCount(CHANNELS);
            format.setSampleFormat(SAMPLE_FORMAT);

            if (device.isFormatSupported(format)) {
                mSampleRateList.append(QString::number(rate) + " Hz");
                mAvalableRates[mNumberofRates] = rate;
                mNumberofRates++;
            }
        }
    }
    emit sampleRateListChanged();
    setSampleRateIndex(0); // 48000 Hz 기본
}

bool MainWindow::SetAudioRate(int Rate)
{
    int index = -1;
    for (int i = 0; i < mSampleRateList.size(); ++i) {
        if (mSampleRateList[i].startsWith(QString::number(Rate))) {
            index = i;
            break;
        }
    }
    if (index != -1) {
        setSampleRateIndex(index);
        return true;
    }
    return false;
}

bool MainWindow::SetAudioDevice(QString Name)
{
    int index = mDeviceList.indexOf(Name);
    if (index != -1) {
        setDeviceIndex(index);
        return true;
    }
    return false;
}

void MainWindow::GetAudioRate(int &Rate)
{
    Rate = mCurrentSamplesPerSecond;
}

void MainWindow::GetAudioDevice(QString &Name)
{
    if (mDeviceIndex >= 0 && mDeviceIndex < mDeviceList.size()) {
        Name = mDeviceList[mDeviceIndex];
    } else {
        Name = PLAYBACK_OR_SIM_PCM;
    }
}

void MainWindow::pushCaptureConfig(void)
{
    mCapture->setEngineParams(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    if (mCurrentMode == SIM) SyncDetectorBphToSimBph();
    bool bphAuto   = (mCurrentMode != SIM && mDetectorBphIndex == 0);
    int  manualBph = mCurrentMode == SIM ? mSimBphList[mSimBphIndex].toInt()
                                          : (bphAuto ? 0 : mBphList[mDetectorBphIndex].toInt());
    mCapture->setDetectorConfig(bphAuto, manualBph, (double)mHighPassCutoff);
    mCapture->setUseConset(mUseConset);
    mCapture->setWavWriter(mWavWriter);
}

void MainWindow::LiveStart(void)
{
    if (mDeviceIndex < 0 || mDeviceIndex >= mAudioInputDevices.size()) {
        QMessageBox::warning(this, "No Audio Input Device",
            "No audio input device is available.\n\n"
            "Please connect the USB microphone and try again.");
        return;
    }
    if (mRecordSessionEnabled && !RecordSessionCheck()) return;
    Reset();
    pushCaptureConfig();
    QAudioDevice dev = mAudioInputDevices[mDeviceIndex];
    mCapture->startLive(dev, mCurrentSamplesPerSecond, mGain);
    SetGuiRunMode();
    if (mPositionSequence)
        mPositionSequence->start();   // [측정 대기] 첫 포지션 warm-up 은 warmupRequested 로 자동 시작
}

void MainWindow::PlaybackStart(void)
{
    if (mPlaybackFileName.isEmpty() && !choosePlaybackFile()) return;
    if (mRecordSessionEnabled && !RecordSessionCheck()) return;
    if (!OpenFile(mPlaybackFileName)) return;
    Reset();
    pushCaptureConfig();
    mCapture->startPlayback(mPlaybackFileName, mCurrentSamplesPerSecond);
    SetGuiRunMode();
    if (mPositionSequence)
        mPositionSequence->start();   // [측정 대기] 첫 포지션 warm-up 은 warmupRequested 로 자동 시작
}

void MainWindow::SimStart(void)
{
    SyncDetectorBphToSimBph();
    SimConfigParams p;
    p.realistic          = mSimRealistic;
    p.bph                = mSimBphList[mSimBphIndex].toInt();
    p.sampleRateHz       = mCurrentSamplesPerSecond;
    p.beatErrorMs        = mSimBeatError;
    p.watchAmplitudeDeg  = mSimAmplitude;
    p.liftAngleDeg       = (int)mLiftAngle;
    p.rateErrorSecPerDay = mSimErrorRate;
    WatchSynthStreamConfig cfg = SimConfigBuilder::build(p);

    if (mRecordSessionEnabled && !RecordSessionCheck()) return;
    GetAudioRate(mRateBeforePlaybackOrSim);
    GetAudioDevice(mDeviceNameBeforePlaybackOrSim);
    if (!SetAudioDevice(PLAYBACK_OR_SIM_PCM)) qInfo()<< "SetAudioDevice Failed";
    if (!SetAudioRate(mRateBeforePlaybackOrSim)) qInfo()<< "SetAudioRate Failed";
    Reset();
    pushCaptureConfig();
    mCapture->startSim(cfg, mCurrentSamplesPerSecond);
    SetGuiRunMode();
    if (mPositionSequence)
        mPositionSequence->start();   // [측정 대기] 첫 포지션 warm-up 은 warmupRequested 로 자동 시작
}

void MainWindow::DisplayResults(void)
{
    if (mInWarmup) return;   // [측정 대기] 웜업 중에는 readout/그래프 갱신 차단
    if (mReadoutFrozen) return;   // [MPS 포지션 전환] 대기 구간 동안 요약바/탭 갱신 고정
    MeasurementEngine::Results res = mEngine.results();
    QString BeatsPerHour,RateError,BeatError,Amplitude, Results;
    if (res.bphValid) {
        BeatsPerHour= QString("%1").arg(res.bph, 5, 10, QChar(' '));
    } else BeatsPerHour="-----";

    if (res.rateValid) {
        RateError= QString::asprintf("%+6.1f", res.rateSecPerDay);
    } else RateError="------";
    
    if (res.beatErrorValid) {
        BeatError= QString("%1").arg(res.beatErrorMs, 4, 'f', 1);
    } else BeatError="----";
    
    if (res.amplitudeValid) {
        Amplitude=  QString("%1°").arg(qRound64(res.amplitudeDeg), 3, 10, QChar(' '));
    } else Amplitude="---";

    Results="RATE "+RateError+" s/d   AMPLITUDE "+Amplitude+"   BEAT ERROR "+BeatError+" ms   BEAT "+BeatsPerHour+" bph";
    ui->Results->setText(Results);

    PublishMeasurementToTabs();
}

void MainWindow::PublishMeasurementToTabs(void)
{
    if (!mTabManager) return;
    MeasurementEngine::Results res = mEngine.results();
    MeasurementSnapshot snap;
    snap.timeMs         = Perf::nowMs();
    snap.bphValid       = res.bphValid;
    snap.bph            = res.bph;
    snap.rateValid      = res.rateValid;
    snap.rate           = res.rateSecPerDay;
    snap.beatErrorValid = res.beatErrorValid;
    snap.beatErrorMs    = res.beatErrorMs;
    snap.amplitudeValid = res.amplitudeValid;
    snap.amplitudeDeg   = res.amplitudeDeg;
    snap.rateOutlier      = res.rateOutlier;       // [이상치] 평균엔 미반영, 표시 마킹용
    snap.beatErrorOutlier = res.beatErrorOutlier;
    snap.amplitudeOutlier = res.amplitudeOutlier;
    snap.synced         = res.bphValid;
    snap.sampleRateHz   = mCurrentSamplesPerSecond;
    snap.totalSamples   = mCapture ? mCapture->totalSamples() : 0;
    snap.liftAngle      = (int)mLiftAngle;
    snap.mode           = mCurrentMode;
    snap.rateTicX = mEngine.ticX().constData(); snap.rateTicY = mEngine.ticY().constData(); snap.rateTicN = mEngine.ticX().size();
    snap.rateTocX = mEngine.tocX().constData(); snap.rateTocY = mEngine.tocY().constData(); snap.rateTocN = mEngine.tocX().size();
    snap.rateTicOutY = mEngine.ticOutY().constData(); snap.rateTocOutY = mEngine.tocOutY().constData();   // [이상치] 점별 표식
    snap.rateMaxPoints = mEngine.maxDataPoints();
    if (mReadoutBar) mReadoutBar->update(snap);
    mTabManager->broadcastMeasurement(snap);
}

void MainWindow::Reset(void)
{
    qInfo()<<"RESET";
    if (mTabManager) mTabManager->broadcastReset();
    mWaveHistory.clear();
    clearPauseState();
    if (mSeekLabel)  mSeekLabel->clear();
    EventsReset();
}

void MainWindow::onWatchdogEvent(const WatchdogEvent &ev)
{
    // 오디오 장치가 측정 중 분리되면: 측정을 멈추고 사용 중인 버퍼/데이터를 전부 비워
    //  초기화 상태로 되돌린다. → 장치를 다시 연결하고 Start 를 누르면 곧바로 정상 동작한다.
    //  (분리 감지는 Live 모드에서만 발생하므로 stopLive 경로만 처리.)
    if (ev.id != WatchdogEventId::AudioDeviceLost)
        return;
    if (!mIsRunning)
        return;

    qInfo() << "[audio-disconnect] capture device lost — auto-stopping and resetting to clean state";

    if (mCapture)
        mCapture->stopLive();   // 오디오 워커 정지(녹음/캡처 중단)
    finishSession(false);       // GUI Stop 모드 전환(mIsRunning=false), 진단은 생략(데이터 불완전)
    Reset();                    // 엔진/탭/이력 버퍼 전부 클리어 → 초기화 상태
}

void MainWindow::HandlePlaybackDoneReadingFile()
{
    if (mCapture)
        mCapture->stopPlayback();
    finishSession(true);
}

void MainWindow::HandleSimDone()
{
    if (mCapture)
        mCapture->stopSim();
    finishSession(true);
}

bool MainWindow::RecordSessionCheck(void)
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Output File"),
                                                    "../../Output/",
                                                    tr("Wav Files (*.wav);;All Files (*)"));

    if (!fileName.isEmpty())
    {
        mWavWriter= new WavStreamWriter;
        if (!mWavWriter->open(fileName,mCurrentSamplesPerSecond,1))
        {
            QMessageBox::critical(this, "Error", "Failed to open WAV file");
            delete mWavWriter;
            mWavWriter=NULL;
            return(false);
        }
    }
    else return(false);
    return (true);
}

void MainWindow::AudioCloseCheck(void)
{
    if (mWavWriter)
    {
        if (mCapture) mCapture->setWavWriter(nullptr);
        mWavWriter->close();
        delete mWavWriter;
        mWavWriter=NULL;
    }
}

bool MainWindow::OpenFile(const QString &FileName)
{
    WavFileInfo info = WavFileReader::readHeader(FileName);
    if (!info.ok) {
        statusBar()->showMessage(tr("File %1 could not be opened").arg(QDir::toNativeSeparators(FileName)));
        return false;
    }
    mCurrentDir = QFileInfo(FileName).dir();

    GetAudioRate(mRateBeforePlaybackOrSim);
    GetAudioDevice(mDeviceNameBeforePlaybackOrSim);
    if (!SetAudioDevice(PLAYBACK_OR_SIM_PCM)) qInfo()<< "SetAudioDevice Failed";
    if (!SetAudioRate(info.header.sampleRate)) qInfo()<< "SetAudioRate Failed";

    if (!WavFileReader::isSupportedFormat(info.header) ||
        info.header.sampleRate != (uint32_t)mCurrentSamplesPerSecond)
    {
        statusBar()->showMessage(tr("File %1 Not a 48K, single channel 32-bit Float WAV file").arg(FileName));
        QMessageBox::critical(this, "Error", "Invalid PCM Wave File");
        return false;
    }
    return true;
}

bool MainWindow::choosePlaybackFile()
{
    QFileDialog fileDialog(this, tr("Open WAV File"), mCurrentDir.absolutePath(), tr("WAV Files (*.wav)"));
    fileDialog.setOptions(QFileDialog::DontUseNativeDialog);

    while (fileDialog.exec() == QDialog::Accepted) {
        if (SetPlaybackFile(fileDialog.selectedFiles().constFirst())) return true;
    }
    return false;
}

bool MainWindow::SetPlaybackFile(const QString &fileName)
{
    WavFileInfo info = WavFileReader::readHeader(fileName);
    if (!info.ok) {
        statusBar()->showMessage(tr("File %1 could not be opened").arg(QDir::toNativeSeparators(fileName)));
        QMessageBox::critical(this, tr("Error"), tr("Failed to open WAV file: %1").arg(info.error));
        return false;
    }

    if (!WavFileReader::isSupportedFormat(info.header)) {
        statusBar()->showMessage(tr("File %1 is not a supported mono 32-bit float WAV file").arg(fileName));
        QMessageBox::critical(this, tr("Error"), tr("Invalid PCM Wave File"));
        return false;
    }

    mPlaybackFileName = fileName;
    emit selectedWavFileChanged();
    mCurrentDir = QFileInfo(fileName).dir();
    statusBar()->showMessage(tr("Playback file selected: %1").arg(QFileInfo(fileName).fileName()));
    return true;
}

void MainWindow::SetGuiRunMode(void)
{
    mIsRunning = true;
    emit isRunningChanged();
}

void MainWindow::SetGuiStopMode(void)
{
    clearPauseState();
    if (mSeekLabel)  mSeekLabel->clear();

    mLastPhaseName.clear();
    mLastPhaseLabel.clear();
    mLastPhaseRemainingSec = 0;

    mIsRunning = false;
    emit isRunningChanged();
}

void MainWindow::SyncDetectorBphToSimBph(void)
{
    if (mCurrentMode != SIM) return;
    int simBphVal = mSimBphList[mSimBphIndex].toInt();
    int detectorIdx = mBphList.indexOf(QString::number(simBphVal));
    if (detectorIdx >= 0) {
        setDetectorBphIndex(detectorIdx);
    }
}

QObject *MainWindow::positionTiming() const
{
    return mPositionTiming;
}

void MainWindow::onPositionPhaseChanged(const QString &positionName,
                                        const QString &phaseLabel, int remainingSec)
{
    mLastPhaseName = positionName;
    mLastPhaseLabel = phaseLabel;
    mLastPhaseRemainingSec = remainingSec;

    // [MPS 포지션 전환] 다음 포지션이 실제 측정에 진입하면 요약바 고정 해제.
    //  대기 팝업/안정화(stabilizing) 동안에는 계속 고정 상태를 유지한다.
    if (phaseLabel == QStringLiteral("measuring") && mReadoutFrozen) {
        mReadoutFrozen = false;
        DisplayResults();
    }

    if (mSequenceDisplay)
        mSequenceDisplay->setPhaseStatus(phaseLabel, remainingSec);

    if (mIsPaused) {
        updatePauseStatusMessage();
        return;
    }

    if (phaseLabel.isEmpty() || phaseLabel == QStringLiteral("idle")) {
        statusBar()->showMessage(QStringLiteral("Stopped"));
        return;
    }

    const QString phaseText = (phaseLabel == QStringLiteral("stabilizing"))
        ? QStringLiteral("Stabilizing") : QStringLiteral("Measuring");
    statusBar()->showMessage(QStringLiteral("%1 %2 — %3 s remaining")
        .arg(phaseText, positionName)
        .arg(remainingSec));
}

void MainWindow::onPositionMeasurementEnded(int positionIndex,
                                          const QString &positionName,
                                          const QString &nextPositionName,
                                          bool sequenceComplete)
{
    const bool onSequenceTab = ui && ui->GraphicsTabWidget && mSequenceDisplay
        && ui->GraphicsTabWidget->currentWidget() == mSequenceDisplay;

    // [MPS 탭 한정] 포지션 전환 부수효과(요약바 고정 · 엔진 리셋 · 트레이스 탭 리셋)는
    //  사용자가 Multi-Position Sequence Display 탭을 보고 있을 때만 수행한다.
    //  다른 그래프 탭을 보는 중이라면 그 그래프에 영향을 주지 않도록 아무것도 하지 않는다.
    if (onSequenceTab) {
        // [MPS 포지션 전환] 측정 창이 끝나는 즉시 상단 요약바를 고정한다.
        //  → 대기 팝업/안정화 동안 직전 포지션의 마지막 값이 그대로 유지되고 흔들리지 않는다.
        //    다음 포지션이 'measuring' 으로 진입하면 onPositionPhaseChanged 에서 해제.
        if (!sequenceComplete)
            mReadoutFrozen = true;

        Q_UNUSED(positionIndex);
        Q_UNUSED(positionName);
        Q_UNUSED(nextPositionName);
        const QList<int> remaining = mSequenceDisplay->remainingPositionIndices();
        if (!remaining.isEmpty()) {
            PositionChangeDialog dlg(mSequenceDisplay->measuredPositionIndices(),
                                     remaining,
                                     PositionChangeDialog::Mode::ChangePosition,
                                     this);
            mActivePositionDialog = &dlg;
            dlg.exec();
            mActivePositionDialog = nullptr;
        }

        // [MPS 포지션 전환] 다음 포지션으로 넘어가기 직전에 엔진 누적 통계를 비운다.
        //  → 포지션 이동/안정화 구간(다이얼로그 동안 들어온 핸들링 노이즈 포함)이 다음
        //    포지션 측정값에 섞이지 않게 한다. BPH 락은 보존되어 즉시 깨끗하게 재수렴.
        if (!sequenceComplete) {
            mEngine.resetForPositionChange();
            // 트레이스/스코프 표시만 새 포지션 기준으로 비우고, 시퀀스 누적표(포지션별
            //  측정값)는 보존한다. 요약바는 mReadoutFrozen 으로 이미 고정된 상태.
            if (mTabManager) mTabManager->broadcastResetExcept(mSequenceDisplay);
        }
    }

    if (mPositionSequence)
        mPositionSequence->confirmPositionChange();
}

void MainWindow::onAllPositionsMeasured()
{
    if (!mSequenceDisplay || !mSequenceDisplay->hasAllPositionsMeasured())
        return;

    PositionChangeDialog dlg(mSequenceDisplay->measuredPositionIndices(),
                             QList<int>(),
                             PositionChangeDialog::Mode::SequenceComplete,
                             this);
    dlg.exec();

    // [MPS 시퀀스 완료] 모든 포지션 측정이 끝났으므로 확인 후 측정을 중단(stop)한다.
    if (mIsRunning)
        stopSession();
}

// =============================================================================
// Control Panel Collapse / Expand
// =============================================================================

// Layout constants
static constexpr int PANEL_EXPANDED_W    = 242;
static constexpr int PANEL_COLLAPSED_W   = 40;
static constexpr int CONTENT_EXPANDED_X  = 250;
static constexpr int CONTENT_COLLAPSED_X = 48;   // 40px panel + 8px gap
static constexpr int READOUT_H           = 50;
static constexpr int TAB_Y               = 53;
static constexpr int WINDOW_W           = 1280;
static constexpr int RIGHT_MARGIN        = 8;
static constexpr int BOTTOM_MARGIN       = 4;

void MainWindow::setControlPanelCollapsed(bool collapsed)
{
    if (mControlPanelCollapsed == collapsed)
        return;
    mControlPanelCollapsed = collapsed;
    emit controlPanelCollapsedChanged();
    onControlPanelToggled(collapsed);
}

void MainWindow::onControlPanelToggled(bool collapsed)
{
    const int panelW   = collapsed ? PANEL_COLLAPSED_W  : PANEL_EXPANDED_W;
    const int contentX = collapsed ? CONTENT_COLLAPSED_X : CONTENT_EXPANDED_X;

    ui->ControlPanelPlaceholder->setFixedWidth(panelW);

    QWidget *host = ui && ui->CentralWidget ? static_cast<QWidget *>(ui->CentralWidget) : this;
    const int hostW = host ? host->width() : width();
    const int hostH = host ? host->height() : height();

    const int contentW = qMax(200, hostW - contentX - RIGHT_MARGIN);
    const int tabH = qMax(200, hostH - TAB_Y - BOTTOM_MARGIN);

    mReadoutBar->setGeometry(contentX, 0, contentW, READOUT_H);
    ui->GraphicsTabWidget->setGeometry(contentX, TAB_Y, contentW, tabH);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    onControlPanelToggled(mControlPanelCollapsed);
}