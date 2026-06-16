#include <QtGlobal>
#include "MainWindow.h"
#include "./ui_MainWindow.h"
#include <QMediaDevices>   // 오디오 입력 장치 열거(LoadAudioDevices) — 구 AudioWorker.h 전이 include 대체
#include <QAudioDevice>
#include "WaveHeader.h"
#include "PerfInstrumentation.h"   // [PERF 계측] 지연/처리량/자원 측정 (docs/PERF_VERIFICATION_GUIDE.md)

// [탭 모듈 · QA-MOD-01] 디스플레이 탭 매니저 + 신규 탭 모듈들 (tabs/) — 코어 DSP 불변
#include <QVarLengthArray>
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
#include <QFile>
#include <QDataStream>
#include <QtEndian>
#include <QDebug>
#include <QTextStream>
#include <QtMath>
#include <QRandomGenerator>
#include <QMessageBox>
#include <stdexcept>

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
    mCtx=NULL;
    mInputBlock=NULL;

    mBackgroundLastFPS=0.0;
    mBackgroundLastSPF=0.0;
    mBackgroundLastSPS=0.0;

    ui->setupUi(this);
    this->setWindowTitle("TimeGrapher");

    ui->StopPushButton->setEnabled(false);
    ui->LiftAngleSpinBox->setFocusPolicy(Qt::NoFocus);

    ui->Results->setAlignment(Qt::AlignHCenter);
    ui->LiftAngleSpinBox->setValue(mLiftAngle);
    //QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    //ui->Results->setFont(fixedFont);

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

    // ── [PERF 계측 · §A-3 · QA-RT-01] UI 응답성(이벤트 루프 지연) 0.1초 하트비트 ──
    //  100ms 주기 타이머가 '얼마나 늦게' 실제로 불리는지를 측정한다. ProcessSamples/
    //  replot 등이 메인 스레드를 막으면 타이머가 늦게 발화 → 그 지연 = UI 비응답 시간.
    //  (사용자 클릭 없이도 GUI 반응성(≤200ms)을 상시 정량화)
    mPerfUiTimer = new QTimer(this);
    connect(mPerfUiTimer, &QTimer::timeout, this, &MainWindow::SamplePerfUiResponsiveness);
    mPerfUiTimer->start(100);

    // ── [PERF 계측 · §A-1/A-2 · QA-LT-01] 실제 '그리기 완료' 시점 포착 ──
    //  ScopePlot 은 TabRateScope 로 이동했다 → 그 탭의 scopeReplotted() 시그널을 RegisterDisplayTabs 에서
    //  OnScopeReplotted 에 연결해, (요청→페인트)·진짜 종단간(캡처→페인트)을 기록한다.

    // [탭 모듈] 신규 디스플레이 탭들을 생성·등록. 코어 DSP 불변(QA-MOD-01).
    RegisterDisplayTabs();

    // 오디오 소스 오케스트레이션(스레드·워커·버퍼)은 CaptureController 담당.
    //  워커가 채운 블록 → dataReady → HandleInputData(처리). 소스 종료 → 정지 핸들러.
    //  (cross-thread 는 워커→컨트롤러 큐 연결 1곳뿐, dataReady→HandleInputData 는 메인 스레드 직접)
    mCapture = new CaptureController(this);
    connect(mCapture, &CaptureController::dataReady,              this, &MainWindow::HandleInputData);
    connect(mCapture, &CaptureController::playbackDoneReadingFile, this, &MainWindow::HandlePlaybackDoneReadingFile);
    connect(mCapture, &CaptureController::simDone,                 this, &MainWindow::HandleSimDone);
}

