#include <QtGlobal>
#include "MainWindow.h"
#include "PositionChangeDialog.h"
#include "./ui_MainWindow.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QQuickWidget>
#include <QQmlContext>
#include <QFileDialog>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QDebug>
#include <QtMath>
#include "WaveHeader.h"
#include "WavFileReader.h"
#include "PerfInstrumentation.h"
#include "UiResponsivenessSampler.h"

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

#if defined(Q_OS_LINUX)
#include "LinuxAudio.h"
#elif defined(Q_OS_WIN)
#include "WindowsAudio.h"
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

    // Hide legacy Results label and setup styled ReadoutBar
    ui->Results->hide();
    mReadoutBar = new ReadoutBar(ui->CentralWidget);
    mReadoutBar->setGeometry(250, 0, 1020, 50);
    mReadoutBar->show();

    // Reposition GraphicsTabWidget to make room for ReadoutBar
    ui->GraphicsTabWidget->setGeometry(250, 53, 1025, 661);

    // Load static lists for models
    LoadBPH();
    LoadSimBPH();
    LoadAverageingPeriod();
    LoadAudioDevices();

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
    connect(mPositionSequence, &PositionSequenceController::currentPositionIndexChanged,
            this, [this](int idx) {
        if (mSequenceDisplay)
            mSequenceDisplay->setCurrentPositionByIndex(idx);
    });

    QVBoxLayout *cpLayout = new QVBoxLayout(ui->ControlPanelPlaceholder);
    cpLayout->setContentsMargins(0, 0, 0, 0);
    cpLayout->addWidget(mControlPanelQuickWidget);

    DisplayResults();

#if PERF_ENABLE
    new UiResponsivenessSampler(this);
#endif

    RegisterDisplayTabs();

    mCapture = new CaptureController(&mEngine, mTabManager, this);
    connect(mCapture, &CaptureController::statusMessage,  this, [this](const QString &m){ statusBar()->showMessage(m); });
    connect(mCapture, &CaptureController::measurementReady, this, &MainWindow::DisplayResults);
    // [워치독] 이벤트 → EventHandler(severity 별 상태바/모달). 워커 스레드에서 큐드 전달.
    mEventHandler = new EventHandler(this, this);
    connect(mCapture, &CaptureController::watchdogEvent, mEventHandler, &EventHandler::onEvent);
    connect(mCapture, &CaptureController::playbackDoneReadingFile, this, &MainWindow::HandlePlaybackDoneReadingFile);
    connect(mCapture, &CaptureController::simDone,                 this, &MainWindow::HandleSimDone);
    
    mCapture->setUseConset(mUseConset);

#if PERF_ENABLE
    if (mRateScope) connect(mRateScope, &TabRateScope::scopeReplotted, mCapture, &CaptureController::onScopeReplotted);
#endif
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::RegisterDisplayTabs(void)
{
    mTabManager = new TabManager(ui->GraphicsTabWidget, this);
    mTabManager->addWaveSink(&mWaveHistory);

    mSequenceDisplay = new TabSequenceDisplay(this);
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

    // Apply touch-friendly stylesheet and enable scrolling for GraphicsTabWidget
    ui->GraphicsTabWidget->setUsesScrollButtons(true);
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
        "QTabBar QToolButton {"
        "    background-color: #252530;"
        "    border: 1px solid #2e2e3a;"
        "    border-radius: 4px;"
        "    width: 28px;"
        "    height: 28px;"
        "}"
        "QTabBar QToolButton:hover {"
        "    background-color: #ab47bc;"
        "}"
    ));

    QWidget *corner = new QWidget(this);
    auto *cl = new QHBoxLayout(corner); cl->setContentsMargins(0, 0, 6, 0); cl->setSpacing(8);
    mSeekLabel = new QLabel(this);
    mSeekLabel->setStyleSheet(QStringLiteral("color:#960096; font-weight:bold;"));
    mPauseBtn = new QPushButton(QStringLiteral("⏸ Pause"), this);
    mPauseBtn->setCheckable(true);
    cl->addWidget(mSeekLabel); cl->addWidget(mPauseBtn);
    ui->GraphicsTabWidget->setCornerWidget(corner, Qt::TopRightCorner);
    
    connect(mPauseBtn, &QPushButton::toggled, this, [this](bool p) {
        if (p && !mWaveHistory.hasData()) {
            mPauseBtn->blockSignals(true); mPauseBtn->setChecked(false); mPauseBtn->blockSignals(false);
            return;
        }
        mPauseBtn->setText(p ? QStringLiteral("▶ Resume") : QStringLiteral("⏸ Pause"));
        if (p) updateSeekLabel((double)mWaveHistory.latestAbs());
        else if (mSeekLabel) mSeekLabel->clear();
        if (mCapture)    mCapture->setPaused(p);
        if (mTabManager) mTabManager->setPaused(p);
        if (mRateScope)  mRateScope->setPaused(p);
    });
}

