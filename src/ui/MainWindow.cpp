#include <QtGlobal>
#include "MainWindow.h"
#include "./ui_MainWindow.h"
#include <QMediaDevices>   // 오디오 입력 장치 열거(LoadAudioDevices)
#include <QAudioDevice>
#include <QPushButton>     // 전역 Pause/Resume(탭바 코너위젯)
#include <QLabel>          // 전역 seek 위치 라벨
#include <QHBoxLayout>     // 코너 위젯(라벨+버튼) 레이아웃
#include <QWidget>
#include "WaveHeader.h"
#include "WavFileReader.h"   // WAV 헤더 파싱(파일 I/O 분리)
#include "PerfInstrumentation.h"   // [PERF 계측] 지연/처리량/자원 측정 (docs/PERF_VERIFICATION_GUIDE.md)
#include "UiResponsivenessSampler.h"   // [PERF · §A-3] UI 응답성 샘플러(계측 책임 분리)

// [탭 모듈 · QA-MOD-01] 디스플레이 탭 매니저 + 신규 탭 모듈들 (tabs/) — 코어 DSP 불변
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

#if defined(Q_OS_LINUX)
#include "LinuxAudio.h"
#elif defined(Q_OS_WIN)
#include "WindowsAudio.h"
#endif

#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>
#include <QtMath>
#include <QMessageBox>

#ifdef ENABLE_VISION
#include <QThread>
#include "VisionWorker.h"          // [vision] 웹캠 1Hz watch-position 추론 워커
#endif

#define  LIVE     0
#define  PLAYBACK 1
#define  SIM      2

#define  GRAPH_HISTORY_IN_SECONDS           10
#define  DETECTOR_NUMBER_OF_SAMPLES      4096u
#define  ERROR_RATE_Y_SCALE                 10
#define  ERROR_RATE_X_DATA_POINTS          250
#define  RLS_WINDOW_INIT                   100
#define  INWARD_MARKER_LENGTH             (500*(mCurrentSamplesPerSecond/48000.0))

#define PLAYBACK_OR_SIM_PCM             "Playback/Sim"

#define PREF_NAME_WELSHI                "Welshi USB"
#define PREF_NAME_CHINESE_GENERIC       "Chinese Generic USB"

#define LINUX_SOUND_CARD_NAME           "USB PnP Sound Device"
#define LINUX_SOUND_MIC_NAME            "Mic Capture Volume"
#define LINUX_SOUND_AGC_NAME            "Auto Gain Control"
#define LINUX_SOUND_MIC_PERCENT_VOLUME  50

#define WINDOWS_SOUND_ENDPOINT_NAME      "USB PnP Sound Device"
#define WINDOWS_SOUND_MIC_NAME           "USB PnP Sound Device"
#define WINDOWS_SOUND_MIC_PERCENT_VOLUME 50


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