// [탭 모듈 · QA-MOD-01] 기존 2개 탭(RateTab/SoundTab, MainWindow.ui 정의)에 더해 신규 탭
//  모듈을 TabManager 로 등록한다. 새 탭 추가 = TabView 상속 클래스 1개 + 아래 한 줄.
//  ("Split module" / "Restrict dependencies" 택틱 — 코어/기존 탭 수정 불필요)
void MainWindow::RegisterDisplayTabs(void)
{
    mTabManager = new TabManager(ui->GraphicsTabWidget, this);
    // Rate/Scope 를 첫 탭으로(기본 표시) — ScopePlot 이 기본으로 paint 되어 perf(disp_paint 등) 측정 유지.
    TabRateScope *rateScope = new TabRateScope(this);
    mTabManager->registerTab(rateScope);                        // rate 시계열 + 실시간 스코프(구 정적 RateTab)
    //  ScopePlot 의 실제 paint 완료 → perf 기록(상태는 MainWindow 잔류).
    connect(rateScope, &TabRateScope::scopeReplotted, this, &MainWindow::OnScopeReplotted);
    mTabManager->registerTab(new TabSoundPrint(this));          // 폴딩 사운드 이미지(구 정적 SoundTab)
    mTabManager->registerTab(new TabTraceDisplay(this));        // FR-TD
    mTabManager->registerTab(new TabVarioStability(this));      // FR-RAS
    mTabManager->registerTab(new TabSequenceDisplay(this));     // FR-MPS
    mTabManager->registerTab(new TabBeatNoiseScope(this));      // FR-BNS
    mTabManager->registerTab(new TabBeatErrorTrace(this));      // FR-BED
    mTabManager->registerTab(new TabLongTermPerformance(this)); // FR-LTP
    mTabManager->registerTab(new TabEscapementAnalyzer(this));  // FR-EAM
    mTabManager->registerTab(new TabSpectrogram(this));         // FR-TFS
    mTabManager->registerTab(new TabWaveformCompare(this));     // FR-WCD
    mTabManager->registerTab(new TabSyncSweepScope(this));      // FR-SMS
    mTabManager->registerTab(new TabFilterViews(this));         // FR-SFM(F0~F3)
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
    snap.totalSamples   = mLocalTotalSamplesWritten;
    snap.liftAngle      = (int)mLiftAngle;
    snap.mode           = ui->ModeComboBox->currentIndex();
    // RatePlot 시리즈(포인터는 이 호출 동안만 유효 → 탭이 복사). 구 mRateErrorEvents.x/yTic/Toc.
    snap.rateTicX = mEngine.ticX().constData(); snap.rateTicY = mEngine.ticY().constData(); snap.rateTicN = mEngine.ticX().size();
    snap.rateTocX = mEngine.tocX().constData(); snap.rateTocY = mEngine.tocY().constData(); snap.rateTocN = mEngine.tocX().size();
    snap.rateMaxPoints = mEngine.maxDataPoints();
    mTabManager->broadcastMeasurement(snap);
}

// [PERF 계측 · §A-1/A-2 · QA-LT-01] ScopePlot 이 '실제로 다 그려진' 직후 호출됨.
//  disp_paint_ms = (페인트 완료 − replot 요청) = 미뤄졌던 그리기 시간.
//  e2e_full_ms   = (페인트 완료 − 캡처)        = ★진짜 종단간★ = (캡처→요청) + (요청→페인트) 의 합.
void MainWindow::OnScopeReplotted()
{
    if (!mPerfReplotPending) return;     // ProcessSamples가 요청한 replot 에만 반응(잡음 replot 무시)
    mPerfReplotPending = false;
    double now = Perf::nowMs();
    Perf::log("A-2","QA-LT-01","disp_paint_ms", now - mPerfReplotRequestMs, "ms","");   // 요청→실제 페인트
    if (mPerfReplotLive && mPerfCaptureForReplotMs > 0.0)
        Perf::log("A-1","QA-LT-01","e2e_full_ms", now - mPerfCaptureForReplotMs, "ms", "paint_included");

    // ── [PERF 계측 · §F-1 · QA-SC-01] 프레임(실제 화면 갱신) 비율 ──
    //  paint_fps = 초당 실제 paint 수(화면이 실제로 갱신된 횟수). 부하 시 떨어지면 frame drop.
    //  extra 의 replot_req = 요청 수. 요청>paint 는 정상(rpQueuedReplot 코얼레싱), 둘 다 같이 떨어지면 과부하.
    mPaintCount++;
    if (!mPaintHave) { mPaintLastEmitMs = now; mPaintHave = true; }
    if (now - mPaintLastEmitMs >= 1000.0) {
        double sec = (now - mPaintLastEmitMs) / 1000.0;
        Perf::log("F-1","QA-SC-01","paint_fps", (double)mPaintCount / sec, "frame/s",
                  QString("replot_req=%1").arg(mReplotReqCount));
        mPaintCount = 0; mReplotReqCount = 0; mPaintLastEmitMs = now;
    }
}

// [PERF 계측 · §A-3 · QA-RT-01] 100ms 하트비트의 실제 간격에서 100ms를 뺀 '초과 지연'을 기록.
//  값이 클수록 메인 스레드가 막혀 UI가 늦게 반응한다는 뜻.
void MainWindow::SamplePerfUiResponsiveness()
{
    double now = Perf::nowMs();
    if (mPerfUiHave) {
        double lag = (now - mPerfUiLastMs) - 100.0;   // 명목 주기(100ms) 대비 초과분
        if (lag < 0.0) lag = 0.0;
        Perf::log("A-3","QA-RT-01","ui_loop_lag_ms", lag, "ms","");
    }
    mPerfUiLastMs = now;
    mPerfUiHave = true;
}

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