void MainWindow::updateSeekLabel(double absSample)
{
    if (!mSeekLabel) return;
    if (mPauseBtn && !mPauseBtn->isChecked()) return;                  // [③] 선택은 정지 중에만 — 라이브 클릭은 라벨도 무시
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

QString MainWindow::selectedWavFile() const 
{ 
    return mPlaybackFileName.isEmpty() ? "" : QFileInfo(mPlaybackFileName).fileName(); 
}

bool MainWindow::isRunning() const 
{ 
    return mIsRunning; 
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
    if (mPositionSequence)
        mPositionSequence->stop();
    if (mSequenceDisplay)
        mSequenceDisplay->setPhaseStatus(QString(), 0);

    SetGuiStopMode();

    if (mCurrentMode == LIVE) {
        mCapture->stopLive();
        AudioCloseCheck();
    } else if (mCurrentMode == PLAYBACK) {
        mCapture->stopPlayback();
        AudioCloseCheck();
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    } else if (mCurrentMode == SIM) {
        mCapture->stopSim();
        AudioCloseCheck();
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }

    statusBar()->showMessage("Stopped");
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
    if (mRecordSessionEnabled && !RecordSessionCheck()) return;
    Reset();
    pushCaptureConfig();
    QAudioDevice dev = mAudioInputDevices[mDeviceIndex];
    mCapture->startLive(dev, mCurrentSamplesPerSecond, mGain);
    SetGuiRunMode();
    if (mPositionSequence)
        mPositionSequence->start();
    statusBar()->showMessage("Running");
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
    statusBar()->showMessage("Running");
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
    statusBar()->showMessage("Running");
}

void MainWindow::DisplayResults(void)
{
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
    if (mPauseBtn && mPauseBtn->isChecked()) {
        mPauseBtn->blockSignals(true); mPauseBtn->setChecked(false);
        mPauseBtn->setText(QStringLiteral("⏸ Pause")); mPauseBtn->blockSignals(false);
    }
    if (mCapture)    mCapture->setPaused(false);
    if (mTabManager) mTabManager->setPaused(false);
    if (mSeekLabel)  mSeekLabel->clear();
    EventsReset();
}

void MainWindow::HandlePlaybackDoneReadingFile()
{
    SetGuiStopMode();
    if (mCurrentMode == PLAYBACK) {
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }
    AudioCloseCheck();
    statusBar()->showMessage("Stopped");
}

void MainWindow::HandleSimDone()
{
    SetGuiStopMode();
    if (mCurrentMode == SIM) {
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }
    AudioCloseCheck();
    statusBar()->showMessage("Stopped");
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
    if (mPauseBtn && mPauseBtn->isChecked()) {
        mPauseBtn->blockSignals(true); mPauseBtn->setChecked(false);
        mPauseBtn->setText(QStringLiteral("⏸ Pause")); mPauseBtn->blockSignals(false);
    }
    if (mCapture)    mCapture->setPaused(false);
    if (mTabManager) mTabManager->setPaused(false);
    if (mSeekLabel)  mSeekLabel->clear();

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
    if (mSequenceDisplay)
        mSequenceDisplay->setPhaseStatus(phaseLabel, remainingSec);

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
    if (sequenceComplete) {
        QString message = tr("Measurement time for %1 is complete.\n\n"
                             "All core positions in the sequence are done.")
                              .arg(positionName);
        QMessageBox::information(this, tr("Sequence Complete"), message);
    } else {
        if (ui && ui->GraphicsTabWidget && mSequenceDisplay && ui->GraphicsTabWidget->currentWidget() == mSequenceDisplay) {
            PositionChangeDialog dlg(positionName, nextPositionName, positionIndex, this);
            dlg.exec();
        }

        if (mPositionSequence)
            mPositionSequence->confirmPositionChange();
    }
}