static QString ModeStrings[] =
{
        "Live",
        "Playback",
        "Sim",
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
    // 검출기/파이프라인/FPS 상태는 CaptureController 로 이동(초기화도 그쪽에서).

    ui->setupUi(this);
    this->setWindowTitle("TimeGrapher");

    ui->StopPushButton->setEnabled(false);
    ui->LiftAngleSpinBox->setFocusPolicy(Qt::NoFocus);

    ui->Results->setAlignment(Qt::AlignHCenter);
    ui->LiftAngleSpinBox->setValue(mLiftAngle);

    LoadBPH();
    LoadSimBPH();
    LoadAudioDevices();
    LoadAverageingPeriod();
    DisplayResults();

    // ── [PERF 계측] CPU·메모리·스로틀(자원)은 앱 내부에서 측정하지 않는다 ──
    //  관측자 효과(측정 자체가 CPU/메모리를 먹어 결과 오염)를 피하기 위해 외부 도구로 측정한다.
    //    예) psrecord $(pidof TimeGrapher) --interval 1 --plot perf_ext.png
    //        pidstat -r -u -p $(pidof TimeGrapher) 1     # CPU% + RSS
    //        watch -n1 vcgencmd get_throttled            # Pi 스로틀
    //  앱 내부 계측은 '밖에서 못 보는' 의미론적 지표(지연·정확도·FPS·백로그·이벤트루프 지연)만 담당.
    //  자세한 외부 측정 런북: docs/*/PERF_VERIFICATION_GUIDE.md

#if PERF_ENABLE
    // [PERF 계측 · §A-3 · QA-RT-01] UI 응답성(이벤트 루프 지연) 상시 샘플러 — 자체 타이머 소유.
    //  this 에 부모로 묶여 자동 소멸. (계측 책임을 UI 밖으로 분리)
    new UiResponsivenessSampler(this);
#endif

    // ── [PERF 계측 · §A-1/A-2 · QA-LT-01] 실제 '그리기 완료' 시점 포착 ──
    //  ScopePlot 은 TabRateScope 로 이동했다 → 그 탭의 scopeReplotted() 시그널을 RegisterDisplayTabs 에서
    //  OnScopeReplotted 에 연결해, (요청→페인트)·진짜 종단간(캡처→페인트)을 기록한다.

    // [탭 모듈] 신규 디스플레이 탭들을 생성·등록. 코어 DSP 불변(QA-MOD-01).
    RegisterDisplayTabs();

    // 입력 소스 + 신호 파이프라인은 CaptureController 담당(엔진·탭에 직접 게시).
    //  UI 갱신은 저빈도 신호로만: 상태바(FPS/정지) · readout(비트당). 핫패스는 컨트롤러 내부 직접 호출.
    mCapture = new CaptureController(&mEngine, mTabManager, this);
    connect(mCapture, &CaptureController::statusMessage,  this, [this](const QString &m){ statusBar()->showMessage(m); });
    connect(mCapture, &CaptureController::measurementReady, this, &MainWindow::DisplayResults);
    connect(mCapture, &CaptureController::playbackDoneReadingFile, this, &MainWindow::HandlePlaybackDoneReadingFile);
    connect(mCapture, &CaptureController::simDone,                 this, &MainWindow::HandleSimDone);
    // UseConset 체크박스는 런타임 토글 → 컨트롤러에 반영
    connect(ui->UseConsetCheckBox, &QCheckBox::toggled, this, [this](bool c){ mCapture->setUseConset(c); });
    mCapture->setUseConset(ui->UseConsetCheckBox->isChecked());
    // 탭 ScopePlot 의 실제 paint 완료 → 컨트롤러 perf(disp_paint/e2e_full/paint_fps)
#if PERF_ENABLE
    // 탭 ScopePlot 실제 paint 완료 → 컨트롤러 perf(disp_paint/e2e_full/paint_fps). 계측 OFF 면 배선 자체 제거.
    if (mRateScope) connect(mRateScope, &TabRateScope::scopeReplotted, mCapture, &CaptureController::onScopeReplotted);
#endif

#ifdef ENABLE_VISION
    // [vision] 웹캠 watch-position 추론을 전용 스레드에서 병렬 실행(초당 1회 → 결과 print).
    //  기존 오디오 워커와 동일한 QObject+QThread 패턴. UI/측정 핫패스와 독립.
    mVisionThread = new QThread(this);
    mVisionWorker = new vision::VisionWorker();
    mVisionWorker->moveToThread(mVisionThread);
    connect(mVisionThread, &QThread::started,  mVisionWorker, &vision::VisionWorker::start);
    connect(mVisionThread, &QThread::finished, mVisionWorker, &QObject::deleteLater);
    mVisionThread->start();
#endif
}
// [탭 모듈 · QA-MOD-01] 기존 2개 탭(RateTab/SoundTab, MainWindow.ui 정의)에 더해 신규 탭
//  모듈을 TabManager 로 등록한다. 새 탭 추가 = TabView 상속 클래스 1개 + 아래 한 줄.
//  ("Split module" / "Restrict dependencies" 택틱 — 코어/기존 탭 수정 불필요)
void MainWindow::RegisterDisplayTabs(void)
{
    mTabManager = new TabManager(ui->GraphicsTabWidget, this);
    // [8분 이력] 중앙 엔벨로프 버퍼를 wave 방송 청취자로 등록(구독자 방식 · OCP/ISP).
    //  탭이 아닌 비시각 수집기라 broadcastWave 만 받는다. 첫 wave 에서 샘플레이트로 지연 구성.
    mTabManager->addWaveSink(&mWaveHistory);
    // Rate/Scope 를 첫 탭으로(기본 표시) — ScopePlot 이 기본으로 paint 되어 perf(disp_paint 등) 측정 유지.
    mRateScope = new TabRateScope(this);
    mRateScope->setHistory(&mWaveHistory);                      // [8분 스크롤백] 정지 중 렌더 원본 주입
    // [③] 상단 RatePlot 클릭 → 그 시점을 전파 + 코너 라벨.
    connect(mRateScope, &TabRateScope::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(mRateScope, &TabRateScope::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(mRateScope);                       // rate 시계열 + 실시간 스코프
    //  ScopePlot afterReplot → CaptureController::onScopeReplotted 연결은 생성자(mCapture 생성 후)에서.
    mTabManager->registerTab(new TabSoundPrint(this));          // 폴딩 사운드 이미지
    auto *traceTab = new TabTraceDisplay(this);                 // FR-TD
    // [③] 정지 중 Trace 트렌드 클릭 → 그 시점을 모든 스코프 탭에 전파(Rate/Scope 점프) + 코너 라벨 갱신.
    connect(traceTab, &TabTraceDisplay::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(traceTab, &TabTraceDisplay::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(traceTab);
    mTabManager->registerTab(new TabVarioStability(this));      // FR-RAS
    mTabManager->registerTab(new TabSequenceDisplay(this));     // FR-MPS
    auto *bnsTab = new TabBeatNoiseScope(this);                // FR-BNS
    bnsTab->setHistory(&mWaveHistory);                         // [③] seek 대상(그 비트 표시)
    connect(bnsTab, &TabBeatNoiseScope::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(bnsTab, &TabBeatNoiseScope::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(bnsTab);
    auto *bedTab = new TabBeatErrorTrace(this);                // FR-BED
    connect(bedTab, &TabBeatErrorTrace::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(bedTab, &TabBeatErrorTrace::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(bedTab);
    auto *ltpTab = new TabLongTermPerformance(this);           // FR-LTP
    // [③] Long-Term 트렌드(8분 시간축) 클릭 → 그 시점을 스코프 탭에 전파 + 코너 라벨 갱신.
    connect(ltpTab, &TabLongTermPerformance::seekRequested, mTabManager, &TabManager::broadcastSeek);
    connect(ltpTab, &TabLongTermPerformance::seekRequested, this, &MainWindow::updateSeekLabel);
    mTabManager->registerTab(ltpTab);
    auto *escTab = new TabEscapementAnalyzer(this);             // FR-EAM
    escTab->setHistory(&mWaveHistory);                          // [③] seek replay 원본 주입
    mTabManager->registerTab(escTab);
    auto *specTab = new TabSpectrogram(this);                  // FR-TFS
    specTab->setHistory(&mWaveHistory);                        // [③] seek 대상(그 구간 스펙트로그램)
    mTabManager->registerTab(specTab);
    auto *wcmpTab = new TabWaveformCompare(this);               // FR-WCD
    wcmpTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(wcmpTab);
    auto *sweepTab = new TabSyncSweepScope(this);               // FR-SMS
    sweepTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(sweepTab);
    auto *filterTab = new TabFilterViews(this);                 // FR-SFM(F0~F3)
    filterTab->setHistory(&mWaveHistory);
    mTabManager->registerTab(filterTab);

    // [8분 스크롤백] 전역 Pause/Resume + seek 위치 라벨 — 탭바 코너에 두어 어느 탭에서나 보인다.
    //  정지 → TabManager 전 탭 동결(방송 중단, 이력 버퍼는 계속) + Rate/Scope 스크롤백 진입.
    auto *corner = new QWidget(this);
    auto *cl = new QHBoxLayout(corner); cl->setContentsMargins(0, 0, 6, 0); cl->setSpacing(8);
    mSeekLabel = new QLabel(this);                          // 현재 보는 시점(t/샘플) — 모든 탭 공통
    mSeekLabel->setStyleSheet(QStringLiteral("color:#960096; font-weight:bold;"));
    mPauseBtn = new QPushButton(QStringLiteral("⏸ Pause"), this);
    mPauseBtn->setCheckable(true);
    cl->addWidget(mSeekLabel); cl->addWidget(mPauseBtn);
    ui->GraphicsTabWidget->setCornerWidget(corner, Qt::TopRightCorner);
    connect(mPauseBtn, &QPushButton::toggled, this, [this](bool p) {
        if (p && !mWaveHistory.hasData()) {                     // 아직 이력 없으면 정지 무시(버튼 원복)
            mPauseBtn->blockSignals(true); mPauseBtn->setChecked(false); mPauseBtn->blockSignals(false);
            return;
        }
        mPauseBtn->setText(p ? QStringLiteral("▶ Resume") : QStringLiteral("⏸ Pause"));
        // 시점 라벨: 정지 시작 → 마지막(최신) 위치로 채움 / 라이브 복귀 → 지움.
        if (p) updateSeekLabel((double)mWaveHistory.latestAbs());
        else if (mSeekLabel) mSeekLabel->clear();
        if (mCapture)    mCapture->setPaused(p);               // [전체 정지] 소스 워커까지 멈춤(위치 보존)
        if (mTabManager) mTabManager->setPaused(p);            // 인플라이트 데이터 차단 + seek 게이트
        if (mRateScope)  mRateScope->setPaused(p);
    });
}

// [③] 코너 seek 위치 라벨 갱신 — 모든 트렌드 클릭 소스가 공통으로 호출.
void MainWindow::updateSeekLabel(double absSample)
{
    if (!mSeekLabel) return;
    const int sr = mWaveHistory.sampleRate();
    const double t = sr > 0 ? absSample / (double)sr : 0.0;
    mSeekLabel->setText(QString("viewing  t=%1 s   #%2").arg(t, 0, 'f', 1).arg((qint64)absSample));
}

// [탭 모듈] 현재 측정값을 읽기 전용 스냅샷으로 묶어 모든 탭에 게시. 탭은 코어 내부가 아니라
//  이 스냅샷에만 의존한다(QA-MOD-01 / Restrict dependencies).
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
    snap.synced         = res.bphValid;
    snap.sampleRateHz   = mCurrentSamplesPerSecond;
    snap.totalSamples   = mCapture ? mCapture->totalSamples() : 0;
    snap.liftAngle      = (int)mLiftAngle;
    snap.mode           = ui->ModeComboBox->currentIndex();
    // RatePlot 시리즈(포인터는 이 호출 동안만 유효 → 탭이 복사).
    snap.rateTicX = mEngine.ticX().constData(); snap.rateTicY = mEngine.ticY().constData(); snap.rateTicN = mEngine.ticX().size();
    snap.rateTocX = mEngine.tocX().constData(); snap.rateTocY = mEngine.tocY().constData(); snap.rateTocN = mEngine.tocX().size();
    snap.rateMaxPoints = mEngine.maxDataPoints();
    mTabManager->broadcastMeasurement(snap);
}

// [PERF 계측 · §A-1/A-2 · QA-LT-01] ScopePlot 이 '실제로 다 그려진' 직후 호출됨.
//  disp_paint_ms = (페인트 완료 − replot 요청) = 미뤄졌던 그리기 시간.
//  e2e_full_ms   = (페인트 완료 − 캡처)        = ★진짜 종단간★ = (캡처→요청) + (요청→페인트) 의 합.
// OnScopeReplotted(실제 paint 완료 perf) 은 CaptureController::onScopeReplotted 로 이동
//  — 탭 ScopePlot afterReplot → mCapture->onScopeReplotted 로 연결(생성자 배선).

// [PERF 계측 · §A-3 · QA-RT-01] UI 이벤트 루프 응답성 측정은 UiResponsivenessSampler 로 이동.

void   MainWindow::ConfigureSoundCard(void)
{
#if defined(Q_OS_LINUX)
    //LinuxListSoundCardsAndElements();
    LinuxSetSoundParameters(LINUX_SOUND_CARD_NAME,LINUX_SOUND_MIC_NAME,LINUX_SOUND_AGC_NAME,LINUX_SOUND_MIC_PERCENT_VOLUME);
#elif defined(Q_OS_WIN)
    //WindowsListSoundCardsAndElements();
    WindowsSetSoundParameters(WINDOWS_SOUND_ENDPOINT_NAME,WINDOWS_SOUND_MIC_NAME,WINDOWS_SOUND_MIC_PERCENT_VOLUME);
#endif
}
void MainWindow::EventsReset(void)
{
    mEngine.reset();   // 측정 상태/시리즈 비움(롤링 통계 리셋). RatePlot 비우기는 TabRateScope(onResetSession).
    DisplayResults();
}

void MainWindow::LoadAudioDevices(void)
{
    const QList<QAudioDevice> inputDevices = QMediaDevices::audioInputs();
    ui->InputDeviceComboBox->clear();
#if 1  // zero to test no devices
    int RenameLen=sizeof RenameAudioDevices / sizeof RenameAudioDevices[0];
    for (const QAudioDevice &d : inputDevices)
    {
        QString Description=d.description();
        for (int i=0;i<RenameLen;i++)
        {
            if (Description.contains(RenameAudioDevices[i][0],Qt::CaseSensitive))
            {
                Description=RenameAudioDevices[i][1];
                break;
            }
        }
        ui->InputDeviceComboBox->addItem(Description,QVariant::fromValue(d));
        qInfo()<<"Device Name - "<<Description;
    }
#endif
    ui->InputDeviceComboBox->addItem(PLAYBACK_OR_SIM_PCM);
    qInfo()<<"Device Name - "<<PLAYBACK_OR_SIM_PCM;

    int len = std::size(PreferredAudioDevices);

    for (int i=0;i<len;i++)
    {
     int index = ui->InputDeviceComboBox->findText(PreferredAudioDevices[i],Qt::MatchContains);
     if (index != -1) // -1 means the text was not found
       {
         ui->InputDeviceComboBox->setCurrentIndex(index);
         break;
       }
    }
    LoadMode();
}

void MainWindow::LoadAverageingPeriod(void)
{
    auto length = std::size(AveragingPeriodList);
    QString Name;
    for (int i=0;i<length;i++)
    {
        Name=QString::asprintf("%ds", AveragingPeriodList[i]);
        ui->AveragingPeriodComboBox->addItem(Name,AveragingPeriodList[i]);
    }

    ui->AveragingPeriodComboBox->setCurrentIndex(4); //20 Seconds
}
void MainWindow::LoadBPH(void)
{
  auto length = std::size(ManualAutoBPH);
  QString Name;
  for (int i=0;i<length;i++)
  {
      if (ManualAutoBPH[i]!=0) Name.setNum(ManualAutoBPH[i]);
      else  Name="Auto BPH";
      ui->BPHComboBox->addItem(Name,ManualAutoBPH[i]);
  }
  ui->BPHComboBox->setCurrentIndex(0); //Auto
}

void MainWindow::LoadSimBPH(void)
{
    auto length = std::size(SimBPH);
    QString Name;
    for (int i=0;i<length;i++)
    {
        Name.setNum(SimBPH[i]);
        ui->SimBPHComboBox->addItem(Name,SimBPH[i]);
    }
    ui->SimBPHComboBox->setCurrentIndex(52);
}
void MainWindow::LoadMode(void)
{
    int start=0;
    int len = std::size( ModeStrings);
    ui->ModeComboBox->clear();

    if (ui->InputDeviceComboBox->count()==1) // Skip over Live
    {
     start++;
    }
    for (int i=start;i<len;i++)
    {
        ui->ModeComboBox->addItem(ModeStrings[i],i);
    }
    ui->ModeComboBox->setCurrentIndex(0);
}

// A_Event/C_Event(측정 이벤트)·ProcessSamples·HandleInputData·검출기 생성은
//  CaptureController 로 이동했다. MainWindow 는 measurementReady 신호로 DisplayResults 만 한다.
void MainWindow::DisplayResults(void)
{
    MeasurementEngine::Results res = mEngine.results();
    QString BeatsPerHour,RateError,BeatError,Amplitude, Results;
    if (res.bphValid)
    {
        BeatsPerHour= QString("%1").arg(res.bph, 5, 10, QChar(' '));
    }
    else BeatsPerHour="-----";

    if (res.rateValid)
    {
        RateError= QString::asprintf("%+6.1f", res.rateSecPerDay);
    }
    else RateError="------";
    if (res.beatErrorValid)
    {
        BeatError= QString("%1").arg(res.beatErrorMs, 4, 'f', 1);
    }
    else BeatError="----";
    if (res.amplitudeValid)
    {
        Amplitude=  QString("%1°").arg(qRound64(res.amplitudeDeg), 3, 10, QChar(' '));
    }
    else Amplitude="---";

    Results="RATE "+RateError+" s/d   AMPLITUDE "+Amplitude+"   BEAT ERROR "+BeatError+" ms   BEAT "+BeatsPerHour+" bph";
    ui->Results->setText(Results);

    // 측정 정확도(G-1)·검출률(G-2) perf 는 CaptureController::cEvent 로 이동(Sim 정답 비교).
    // [탭 모듈] 현재 측정값을 모든 디스플레이 탭에 스냅샷으로 게시.
    PublishMeasurementToTabs();
}

// C_Event·CreateDectectors·DeleteDectectors 는 CaptureController 로 이동했다.

// RatePlot/ScopePlot 그래프 설정은 TabRateScope::setupPlots 로 이동했다.

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
    delete ui;
}

// 오디오 소스(스레드·워커·버퍼) 관리는 CaptureController 로 이동했다.
//  MainWindow 는 mCapture->startLive/Playback/Sim · stopX 로 제어하고, dataReady 신호를 받아 처리한다.

// 스코프 마커 헬퍼(AddVerticalMarker/AddText/AddHorizontalMarker*·RemoveMarkersAndText)는
//  ScopePlot 과 함께 TabRateScope 로 이동했다.

// HandleInputData(워커 블록 수신·GT 스냅샷·FPS) 는 CaptureController 로 이동했다.
//  상태바 FPS 메시지는 CaptureController::statusMessage 신호로 전달된다(생성자 배선).
void MainWindow::HandlePlaybackDoneReadingFile()
{
    SetGuiStopMode();
   if (ui->ModeComboBox->currentIndex()==PLAYBACK)
    {
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }
    AudioCloseCheck();
    statusBar()->showMessage("Stopped");
}
void MainWindow::HandleSimDone()
{
    // mSimActive=false 는 CaptureController::onSimWorkerDone 에서 처리(G-1 비교 중단).
    SetGuiStopMode();
    if (ui->ModeComboBox->currentIndex()==SIM)
    {
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }
    AudioCloseCheck();
    statusBar()->showMessage("Stopped");
}
// 스코프 히스토리 정리는 TabRateScope::purgeHistory 로 이동했다.
void MainWindow::Reset(void)
{
    qInfo()<<"RESET";
    // 세션 리셋: 탭 비움(broadcastReset) + 측정엔진 리셋(EventsReset → readout).
    //  검출기 생성·파이프라인 상태(mInputAbsSample·FPS) 리셋은 CaptureController::startX 가 담당.
    if (mTabManager) mTabManager->broadcastReset();
    mWaveHistory.clear();   // [8분 이력] 세션 리셋 = 8분 이력도 비움(좌표 불연속 방지)
    if (mPauseBtn && mPauseBtn->isChecked()) {   // 새 세션 = 정지 해제(전역 버튼 원복)
        mPauseBtn->blockSignals(true); mPauseBtn->setChecked(false);
        mPauseBtn->setText(QStringLiteral("⏸ Pause")); mPauseBtn->blockSignals(false);
    }
    if (mCapture)    mCapture->setPaused(false);   // 새 세션 = 소스 정지 플래그 해제
    if (mTabManager) mTabManager->setPaused(false);
    if (mSeekLabel)  mSeekLabel->clear();          // 새 세션 = 시점 표시 리셋
    EventsReset();
}


bool MainWindow::RecordSessionCheck(void)
{
    QMessageBox msgBox;
    msgBox.setText("Record Session");
    msgBox.setInformativeText("Do you want to record this session ?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::No);

    int ret = msgBox.exec(); // Returns the enum value of the clicked button

    if (ret==QMessageBox::Yes)
    {
        QString fileName = QFileDialog::getSaveFileName(this,
                                                        tr("Save Output File"),
                                                        "../../Output/",
                                                        tr("Wav Files (*.wav);;All Files (*)"));

        if (!fileName.isEmpty())
        {
            // Process the selected file path (e.g., open for writing)
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
    else if (ret==QMessageBox::No) return (true);
    else if (ret==QMessageBox::QMessageBox::Cancel) return (false);

    return (true);

}
void MainWindow::AudioCloseCheck(void)
{
    if (mWavWriter)
    {
        if (mCapture) mCapture->setWavWriter(nullptr);   // 컨트롤러의 비소유 포인터 무효화(삭제 전)
        mWavWriter->close();
        delete mWavWriter;
        mWavWriter=NULL;
    }
}

bool MainWindow::OpenFile(const QString &FileName)
{
    // 헤더 파싱은 WavFileReader 가 담당(순수 파일 I/O). 여기서는 UI 정책만:
    //  파싱 실패 안내 → 재생용 장치/레이트로 전환 → 포맷·레이트 수용 여부 판정.
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


void MainWindow::PopulateSampleRates(QComboBox *comboBox, const QAudioDevice &device)
{
    QList<int> standardRates = {48000, 96000, 192000, 384000};
    comboBox->clear();
    mNumberofRates=0;
    if (device.isNull())
    {
        qInfo()<<"Audio Device is Null";
        for (int rate : standardRates)
        {
         comboBox->addItem(QString::number(rate) + " Hz", rate);
         mAvalableRates[mNumberofRates]=rate;
         mNumberofRates++;
        }
    }
    else
    {
     // Define standard sample rates to test
     for (int rate : standardRates)
     {
        QAudioFormat format;
        format.setSampleRate(rate);
        // Minimum requirements for a valid check usually include channel count and format
        format.setChannelCount(CHANNELS);
        format.setSampleFormat(SAMPLE_FORMAT);

        if (device.isFormatSupported(format)) {
            comboBox->addItem(QString::number(rate) + " Hz", rate);
            mAvalableRates[mNumberofRates]=rate;
            mNumberofRates++;
        }
      }
    }
    comboBox->setCurrentIndex(-1);
    comboBox->setCurrentIndex(0);
}

bool   MainWindow::SetAudioRate(int Rate)
{
    int index = ui->SampleRatesComboBox->findData(Rate);
   if (index != -1) { // -1 means the text was not found
        ui->SampleRatesComboBox->setCurrentIndex(index);
        return(true);
    }
    return (false);

}
bool   MainWindow::SetAudioDevice(QString Name)
{
    int index = ui->InputDeviceComboBox->findText(Name);
    if (index != -1) { // -1 means the text was not found
        ui->InputDeviceComboBox->setCurrentIndex(index);
        return(true);
    }
    return (false);
}
void   MainWindow::GetAudioRate(int &Rate)
{
    Rate=mCurrentSamplesPerSecond;
}
void   MainWindow::GetAudioDevice(QString &Name)
{
  Name=ui->InputDeviceComboBox->currentText();
}
void   MainWindow::SetGuiRunMode(void)
{
    ui->InputDeviceComboBox->setEnabled(false);
    ui->SampleRatesComboBox->setEnabled(false);
    ui->BPHComboBox->setEnabled(false);
    ui->ModeComboBox->setEnabled(false);
    ui->StartPushButton->setEnabled(false);
    ui->StopPushButton->setEnabled(true);
    ui->RefreshPushButton->setEnabled(false);
    ui->AveragingPeriodComboBox->setEnabled(false);
    ui->LiftAngleSpinBox->setEnabled(false);
    ui->SimAmplitudeSpinBox->setEnabled(false);
    ui->SimBeatErrorSpinBox->setEnabled(false);
    ui->SimBPHComboBox->setEnabled(false);
    ui->SimErrorRateSpinBox->setEnabled(false);
    ui->RealisticCheckBox->setEnabled(false);
    ui->UseConsetCheckBox->setEnabled(false);
    ui->HighLineEdit->setEnabled(false);
}

void   MainWindow::SetGuiStopMode(void)
{
    // 세션 종료(Stop/재생완료/Sim완료) = 정지/시점 표시 해제(라이브 종료 상태로 원복).
    if (mPauseBtn && mPauseBtn->isChecked()) {
        mPauseBtn->blockSignals(true); mPauseBtn->setChecked(false);
        mPauseBtn->setText(QStringLiteral("⏸ Pause")); mPauseBtn->blockSignals(false);
    }
    if (mCapture)    mCapture->setPaused(false);
    if (mTabManager) mTabManager->setPaused(false);
    if (mSeekLabel)  mSeekLabel->clear();

    ui->StopPushButton->setEnabled(false);
    ui->ModeComboBox->setEnabled(true);
    ui->RefreshPushButton->setEnabled(true);
    ui->StartPushButton->setEnabled(true);
    ui->InputDeviceComboBox->setEnabled(true);
    if (ui->ModeComboBox->currentText()!=ModeStrings[PLAYBACK])
      {
       ui->SampleRatesComboBox->setEnabled(true);
      }
    ui->AveragingPeriodComboBox->setEnabled(true);
    ui->LiftAngleSpinBox->setEnabled(true);
    ui->BPHComboBox->setEnabled(true);
    ui->LiftAngleSpinBox->setEnabled(true);
    ui->SimAmplitudeSpinBox->setEnabled(true);
    ui->SimBeatErrorSpinBox->setEnabled(true);
    ui->SimBPHComboBox->setEnabled(true);
    ui->SimErrorRateSpinBox->setEnabled(true);
    ui->RealisticCheckBox->setEnabled(true);
    ui->UseConsetCheckBox->setEnabled(true);
    ui->HighLineEdit->setEnabled(true);
}
// 캡처/파이프라인 설정을 UI 에서 읽어 CaptureController 에 주입(세션 시작 직전).
void   MainWindow::pushCaptureConfig(void)
{
    mCapture->setEngineParams(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    bool bphAuto   = (ui->BPHComboBox->currentIndex()==0);
    int  manualBph = bphAuto ? 0 : ManualAutoBPH[ui->BPHComboBox->currentIndex()];
    mCapture->setDetectorConfig(bphAuto, manualBph, ui->HighLineEdit->text().toDouble());
    mCapture->setUseConset(ui->UseConsetCheckBox->isChecked());
    mCapture->setWavWriter(mWavWriter);   // 녹음 대상(없으면 nullptr)
}
void   MainWindow::LiveStart(void)
{
    if (!RecordSessionCheck()) return;
    Reset();              // 탭·측정엔진 리셋
    pushCaptureConfig();  // 검출기/엔진/녹음 설정 주입
    QAudioDevice dev = ui->InputDeviceComboBox->currentData().value<QAudioDevice>();
    mCapture->startLive(dev, mCurrentSamplesPerSecond, ui->MicrophoneHorizontalSlider->sliderPosition()/1000.0);
    SetGuiRunMode();
    statusBar()->showMessage("Running");
}
void   MainWindow::PlaybackStart(void)
{
    bool status=false;

    if (!RecordSessionCheck()) return;

    QFileDialog fileDialog(this, tr("Open Document"), mCurrentDir.absolutePath(),tr("WAV Files (*.wav)"));
    fileDialog.setOptions(QFileDialog::DontUseNativeDialog);
    while (fileDialog.exec() == QDialog::Accepted
           && !(status=OpenFile(fileDialog.selectedFiles().constFirst()))) {
    }
    if (!status) return;
    Reset();
    pushCaptureConfig();
    mCapture->startPlayback(fileDialog.selectedFiles().constFirst(), mCurrentSamplesPerSecond);
    SetGuiRunMode();
    statusBar()->showMessage("Running");
}
void   MainWindow::SimStart(void)
{
    // UI 위젯값만 모은다 — 합성기 구조/기본값/규약은 SimConfigBuilder 가 안다(SoC).
    SimConfigParams p;
    p.realistic          = ui->RealisticCheckBox->isChecked();
    p.bph                = SimBPH[ui->SimBPHComboBox->currentIndex()];
    p.sampleRateHz       = mAvalableRates[ui->SampleRatesComboBox->currentIndex()];
    p.beatErrorMs        = ui->SimBeatErrorSpinBox->value();
    p.watchAmplitudeDeg  = ui->SimAmplitudeSpinBox->value();
    p.liftAngleDeg       = ui->LiftAngleSpinBox->value();
    p.rateErrorSecPerDay = ui->SimErrorRateSpinBox->value();
    WatchSynthStreamConfig cfg = SimConfigBuilder::build(p);

    if (!RecordSessionCheck()) return;
    GetAudioRate(mRateBeforePlaybackOrSim);
    GetAudioDevice(mDeviceNameBeforePlaybackOrSim);
    if (!SetAudioDevice(PLAYBACK_OR_SIM_PCM))
    {
        qInfo()<< "SetAudioDevice Failed";
    }
    if (!SetAudioRate(mRateBeforePlaybackOrSim))
    {
        qInfo()<< "SetAudioRate Failed";
    }
    Reset();
    pushCaptureConfig();
    // Sim 정답 설정값(mLastSimCfg)·mSimActive 보관은 CaptureController::startSim 이 담당(G-1 비교용).
    mCapture->startSim(cfg, mCurrentSamplesPerSecond);
    SetGuiRunMode();
    statusBar()->showMessage("Running");
}

void MainWindow::on_ModeComboBox_currentTextChanged(const QString &arg1)
{
    if (arg1!=ModeStrings[LIVE])
        SetAudioDevice(PLAYBACK_OR_SIM_PCM);
    if (arg1==ModeStrings[PLAYBACK])
    {
        ui->SampleRatesComboBox->setEnabled(false);
    }
    else ui->SampleRatesComboBox->setEnabled(true);
    if (arg1==ModeStrings[LIVE])
    {
        bool isSet=false;
        int len = std::size(PreferredAudioDevices);
        for (int i=0;i<len;i++)
        {
            int index = ui->InputDeviceComboBox->findText(PreferredAudioDevices[i],Qt::MatchContains);
            if (index != -1) // -1 means the text was not found
            {
                ui->InputDeviceComboBox->setCurrentIndex(index);
                isSet=true;

                break;
            }
        }
        if (!isSet)
        {
            for (int i = 0; i <  ui->InputDeviceComboBox->count(); ++i) {
                if (ui->InputDeviceComboBox->itemText(i)!=PLAYBACK_OR_SIM_PCM)
                {
                  ui->InputDeviceComboBox->setCurrentIndex(i);
                  break;
                }
            }
        }

    }
}
void MainWindow::on_RefreshPushButton_clicked()
{
    LoadAudioDevices();
}
void MainWindow::on_LiftAngleSpinBox_valueChanged(int arg1)
{
    mLiftAngle=ui->LiftAngleSpinBox->value();
    if (mCapture) mCapture->setEngineParams(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    qInfo()<<"Lift Angle Value="<<mLiftAngle;
}
void MainWindow::on_AveragingPeriodComboBox_currentIndexChanged(int index)
{
    mAveragingPeriod=AveragingPeriodList[ui->AveragingPeriodComboBox->currentIndex()];
    if (mCapture) mCapture->setEngineParams(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    qInfo()<<"Averaging Period Value="<<mAveragingPeriod;
}
void MainWindow::on_MicrophoneHorizontalSlider_sliderMoved(int position)
{
    if (mCapture) mCapture->setInputVolume(ui->MicrophoneHorizontalSlider->sliderPosition()/1000.0);
}
void MainWindow::on_StartPushButton_clicked()
{
    if (ui->ModeComboBox->currentText()==ModeStrings[LIVE])
    {
        ConfigureSoundCard();
        LiveStart();
    }
    else if (ui->ModeComboBox->currentText()==ModeStrings[PLAYBACK])
    {
        PlaybackStart();
    }
    else if (ui->ModeComboBox->currentText()==ModeStrings[SIM])
    {
        SimStart();
    }
}

void MainWindow::on_StopPushButton_clicked()
{
    SetGuiStopMode();

    if(ui->ModeComboBox->currentText()==ModeStrings[LIVE])
    {
        mCapture->stopLive();
        AudioCloseCheck();
    }
    else if(ui->ModeComboBox->currentText()==ModeStrings[PLAYBACK])
    {
        mCapture->stopPlayback();

        if (mWavWriter)
        {
            if (mCapture) mCapture->setWavWriter(nullptr);
            mWavWriter->close();
            delete mWavWriter;
            mWavWriter=NULL;
        }

        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }
    else if(ui->ModeComboBox->currentText()==ModeStrings[SIM])
    {
        mCapture->stopSim();
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }

    statusBar()->showMessage("Stopped");
}


void MainWindow::on_InputDeviceComboBox_currentIndexChanged(int index)
{
    QAudioDevice InputDevice;
    QVariant v = ui->InputDeviceComboBox->currentData();
    if (ui->InputDeviceComboBox->currentText()!=PLAYBACK_OR_SIM_PCM)
    {
        InputDevice = v.value<QAudioDevice>();

        int index =ui->ModeComboBox->findText(ModeStrings[LIVE]);
        if (index!=-1) ui->ModeComboBox->setCurrentIndex(index);
    }
    else if (ui->InputDeviceComboBox->currentText()==PLAYBACK_OR_SIM_PCM)
    {
        if (ui->ModeComboBox->currentText()==ModeStrings[LIVE])
        {
          int index =ui->ModeComboBox->findText(ModeStrings[PLAYBACK]);
          if (index!=-1) ui->ModeComboBox->setCurrentIndex(index);
        }
    }

    PopulateSampleRates(ui->SampleRatesComboBox, InputDevice);
}
void MainWindow::on_SampleRatesComboBox_currentIndexChanged(int index)
{
    if (index<0 ) return;
    if ((index+1)> mNumberofRates)  return;
    mCurrentSamplesPerSecond=mAvalableRates[index];
    qInfo()<< "Sample Rate is "<<mCurrentSamplesPerSecond<<" Index "<<index;
}
/************************************************************************************/
/* END                                                                              */
/************************************************************************************/