void MainWindow::A_Event(double A_EventTime,bool haveValidBPH, double BPH)
{
  // 측정 계산만 — RatePlot 그리기는 TabRateScope(onMeasurement, snapshot 의 rate 시리즈)가 담당.
  mEngine.onAEvent(A_EventTime,haveValidBPH,BPH);
}
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

    // ── [PERF 계측 · §G-1 · QA-CO-01] 측정 정확도: 측정값 - Sim 설정(정답) 오차 ──
    //  Sim 모드에서만 유효(설정값을 알고 있으므로). Rate/BeatError/Amplitude 의
    //  (측정값 - 설정값)을 기록 → 목표(±1 s/d, ±0.1 ms, ±5°) 달성 여부 판단.
    if (mSimActive) {
        if (res.rateValid)
            Perf::log("G-1","QA-CO-01","rate_err_s_per_d",
                      res.rateSecPerDay - mLastSimCfg.rate_error_s_per_day, "s/d",
                      QString("meas=%1;set=%2").arg(res.rateSecPerDay,0,'f',2)
                                               .arg(mLastSimCfg.rate_error_s_per_day,0,'f',2));
        if (res.beatErrorValid) {
            double measBE = res.beatErrorMs;
            double setBE  = qAbs(mLastSimCfg.beat_error_ms);   // cfg 는 부호 반전 저장 → 크기 비교
            Perf::log("G-1","QA-CO-01","beaterr_err_ms", measBE - setBE, "ms",
                      QString("meas=%1;set=%2").arg(measBE,0,'f',3).arg(setBE,0,'f',3));
        }
        if (res.amplitudeValid) {
            double measAmp = res.amplitudeDeg;
            Perf::log("G-1","QA-CO-01","amp_err_deg", measAmp - mLastSimCfg.watch_amplitude_degrees, "deg",
                      QString("meas=%1;set=%2").arg(measAmp,0,'f',1).arg(mLastSimCfg.watch_amplitude_degrees,0,'f',1));
        }
        // [§G-2 · QA-AC-01] 검출률 분모(정답 비트 누적 수). a_match/c_match 합과 비교해 검출률 산출.
        Perf::log("G-2","QA-AC-01","gt_total", (double)mLocalGtTotal, "beats","");
    }

    // [탭 모듈] 현재 측정값을 모든 디스플레이 탭에 스냅샷으로 게시.
    PublishMeasurementToTabs();
}
void MainWindow::C_Event(double C_EventTime,bool haveValidBPH, double BPH)
{
  mEngine.onCEvent(C_EventTime,haveValidBPH,BPH);
  DisplayResults();
}
void MainWindow::CreateDectectors(void)
{
    DeleteDectectors(); // Delete old ones if present
    tg_config_default(&mCfg);
    mCfg.sample_rate     = mCurrentSamplesPerSecond;
    if (ui->BPHComboBox->currentIndex()==0)
        mCfg.bph_mode= TG_BPH_MODE_AUTO;
    else
    {
     mCfg.bph_mode=TG_BPH_MODE_MANUAL;
     mCfg.manual_bph=ManualAutoBPH[ui->BPHComboBox->currentIndex()];
    }
    //mCfg.onset_fraction_init=0.2;
    mCfg.suppress_pre_sync_events=true;

    mCfg.hpf_cutoff_hz=ui->HighLineEdit->text().toDouble();

    mCtx = tg_init(&mCfg);
    if (mCtx==NULL)
        throw std::runtime_error("allocation failed-could not initialize detector");

    qInfo()<<"Rate "<<mCurrentSamplesPerSecond;

    mInputBlock = (float *)malloc(DETECTOR_NUMBER_OF_SAMPLES * SAMPLE_SIZE);
    if (!mInputBlock)
    {
        tg_destroy(mCtx);
        mCtx=NULL;
        throw std::runtime_error("allocation failed");
    }
}
void MainWindow::DeleteDectectors(void)
{
  if (mInputBlock) free(mInputBlock);
  mInputBlock=NULL;
  if (mCtx) tg_destroy(mCtx);
  mCtx=NULL;
}

// RatePlot/ScopePlot 그래프 설정(구 CreateGraphs)은 TabRateScope::setupPlots 로 이동했다.

MainWindow::~MainWindow()
{
    delete ui;
}

// 오디오 소스(스레드·워커·버퍼) 관리는 CaptureController 로 이동했다.
//  MainWindow 는 mCapture->startLive/Playback/Sim · stopX 로 제어하고, dataReady 신호를 받아 처리한다.

// 스코프 마커 헬퍼(AddVerticalMarker/AddText/AddHorizontalMarker*·RemoveMarkersAndText)는
//  ScopePlot 과 함께 TabRateScope 로 이동했다.

void MainWindow::HandleInputData(TMasterAudioDataRaw *SharedDataPtr)
{
        SharedDataPtr->Mutex.lock();
        mLocalWriteIndex=SharedDataPtr->WriteIndex;
        mLocalTotalSamplesWritten=SharedDataPtr->TotalSamplesWritten;
        // [PERF 계측 · §A-1/A-2 · QA-LT-01] 동일 Mutex 안에서 최신 블록 캡처 시각/드롭 추정을
        //   원자적으로 스냅샷. (이후 ProcessSamples에서 표시 시각과 비교해 지연 산출)
        mLocalLastBlockCaptureMs=SharedDataPtr->LastBlockCaptureMs;
        mLocalDroppedSamples=SharedDataPtr->DroppedSampleEstimate;
        // [PERF 계측 · §E/§G-2] Sim 모드에서만 정답(GT) 이벤트 링을 동일 Mutex 안에서 스냅샷
        if (ui->ModeComboBox->currentIndex()==SIM) {
            memcpy(mLocalGt, SharedDataPtr->GtBeats, sizeof(mLocalGt));
            mLocalGtHead  = SharedDataPtr->GtHead;
            mLocalGtTotal = SharedDataPtr->GtTotal;
            mLocalGtValid = true;
        } else {
            mLocalGtValid = false;
        }
        SharedDataPtr->Mutex.unlock();

        ProcessSamples(SharedDataPtr);
        if ((mBackgroundLastFPS!=SharedDataPtr->FPS) ||
            (mBackgroundLastSPS!=SharedDataPtr->SPS) ||
            (mBackgroundLastSPF!=SharedDataPtr->SPF) ||
            (mForegroundLastFPS!=mForegroundFPS) ||
            (mForegroundLastSPS!=mForegroundSPS) ||
            (mForegroundLastSPF!=mForegroundSPF))
        {
            mBackgroundLastFPS=SharedDataPtr->FPS;
            mBackgroundLastSPS=SharedDataPtr->SPS;
            mBackgroundLastSPF=SharedDataPtr->SPF;
            mForegroundLastFPS=mForegroundFPS;
            mForegroundLastSPS=mForegroundSPS;
            mForegroundLastSPF=mForegroundSPF;

            statusBar()->showMessage(
                QString("Backgroud Audio Thread Average - FPS:%1, SPS:%2, SPF: %3 Foregroud Audio Handler Average - FPS:%4, SPS:%5, SPF: %6")
                    .arg(mBackgroundLastFPS, 0, 'f', 0)
                    .arg(mBackgroundLastSPS, 0, 'f', 0)
                    .arg(mBackgroundLastSPF, 0, 'f', 0)
                    .arg(mForegroundLastFPS, 0, 'f', 0)
                    .arg(mForegroundLastSPS, 0, 'f', 0)
                    .arg(mForegroundLastSPF, 0, 'f', 0));
        }

       // qDebug() << "Main thread: handleResults slot is running in thread" << QThread::currentThreadId()<<" "<<count;
}

// HandleAudioInput/HandlePlaybackInput/HandleSimInput 은 제거 — CaptureController::dataReady 가
//  직접 HandleInputData(raw) 로 연결된다(생성자 배선 참조).
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
    mSimActive=false;   // [PERF 계측 · §G-1] Sim 측정 종료 → 측정값 비교 중단
    SetGuiStopMode();
    if (ui->ModeComboBox->currentIndex()==SIM)
    {
        SetAudioDevice(mDeviceNameBeforePlaybackOrSim);
        SetAudioRate(mRateBeforePlaybackOrSim);
    }
    AudioCloseCheck();
    statusBar()->showMessage("Stopped");
}
void MainWindow::ProcessSamples(TMasterAudioDataRaw *SharedDataPtr)
{
    // 측정 엔진에 현재 설정 반영(샘플레이트·평균구간·lift angle) — 이벤트 계산 전에 1회.
    mEngine.setConfig(mCurrentSamplesPerSecond, mAveragingPeriod, (int)mLiftAngle);
    int    SamplesToAdd=mLocalTotalSamplesWritten-SharedDataPtr->MainThrd_LastTotalSamplesWritten;

    int slice;
    if (!mForegroundTimerStarted)
    {
        mForegroundTimer.restart();
        mForegroundTimerStarted=true;
        mForegroundLastTime=0.0;
        mForegroundFrameCount=0;
        mForegroundSampleCount=0;
    }
    if (SamplesToAdd>0)
    {
        // ── [PERF 계측 · §A-2 · QA-LT-01] 처리 단계 시작 시각 + 백로그 ─────────────
        //  backlog_samples = 이번 호출 시점에 아직 처리 못한 누적 샘플 수(대기량).
        //  cap2proc_latency_ms = (처리 시작 - 최신 블록 캡처 시각) = 캡처→처리 지연(Live).
        //  ※ Live 모드에서만 캡처 시각이 의미 있음(Playback/Sim 은 인위적 타이밍).
        const double perfProcStartMs = Perf::nowMs();
        const bool   perfIsLive      = (ui->ModeComboBox->currentIndex()==LIVE);
        Perf::log("A-2","QA-LT-01","backlog_samples",(double)SamplesToAdd,"samp",
                  perfIsLive ? "mode=Live" : "mode=PlaybackOrSim");
        if (perfIsLive && mLocalLastBlockCaptureMs>0.0)
            Perf::log("A-2","QA-LT-01","cap2proc_latency_ms",
                      perfProcStartMs - mLocalLastBlockCaptureMs, "ms",
                      QString("backlog=%1;drop_est=%2").arg(SamplesToAdd).arg(mLocalDroppedSamples));

        while (SamplesToAdd>0)
        {
         if ( SamplesToAdd>DETECTOR_NUMBER_OF_SAMPLES) slice= DETECTOR_NUMBER_OF_SAMPLES;
         else slice=SamplesToAdd;

         for (int i=0;i<slice;i++)
          {
           mInputBlock[i]=SharedDataPtr->Samples[SharedDataPtr->MainThrd_LastWriteIndex];
           SharedDataPtr->MainThrd_LastWriteIndex=(SharedDataPtr->MainThrd_LastWriteIndex+1)%SharedDataPtr->NumberOfAudioSamples;
          }
          if (mWavWriter) mWavWriter->write(mInputBlock,slice);

          tg_result_t r;
          if (tg_process(mCtx,mInputBlock, slice, &r) != 0) {
              qInfo()<<"tg_process failed";
              return ;
          }

          // ── [PERF 계측 · §A-4 · QA-AC-04/US-01/LT-03] 결함/관측 이벤트 발생 시각 ──
          //  sync_lost=동기 상실(신호 손실·과잡음 신호), detector_reset=신호 레짐 급변.
          //  결함 주입 시각과 이 로그 시각의 차이로 '결함→인지 지연(≤2초)'을 산출한다.
          //  (드롭/누락 카운터의 화면 반영(≤5초)은 §B-1 로그가 2초 주기로 관찰 보장)
          if (r.sync_lost_event)      Perf::log("A-4","QA-US-01","fault_sync_lost", 1, "event","");
          if (r.sync_acquired_event)  Perf::log("A-4","QA-US-01","sync_acquired",   1, "event","");
          if (r.detector_reset_event) Perf::log("A-4","QA-US-01","detector_reset",  1, "event","");

          // [탭] 엔벨로프/임계선 + 이벤트 마커(ScopePlot) 렌더링은 TabRateScope.onWave 가 담당.
          for (int i=0;i<r.num_events;i++)
             {
               double val;
               if (r.events[i].type==TG_EVENT_A)
               {
                    val=r.events[i].sample_index+r.events[i].sub_sample_offset;
                    A_Event(val,(r.sync_status==TG_SYNC_SYNCED),r.detected_bph);
                    // ── [PERF 계측 · §E-2/§G-2 · QA-AC-02/AC-01] 검출 A(onset) vs 정답 A 대조 ──
                    //  검출된 A 샘플위치와 가장 가까운 정답 A 의 차이 = onset 식별 오차(ms).
                    //  허용창(반 비트) 안이면 검출 성공(a_match), 아니면 a_unmatched(검출률/FP 계산용).
                    if (mLocalGtValid && mLastSimCfg.bph>0.0) {
                        double tol = 0.5 * ((double)mCurrentSamplesPerSecond * 3600.0 / mLastSimCfg.bph);
                        double bestErr=0.0; bool found=false;
                        for (unsigned k=0;k<GT_EVENT_RING;k++){
                            uint64_t a=mLocalGt[k].a_sample; if(a==0) continue;
                            double e=val-(double)a;
                            if(!found || qAbs(e)<qAbs(bestErr)){bestErr=e;found=true;}
                        }
                        if (found && qAbs(bestErr)<tol){
                            Perf::log("E-2","QA-AC-02","onset_err_ms", bestErr*1000.0/mCurrentSamplesPerSecond,"ms","");
                            Perf::log("G-2","QA-AC-01","a_match",1,"event","");
                        } else Perf::log("G-2","QA-AC-01","a_unmatched",1,"event","");
                    }
               }
               else if (r.events[i].type==TG_EVENT_C)
               {
                   if(ui->UseConsetCheckBox->isChecked())
                   {
                       if (r.events[i].onset_valid)
                       {
                           val=r.events[i].onset_sample_index+r.events[i].onset_sub_sample_offset;
                       }
                       else
                       {
                           qInfo()<< "Invalid C Onset using C peak";
                           val=r.events[i].sample_index+r.events[i].sub_sample_offset; // Use C PEAK
                       }
                   }
                   else val=r.events[i].sample_index+r.events[i].sub_sample_offset; // C PEAK

                   C_Event(val,(r.sync_status==TG_SYNC_SYNCED),r.detected_bph);
                   // ── [PERF 계측 · §E-2/§G-2 · QA-AC-02/AC-01] 검출 C vs 정답 C 대조 ──
                   //  검출된 C 샘플위치(onset 또는 peak)와 가장 가까운 정답 C 의 차이 = 식별 오차(ms).
                   if (mLocalGtValid && mLastSimCfg.bph>0.0) {
                       double tol = 0.5 * ((double)mCurrentSamplesPerSecond * 3600.0 / mLastSimCfg.bph);
                       double bestErr=0.0; bool found=false;
                       for (unsigned k=0;k<GT_EVENT_RING;k++){
                           uint64_t c=mLocalGt[k].c_sample; if(c==0) continue;
                           double e=val-(double)c;
                           if(!found || qAbs(e)<qAbs(bestErr)){bestErr=e;found=true;}
                       }
                       if (found && qAbs(bestErr)<tol){
                           Perf::log("E-2","QA-AC-02","peak_err_ms", bestErr*1000.0/mCurrentSamplesPerSecond,"ms","");
                           Perf::log("G-2","QA-AC-01","c_match",1,"event","");
                       } else Perf::log("G-2","QA-AC-01","c_unmatched",1,"event","");
                   }
               }
               else qInfo()<< "Unkown Event Type";

             }

          // [탭 모듈] 이 슬라이스의 엔벨로프 + 검출 이벤트(A/C)를 스코프 계열 탭에 게시.
          //  (포인터는 이 호출 동안만 유효 → 각 탭이 WaveBuffer 로 복사)
          if (mTabManager) {
              QVarLengthArray<WaveEvent, 32> wevs;
              const bool useConset = ui->UseConsetCheckBox->isChecked();
              for (size_t ei = 0; ei < r.num_events; ++ei) {
                  // 표시용 마커 위치(markSample): A=검출위치, C=UseConset+onset_valid면 onset, 아니면 peak.
                  uint64_t mark = r.events[ei].sample_index;
                  if (r.events[ei].type == TG_EVENT_C && useConset && r.events[ei].onset_valid)
                      mark = r.events[ei].onset_sample_index;
                  wevs.append(WaveEvent{ r.events[ei].sample_index,
                                         (int)r.events[ei].type,
                                         r.events[ei].peak_value,
                                         mark });
              }
              WaveBlock wb;
              wb.env          = r.processed_pcm;
              wb.n            = (int)r.processed_pcm_len;
              wb.startSample  = r.processed_pcm_start_sample;
              wb.sampleRateHz = mCurrentSamplesPerSecond;
              wb.bph          = r.detected_bph;
              wb.synced       = (r.sync_status == TG_SYNC_SYNCED);
              wb.events       = wevs.data();
              wb.numEvents    = wevs.size();
              wb.raw          = mInputBlock;       // 정류 전 원신호(F0~F3 필터 뷰용)
              wb.rawN         = slice;
              wb.rawStart     = mInputAbsSample;
              wb.onsetThreshold = r.onset_threshold;   // Escapement threshold 선 출처
              mTabManager->broadcastWave(wb);
          }
          mInputAbsSample += (uint64_t)slice;      // 다음 슬라이스의 원신호 시작 인덱스

        mForegroundSampleCount+=slice;
        SamplesToAdd=SamplesToAdd-slice;
        }

        SharedDataPtr->MainThrd_LastTotalSamplesWritten=mLocalTotalSamplesWritten;
        // [탭] ScopePlot 의 purge/축/replot 은 TabRateScope.onWave 가 담당.

        // ── [PERF 계측 · §A-2/§A-1 · QA-LT-01] 표시 완료 시각 → 지연 산출 ──────────
        //  proc2disp_latency_ms = (표시 완료 - 처리 시작) = 처리→표시(그래프 replot) 지연.
        //  e2e_latency_ms       = (표시 완료 - 최신 블록 캡처) = 종단간 지연(Live, ≤50ms 목표).
        const double perfDispMs = Perf::nowMs();
        Perf::log("A-2","QA-LT-01","proc2disp_latency_ms", perfDispMs - perfProcStartMs, "ms","");
        if (perfIsLive && mLocalLastBlockCaptureMs>0.0)
            Perf::log("A-1","QA-LT-01","e2e_latency_ms", perfDispMs - mLocalLastBlockCaptureMs, "ms",
                      QString("set_sps=%1").arg(mCurrentSamplesPerSecond));

        // [PERF 계측 · §A-1/A-2] 이 replot 요청 시점·캡처시각을 보관 → afterReplot(OnScopeReplotted)에서
        //  실제 페인트 완료 시각과 비교해 disp_paint_ms·e2e_full_ms 산출.
        mPerfReplotRequestMs    = perfDispMs;
        mPerfCaptureForReplotMs = mLocalLastBlockCaptureMs;
        mPerfReplotLive         = perfIsLive;
        mPerfReplotPending      = true;
        mReplotReqCount++;   // [F-1] replot 요청 수(코얼레싱 대비 paint 수와 비교)

        mForegroundFrameCount++;
        double CurrentTime;
        CurrentTime = mForegroundTimer.elapsed()/1000.0;

        if (CurrentTime-mForegroundLastTime > 2) // average fps over 2 seconds
        {
            double fdelta;
            fdelta=CurrentTime-mForegroundLastTime;
            mForegroundFPS=mForegroundFrameCount/fdelta;
            mForegroundSPS=mForegroundSampleCount/fdelta;
            mForegroundSPF=mForegroundSampleCount/mForegroundFrameCount;
            mForegroundLastTime=CurrentTime;
            mForegroundFrameCount=0;
            mForegroundSampleCount=0;

            // [PERF 계측 · §B-3/§A-3 · QA-RT-01] 전경(핸들러+렌더) 실효 처리량/프레임율.
            //  fg_fps 가 떨어지면 렌더 부하로 표시가 밀린다는 신호(§F-1과 연계).
            Perf::log("B-3","QA-RT-01","fg_sps", mForegroundSPS, "samp/s","");
            Perf::log("B-3","QA-RT-01","fg_fps", mForegroundFPS, "frame/s","");
            Perf::log("B-3","QA-RT-01","fg_spf", mForegroundSPF, "samp/frame","");
        }
    }
}
// 스코프 히스토리 정리(구 PurgeHistory)는 TabRateScope::purgeHistory 로 이동했다.
void MainWindow::Reset(void)
{
    qInfo()<<"RESET";
    // Rate/Scope·Sound Print 렌더링은 각 탭(TabRateScope/TabSoundPrint)이 담당.
    //  세션 리셋은 broadcastReset 으로 전파되어 각 탭이 자기 그래프/누적을 비운다.

    mInputAbsSample=0;   // [탭 모듈] 원신호 게시 인덱스 리셋

    // [탭 모듈] 세션 리셋을 모든 디스플레이 탭에 전파(누적 데이터·그래프 비움).
    if (mTabManager) mTabManager->broadcastReset();

    CreateDectectors();
    EventsReset();

    mBackgroundLastFPS=0.0;
    mBackgroundLastSPF=0.0;
    mBackgroundLastSPS=0.0;
    mForegroundTimerStarted=false;
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
        mWavWriter->close();
        delete mWavWriter;
        mWavWriter=NULL;
    }
}

bool MainWindow::OpenFile(const QString &FileName)
{
    QFile *file = new QFile(FileName);
    TWaveHeader header;
    if (!file->exists()) {
        statusBar()->showMessage(tr("File %1 could not be opened").arg(QDir::toNativeSeparators(FileName)));
        delete file;
        return false;
    }

    QFileInfo fileInfo(*file);
    mCurrentDir = fileInfo.dir();

    if (!file->open(QIODevice::ReadOnly))
    {
        statusBar()->showMessage(tr("File %1 could not be opened")
                                     .arg(QDir::toNativeSeparators(FileName)));
        delete file;
        return false;
    }

    QDataStream in(file);
    in.setByteOrder(QDataStream::LittleEndian); // WAV is Little Endian

    file->read(header.riffId, 4);
    in >> header.fileSize;
    file->read(header.waveId, 4);
    file->read(header.fmtId, 4);
    in >> header.fmtSize;
    in >> header.audioFormat;
    in >> header.numChannels;
    in >> header.sampleRate;
    in >> header.byteRate;
    in >> header.blockAlign;
    in >> header.bitsPerSample;

    // Skip any extra fmt bytes if fmtSize > 16
    if (header.fmtSize > 16) file->seek(file->pos() + (header.fmtSize - 16));

    // Look for "data" chunk (it might not be immediately after fmt)
    char chunkId[4];
    while (!file->atEnd()) {
        file->read(chunkId, 4);
        uint32_t chunkSize;
        in >> chunkSize;
        if (qstrncmp(chunkId, "data", 4) == 0) {
            header.dataSize = chunkSize;
            break;
        }
        file->seek(file->pos() + chunkSize);
    }
    GetAudioRate(mRateBeforePlaybackOrSim);
    GetAudioDevice(mDeviceNameBeforePlaybackOrSim);
    if (!SetAudioDevice(PLAYBACK_OR_SIM_PCM))
    {
      qInfo()<< "SetAudioDevice Failed";
    }
    if (!SetAudioRate(header.sampleRate))
    {
        qInfo()<< "SetAudioRate Failed";
    }

    if (qstrncmp(header.riffId, "RIFF", 4) != 0 || (header.sampleRate!=mCurrentSamplesPerSecond) ||
        (header.numChannels!=1)|| (header.bitsPerSample != 32)||
        (header.audioFormat != 3))
      {
        statusBar()->showMessage(tr("File %1 Not a 48K, single channel 32-bit Float WAV file")
                                     .arg(FileName));
        file->close();
        delete file;
        QMessageBox::critical(this, "Error", "Invalid PCM Wave File");
        return false;
      }

    file->close();
    delete file;
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
void   MainWindow::LiveStart(void)
{
    if (!RecordSessionCheck()) return;
    Reset();   // 세션 리셋(탭·검출기·측정) — 구 StartAudioThread 가 하던 것
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
    mCapture->startPlayback(fileDialog.selectedFiles().constFirst(), mCurrentSamplesPerSecond);
    SetGuiRunMode();
    statusBar()->showMessage("Running");
}
void   MainWindow::SimStart(void)
{
    WatchSynthStreamConfig cfg;
    if (ui->RealisticCheckBox->isChecked())
        watch_synth_stream_realistic_config(&cfg);
    else watch_synth_stream_clean_config(&cfg);
    cfg.bph = SimBPH[ui->SimBPHComboBox->currentIndex()];
    cfg.sample_rate_hz = mAvalableRates[ui->SampleRatesComboBox->currentIndex()];
    cfg.beat_error_ms = -ui->SimBeatErrorSpinBox->value();
    cfg.pcm_peak_amplitude = 0.40;       /* normalized float PCM digital output level */
    cfg.watch_amplitude_degrees = ui->SimAmplitudeSpinBox->value();
    cfg.lift_angle_degrees = ui->LiftAngleSpinBox->value();
    cfg.rate_error_s_per_day=ui->SimErrorRateSpinBox->value();

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
    // [PERF 계측 · §G-1 · QA-CO-01] Sim 정답 설정값 보관 — DisplayResults에서 측정값과 대비.
    mLastSimCfg = cfg;
    mSimActive  = true;
    Reset();
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
    qInfo()<<"Lift Angle Value="<<mLiftAngle;
}
void MainWindow::on_AveragingPeriodComboBox_currentIndexChanged(int index)
{
    mAveragingPeriod=AveragingPeriodList[ui->AveragingPeriodComboBox->currentIndex()];
    qInfo()<<"Averaging Period Value="<<mAveragingPeriod;
}
void MainWindow::on_MicrophoneHorizontalSlider_sliderMoved(int position)
{
    mCapture->setInputVolume(ui->MicrophoneHorizontalSlider->sliderPosition()/1000.0);
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