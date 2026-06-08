#include <QtGlobal>
#include "MainWindow.h"
#include "./ui_MainWindow.h"
#include "WaveHeader.h"
#include "TfliteConfig.h"

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
#define  SND_PIXEL_SIZE                      3
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
    mLocalGraphTicks=0;
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
    ui->SoundImage->CreateImage();

    //QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    //ui->Results->setFont(fixedFont);

    // MFCC extractor setup
    MfccConfig mfccCfg;
    mfccCfg.sampleRate      = mCurrentSamplesPerSecond;
    mfccCfg.frameLengthMs   = 25.0f;
    mfccCfg.frameHopMs      = 10.0f;
    mfccCfg.numMelFilters   = 26;
    mfccCfg.numCoefficients = 13;
    mMfccExtractor.reconfigure(mfccCfg);

    // TFLite runner
    mTfliteRunner = new TfliteRunner(TFLITE_MODEL_PATH);
    if (!mTfliteRunner->isValid())
        qWarning() << "TfliteRunner: model failed to load from" << TFLITE_MODEL_PATH;

    mMfccExtractor.setCallback([this](const std::vector<float>& mfcc) {
        // Accumulate frames into the sliding window buffer
        mMfccFrameBuffer.insert(mMfccFrameBuffer.end(), mfcc.begin(), mfcc.end());

        // Run inference once we have kMfccNumFrames complete frames
        const int required = kMfccNumFrames * kMfccNumCoeffs;
        if (static_cast<int>(mMfccFrameBuffer.size()) >= required) {
            if (mTfliteRunner && mTfliteRunner->isValid()) {
                std::vector<float> inputWindow(
                    mMfccFrameBuffer.end() - required, mMfccFrameBuffer.end());
                std::vector<float> probs = mTfliteRunner->run(inputWindow);
                if (!probs.empty()) {
                    qInfo("TFLite inference: [%.4f, %.4f, %.4f, %.4f]",
                          static_cast<double>(probs[0]),
                          static_cast<double>(probs[1]),
                          static_cast<double>(probs[2]),
                          static_cast<double>(probs[3]));
                }
            }
            // Keep only the last (kMfccNumFrames - 1) frames for the next window
            const int keep = (kMfccNumFrames - 1) * kMfccNumCoeffs;
            mMfccFrameBuffer.erase(
                mMfccFrameBuffer.begin(),
                mMfccFrameBuffer.begin() + static_cast<int>(mMfccFrameBuffer.size()) - keep);
        }
    });

    CreateEvents();
    LoadBPH();
    LoadSimBPH();
    LoadAudioDevices();
    CreateGraphs();
    LoadAverageingPeriod();
    DisplayResults();
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
void   MainWindow::CreateEvents(void)
{
    mAmplitudeEvents.Have_A_Event=false;
    mAmplitudeEvents.Amplitude_Tic_Valid=false;
    mAmplitudeEvents.RollAmplitude= new RollingAverage(10);

    mBeatErrorEvents.BeatErrorIdx=0;
    mBeatErrorEvents.RollBeatError= new RollingAverage(10);

    mRateErrorEvents.BPH_Valid=false;
    mRateErrorEvents.MaxTicTocDataPoints=ERROR_RATE_X_DATA_POINTS;
    mRateErrorEvents.xTicIndex=0;
    mRateErrorEvents.xTocIndex=0;
    mRateErrorEvents.HaveStartTime=false;
    mRateErrorEvents.HaveZeroOffset=false;
    mRateErrorEvents.ZeroOffsetValue=0.0;
    mRateErrorEvents.xTic.reserve(mRateErrorEvents.MaxTicTocDataPoints);
    mRateErrorEvents.xToc.reserve(mRateErrorEvents.MaxTicTocDataPoints);
    mRateErrorEvents.yTic.reserve(mRateErrorEvents.MaxTicTocDataPoints);
    mRateErrorEvents.yToc.reserve(mRateErrorEvents.MaxTicTocDataPoints);
    mRateErrorEvents.RlsTicRate=new RollingLeastSquares(RLS_WINDOW_INIT);
    mRateErrorEvents.RlsTocRate=new RollingLeastSquares(RLS_WINDOW_INIT);
    mRateErrorEvents.RlsRateValid=false;
}

void MainWindow::EventsReset(void)
{
    mAmplitudeEvents.Have_A_Event=false;
    mAmplitudeEvents.Amplitude_Tic_Valid=false;
    mAmplitudeEvents.RollAmplitude->Reset();

    mBeatErrorEvents.BeatErrorIdx=0;
    mBeatErrorEvents.RollBeatError->Reset();

    mRateErrorEvents.BPH_Valid=false;
    mRateErrorEvents.xTicIndex=0;
    mRateErrorEvents.xTocIndex=0;
    mRateErrorEvents.StartTime=0;
    mRateErrorEvents.HaveStartTime=false;
    mRateErrorEvents.HaveZeroOffset=false;
    mRateErrorEvents.ZeroOffsetValue=0.0;
    mRateErrorEvents.xTic.clear();
    mRateErrorEvents.xToc.clear();
    mRateErrorEvents.yTic.clear();
    mRateErrorEvents.yToc.clear();
    mRateErrorEvents.RlsTicRate->Reset();
    mRateErrorEvents.RlsTocRate->Reset();
    mRateErrorEvents.RlsRateValid=false;

    ui->RatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    ui->RatePlot->xAxis->setRange(0, mRateErrorEvents.MaxTicTocDataPoints);

    for (int i=0;i<ui->RatePlot->graphCount();i++)
    {
        ui->RatePlot->graph(i)->data()->clear();
    }
    ui->RatePlot->clearItems();
    ui->RatePlot->replot();
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

double MainWindow::WrapInToRange(double number, double lower_bound, double upper_bound) {
    double range_size = upper_bound - lower_bound;
    double shifted = number - lower_bound;
    double wrapped = fmod(shifted, range_size);
    if (wrapped < 0) {
        wrapped += range_size;
    }
    return wrapped + lower_bound;
}
void  MainWindow::AddOrOverwrite(QVector<double>& xvec,QVector<double>& yvec, double value, int maxS, int& index)
{
    if (yvec.size() < maxS) {
        yvec.append(value); // Growing
        xvec.append(index); // Never Changes once added
        index = (index + 1) % maxS; // Circular pointer
    } else {
        yvec[index] = value; // Overwriting
        index = (index + 1) % maxS; // Circular pointer
    }
}
#define TIC 0
#define TOC 1
void MainWindow::ComputeRateError(double A_EventTime,bool haveValidBPH, double BPH)
{
   if ((!haveValidBPH) && (mRateErrorEvents.HaveStartTime))
    {
     mRateErrorEvents.HaveStartTime=false;
     mRateErrorEvents.BPH_Valid=false;

       //TODO More reset needed
    }
    else if ((haveValidBPH) && (!mRateErrorEvents.HaveStartTime))
    {
        mRateErrorEvents.HaveStartTime=true;
        mRateErrorEvents.TicTocBeatNumber=0;
        mRateErrorEvents.BPH_Valid=true;
        mRateErrorEvents.BPH=BPH;
        mRateErrorEvents.StartTime=A_EventTime/((double)mCurrentSamplesPerSecond);
        mRateErrorEvents.HaveZeroOffset=false;
        mRateErrorEvents.ZeroOffsetValue=0.0;
        mRateErrorEvents.RlsRateValid=false;
        mRateErrorEvents.WatchHertz=mRateErrorEvents.BPH/3600;
        mRateErrorEvents.RlsTicRate->Resize(mAveragingPeriod*mRateErrorEvents.WatchHertz);
        mRateErrorEvents.RlsTocRate->Resize(mAveragingPeriod*mRateErrorEvents.WatchHertz);
        mRateErrorEvents.RlsTicRate->Reset();
        mRateErrorEvents.RlsTocRate->Reset();

        mBeatErrorEvents.RollBeatError->Reset();
        mAmplitudeEvents.RollAmplitude->Reset();
    }
    if ((haveValidBPH) && (mRateErrorEvents.HaveStartTime))
    {
        double    InstTimingError;
        double    InstTimingErrorMs;
        double    ExpectedTimeTarget;
        double    ExpectedTimeTargetSamePhase;
        double    TimeMeasured;
        int       TicOrToc;

        TimeMeasured=A_EventTime/((double)mCurrentSamplesPerSecond);
        ExpectedTimeTarget =3600.0f/BPH;

        mRateErrorEvents.TicTocBeatNumber++;

        // This is not a good idea ----> TicTocBeatNumber=qRound64((TimeMeasured-mRateErrorEvents.StartTime)/ExpectedTimeTarget);
        TicOrToc=((mRateErrorEvents.TicTocBeatNumber-1)&1);

        InstTimingError=(mRateErrorEvents.StartTime+mRateErrorEvents.TicTocBeatNumber*ExpectedTimeTarget)-TimeMeasured;
        InstTimingErrorMs=InstTimingError*1000.00; // to Miliiseconds
        if (!mRateErrorEvents.HaveZeroOffset)
        {
            mRateErrorEvents.HaveZeroOffset=true;
            mRateErrorEvents.ZeroOffsetValue=-InstTimingErrorMs;
        }
        InstTimingErrorMs=InstTimingErrorMs+mRateErrorEvents.ZeroOffsetValue;

        double WrappedRateError=WrapInToRange(InstTimingErrorMs,-ERROR_RATE_Y_SCALE,ERROR_RATE_Y_SCALE);
        //qInfo()<<"Error "<<InstTimingError<<"Wrap "<<WrappedError;
        if (TicOrToc==TIC)
        {
            mRateErrorEvents.RlsTicRate->AddPoint(TimeMeasured,InstTimingError);
            AddOrOverwrite(mRateErrorEvents.xTic,mRateErrorEvents.yTic,WrappedRateError,mRateErrorEvents.MaxTicTocDataPoints,mRateErrorEvents.xTicIndex);
            ui->RatePlot->graph(0)->setData(mRateErrorEvents.xTic, mRateErrorEvents.yTic);
        }
        else // else TOC
        {
            mRateErrorEvents.RlsTocRate->AddPoint(TimeMeasured,InstTimingError);
            AddOrOverwrite(mRateErrorEvents.xToc,mRateErrorEvents.yToc,WrappedRateError,mRateErrorEvents.MaxTicTocDataPoints,mRateErrorEvents.xTocIndex);
            ui->RatePlot->graph(1)->setData(mRateErrorEvents.xToc, mRateErrorEvents.yToc);
        }

        ui->RatePlot->replot(QCustomPlot::rpQueuedReplot);
        if (TicOrToc==TOC)
        {
              double SlopeTic,RlsTic, SlopeToc,RlsToc;

            if ((mRateErrorEvents.RlsTicRate->GetRate(SlopeTic)) &&
                (mRateErrorEvents.RlsTocRate->GetRate(SlopeToc)))
            {
                RlsTic=SlopeTic*86400.00;
                RlsToc=SlopeToc*86400.00;
                mRateErrorEvents.RlsRate=(RlsTic+RlsToc)/2.0;
                mRateErrorEvents.RlsRateValid=true;
            }
            else
            {
               mRateErrorEvents.RlsRateValid=false;
            }
        }
    }
}
void MainWindow::ComputeBeatError(double A_EventTime,bool haveValidBPH, double BPH)
{
    mBeatErrorEvents.BeatErrorTimes[mBeatErrorEvents.BeatErrorIdx]=A_EventTime;
    mBeatErrorEvents.BeatErrorIdx++;
    if (mBeatErrorEvents.BeatErrorIdx==3)
    {
        double t1=(mBeatErrorEvents.BeatErrorTimes[1]-mBeatErrorEvents.BeatErrorTimes[0])/(double)mCurrentSamplesPerSecond;
        double t2=(mBeatErrorEvents.BeatErrorTimes[2]-mBeatErrorEvents.BeatErrorTimes[1])/(double)mCurrentSamplesPerSecond;

        mBeatErrorEvents.BeatErrorMs=qAbs(((t1-t2)/2.0)*1000.0);
        mBeatErrorEvents.RollBeatError->Add(mBeatErrorEvents.BeatErrorMs);
        mBeatErrorEvents.BeatErrorTimes[0]=mBeatErrorEvents.BeatErrorTimes[2];
        mBeatErrorEvents.BeatErrorIdx=1;
    }
}
void MainWindow::A_Event(double A_EventTime,bool haveValidBPH, double BPH)
{
  ComputeRateError(A_EventTime,haveValidBPH,BPH);
  ComputeBeatError(A_EventTime,haveValidBPH,BPH);
  mAmplitudeEvents.Have_A_Event=true;
  mAmplitudeEvents.Last_A_Event=A_EventTime;
}
void MainWindow::DisplayResults(void)
{
    QString BeatsPerHour,RateError,BeatError,Amplitude, Results;
    if (mRateErrorEvents.BPH_Valid)
    {
        BeatsPerHour= QString("%1").arg(mRateErrorEvents.BPH, 5, 10, QChar(' '));
    }
    else BeatsPerHour="-----";

    if (mRateErrorEvents.RlsRateValid)
    {
        RateError= QString::asprintf("%+6.1f", mRateErrorEvents.RlsRate); //QString("%1").arg(mRateErrorEvents.RlsRate, 6, 'f', 1);
    }
    else RateError="------";
    if (mBeatErrorEvents.RollBeatError->CurrentSize()>0)
    {
        BeatError= QString("%1").arg(mBeatErrorEvents.RollBeatError->GetAverage(), 4, 'f', 1);
    }
    else BeatError="----";
    if (mAmplitudeEvents.RollAmplitude->CurrentSize()>0)
    {
        Amplitude=  QString("%1°").arg(qRound64(mAmplitudeEvents.RollAmplitude->GetAverage()), 3, 10, QChar(' '));
    }
    else Amplitude="---";

    Results="RATE "+RateError+" s/d   AMPLITUDE "+Amplitude+"   BEAT ERROR "+BeatError+" ms   BEAT "+BeatsPerHour+" bph";
    ui->Results->setText(Results);
}
double MainWindow::Amplitude(double LiftAngle,double T1,double BPH)
{
  double Amplitude;
  //Amplitude=(3600.00*LiftAngle)/(T1*M_PI*BPH);    // Other Equation 1
  //Amplitude=(LiftAngle*(7200.0/BPH))/(2*M_PI*T1); // Other Equation 2
  Amplitude=LiftAngle/sin((2.0*M_PI*T1)/(7200.0/BPH));
  return(Amplitude);
}
void MainWindow::ComputeAmplitude(double C_EventTime,bool haveValidBPH, double BPH)
{
 if ((mAmplitudeEvents.Have_A_Event) & (mRateErrorEvents.BPH_Valid))
    {
     double Time;
     double TempAmp;
     int    TicOrToc=((mRateErrorEvents.TicTocBeatNumber-1)&1);

     Time=(C_EventTime-mAmplitudeEvents.Last_A_Event)/(double)mCurrentSamplesPerSecond;
     TempAmp=Amplitude(mLiftAngle,Time,BPH);
     if (TempAmp<360.00)
     {
      if (TicOrToc==TIC)
      {
        mAmplitudeEvents.Amplitude_Tic_Valid=true;
        mAmplitudeEvents.Amplitude_Tic=TempAmp;
      }
      else //TOC
      {
       mAmplitudeEvents.Amplitude_Toc=TempAmp;
       if (mAmplitudeEvents.Amplitude_Tic_Valid)
        {
         double AverageAmplitudeTicToc=(mAmplitudeEvents.Amplitude_Tic+mAmplitudeEvents.Amplitude_Toc)/2.0;
         mAmplitudeEvents.RollAmplitude->Add(AverageAmplitudeTicToc);
         mAmplitudeEvents.Amplitude_Tic_Valid=false;
        }
      }
     }
     else if (TicOrToc==TIC) mAmplitudeEvents.Amplitude_Tic_Valid=false;
    }
}
void MainWindow::C_Event(double C_EventTime,bool haveValidBPH, double BPH)
{
  ComputeAmplitude(C_EventTime,haveValidBPH,BPH);
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

void MainWindow::CreateGraphs(void)
{
    QPen pen;
    QFont legendFont = font();
    legendFont.setPointSize(10);

    /* Setup plot */
    ui->ScopePlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->ScopePlot->legend->setVisible(true);
    ui->ScopePlot->legend->setFont(legendFont);
    ui->ScopePlot->legend->setSelectedFont(legendFont);
    ui->ScopePlot->legend->setSelectableParts(QCPLegend::spItems);
    ui->ScopePlot->yAxis->setLabel("Amplitude");
    ui->ScopePlot->xAxis->setLabel("Time");
    ui->ScopePlot->yAxis->setRange(0, 0.1);
    ui->ScopePlot->xAxis->setTickLabels(false);
    ui->ScopePlot->legend->setVisible(false);
    ui->ScopePlot->clearGraphs();
    ui->ScopePlot->addGraph();

    pen.setWidth(1); // Set line width
    pen.setColor(Qt::blue);
    ui->ScopePlot->graph(0)->setPen(pen);
    ui->ScopePlot->graph(0)->setBrush(QBrush(QColor(0, 0, 255, 20)));
    ui->ScopePlot->graph(0)->setName("Rectified");
    ui->ScopePlot->addGraph();

    pen.setWidth(1); // Set line width
    pen.setColor(Qt::red);
    ui->ScopePlot->graph(1)->setPen(pen);
    ui->ScopePlot->graph(1)->setName("Trigger");
    ui->ScopePlot->legend->setVisible(true);

    //ui->Rate->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->RatePlot->legend->setVisible(true);
    ui->RatePlot->legend->setFont(legendFont);
    ui->RatePlot->legend->setSelectedFont(legendFont);
    ui->RatePlot->legend->setSelectableParts(QCPLegend::spItems);
    ui->RatePlot->yAxis->setLabel("Rate Error (milliseconds)");
    ui->RatePlot->yAxis->setTickLabels(true);
    ui->RatePlot->xAxis->setLabel("Time");
    ui->RatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    ui->RatePlot->xAxis->setRange(0, mRateErrorEvents.MaxTicTocDataPoints);
    ui->RatePlot->xAxis->setTickLabels(false);
    ui->RatePlot->clearGraphs();
    ui->RatePlot->addGraph();
    ui->RatePlot->graph(0)->setScatterStyle(QCPScatterStyle::ssDisc);
    ui->RatePlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 3));
    ui->RatePlot->graph(0)->setLineStyle(QCPGraph::lsNone);
    ui->RatePlot->graph(0)->setPen(QPen(Qt::red));
    //ui->RatePlot->graph(0)->setBrush(QBrush(Qt::red));
    ui->RatePlot->graph(0)->setName("Tic Rate");
    ui->RatePlot->addGraph();
    ui->RatePlot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 3));
    ui->RatePlot->graph(1)->setLineStyle(QCPGraph::lsNone);
    ui->RatePlot->graph(1)->setPen(QPen(Qt::blue));
    //ui->RatePlot->graph(1)->setBrush(QBrush(Qt::blue));
    ui->RatePlot->graph(1)->setName("Toc Rate");
    ui->RatePlot->legend->setVisible(true);
}

MainWindow::~MainWindow()
{
    delete mTfliteRunner;
    delete ui;
}

void MainWindow::StartAudioThread(void)
{
    QVariant v = ui->InputDeviceComboBox->currentData();
    QAudioDevice InputDevice = v.value<QAudioDevice>();
    Reset();
    if (mRawAudio)
    {
        if (mRawAudio->Samples)
        {
            delete[] mRawAudio->Samples;
            mRawAudio->Samples=NULL;
        }
        delete mRawAudio;
        mRawAudio=NULL;
    }
    mRawAudio=new TMasterAudioDataRaw;
    mRawAudio->NumberOfAudioSamples=mCurrentSamplesPerSecond * SECONDS_OF_BUFFER;
    mRawAudio->Samples=new float[mRawAudio->NumberOfAudioSamples];
    mAudioWorkerThread=new QThread();
    mAudioWorker = new TAudioWorker(mRawAudio);
    mAudioWorker->moveToThread(mAudioWorkerThread); // Move the worker to the new thread

    QObject::connect(mAudioWorker, &TAudioWorker::finished, mAudioWorkerThread, &QThread::quit);
    //QObject::connect(mAudioWorker, &TAudioWorker::finished, mAudioWorker, &QObject::deleteLater);
    QObject::connect(mAudioWorkerThread, &QThread::finished, mAudioWorker, &QObject::deleteLater);
    QObject::connect(mAudioWorkerThread, &QThread::finished, mAudioWorkerThread, &QObject::deleteLater);

    // Connect data signal to a handler in the main thread (queued connection is automatic)
    QObject::connect(mAudioWorker, &TAudioWorker::AudioDataReady,this,&MainWindow::HandleAudioInput);

    QObject::connect(this, &MainWindow::LocalStartAudio,mAudioWorker,&TAudioWorker::StartAudioRecording);
    QObject::connect(this, &MainWindow::LocalStopAudio,mAudioWorker,&TAudioWorker::StopAudioRecording);
    QObject::connect(this, &MainWindow::LocalSetAudioInputVolume,mAudioWorker,&TAudioWorker::SetAudioInputVolume);
    mAudioWorkerThread->start(QThread::TimeCriticalPriority);
    emit LocalStartAudio(InputDevice,mCurrentSamplesPerSecond,ui->MicrophoneHorizontalSlider->sliderPosition()/1000.0);
}
void MainWindow::StopAudioThread(void)
{
 emit LocalStopAudio();
}

void MainWindow::StartPlaybackThread(const QString &FileName)
{
    Reset();
    if (mRawAudio)
    {
        if (mRawAudio->Samples)
        {
            delete[] mRawAudio->Samples;
            mRawAudio->Samples=NULL;
        }
        delete mRawAudio;
        mRawAudio=NULL;
    }
    mRawAudio=new TMasterAudioDataRaw;
    mRawAudio->NumberOfAudioSamples=mCurrentSamplesPerSecond * SECONDS_OF_BUFFER;
    mRawAudio->Samples=new float[mRawAudio->NumberOfAudioSamples];
    mPlaybackWorkerThread=new QThread();
    mPlaybackWorker = new TPlaybackWorker(mRawAudio,mCurrentSamplesPerSecond);
    mPlaybackWorker->moveToThread(mPlaybackWorkerThread); // Move the worker to the new thread

    QObject::connect(mPlaybackWorker, &TPlaybackWorker::finished, mPlaybackWorkerThread, &QThread::quit);
    //QObject::connect(mPlaybackWorker, &TPlaybackWorker::finished, mPlaybackWorker, &QObject::deleteLater);
    QObject::connect(mPlaybackWorkerThread, &QThread::finished, mPlaybackWorker, &QObject::deleteLater);
    QObject::connect(mPlaybackWorkerThread, &QThread::finished, mPlaybackWorkerThread, &QObject::deleteLater);

    QObject::connect(this, &MainWindow::LocalStartPlayback,mPlaybackWorker,&TPlaybackWorker::StartPlayback);
    // Connect data signal to a handler in the main thread (queued connection is automatic)
    QObject::connect(mPlaybackWorker, &TPlaybackWorker::PlaybackDataReady,this,&MainWindow::HandlePlaybackInput);
    QObject::connect(mPlaybackWorker, &TPlaybackWorker::PlaybackDoneReadingFile,this,&MainWindow::HandlePlaybackDoneReadingFile);
    mPlaybackWorkerThread->start(QThread::TimeCriticalPriority);

    emit LocalStartPlayback(FileName);
}
void MainWindow::StartSimThread(WatchSynthStreamConfig cfg)
{
    Reset();
    if (mRawAudio)
    {
        if (mRawAudio->Samples)
        {
            delete[] mRawAudio->Samples;
            mRawAudio->Samples=NULL;
        }
        delete mRawAudio;
        mRawAudio=NULL;
    }
    mRawAudio=new TMasterAudioDataRaw;
    mRawAudio->NumberOfAudioSamples=mCurrentSamplesPerSecond * SECONDS_OF_BUFFER;
    mRawAudio->Samples=new float[mRawAudio->NumberOfAudioSamples];
    mSimWorkerThread=new QThread();
    mSimWorker = new TSimWorker(mRawAudio,mCurrentSamplesPerSecond);
    mSimWorker->moveToThread(mSimWorkerThread); // Move the worker to the new thread

    QObject::connect(mSimWorker, &TSimWorker::finished, mSimWorkerThread, &QThread::quit);
    //QObject::connect(mSimWorker, &TSimWorker::finished, mSimWorker, &QObject::deleteLater);
    QObject::connect(mSimWorkerThread, &QThread::finished, mSimWorker, &QObject::deleteLater);
    QObject::connect(mSimWorkerThread, &QThread::finished, mSimWorkerThread, &QObject::deleteLater);

    QObject::connect(this, &MainWindow::LocalStartSim,mSimWorker,&TSimWorker::StartSim);
    // Connect data signal to a handler in the main thread (queued connection is automatic)
    QObject::connect(mSimWorker, &TSimWorker::SimDataReady,this,&MainWindow::HandleSimInput);
    QObject::connect(mSimWorker, &TSimWorker::SimDone,this,&MainWindow::HandleSimDone);
    mSimWorkerThread->start(QThread::TimeCriticalPriority);

    emit LocalStartSim(cfg);
}
void MainWindow::StopPlaybackThread(void)
{
 if (mPlaybackWorkerThread)
    {
     mPlaybackWorkerThread->requestInterruption();
    }
}
void MainWindow::StopSimThread(void)
{
    if (mSimWorkerThread)
    {
        mSimWorkerThread->requestInterruption();
    }
}


void MainWindow::AddVerticalMarker(QCustomPlot *Plot,double x,double height,const QColor color)
{
// Create the line
QCPItemLine *marker = new QCPItemLine(Plot);
marker->start->setCoords(x, 0.0); //Plot->yAxis->range().lower);
marker->end->setCoords(x, height); //Plot->yAxis->range().upper);

// Make it look like a marker
QPen pen;
pen.setColor(color);
pen.setWidth(2);
pen.setStyle(Qt::DashLine);
//pen.setStyle(Qt::SolidLine);
marker->setPen(pen);
}
void MainWindow::AddText(QCustomPlot *Plot, double x,double height,QString text,const QColor color,Qt::Alignment alignment)
{
    QCPItemText *textLabel = new QCPItemText(Plot);
    textLabel->setColor(color);
    textLabel->setFont(QFont(font().family(), 10));

    textLabel->setPositionAlignment(alignment);
    textLabel->position->setType(QCPItemPosition::ptPlotCoords);
    //QFontMetricsF fm(textLabel->font());
    //QRectF textRect = fm.boundingRect(text);

    textLabel->position->setCoords(x,height);

    textLabel->setText(text);
    textLabel->setFont(QFont(font().family(), 10));
    textLabel->setPen(QPen(color));
}
void  MainWindow::AddHorizontalMarkerInward(QCustomPlot *Plot,double xLeft,double xRight,double Length,double Height,const QColor Color)
{
    // Make it look like a marker
    QPen pen;
    pen.setColor(Color);
    pen.setWidth(1);
    //pen.setStyle(Qt::DashLine);
    pen.setStyle(Qt::SolidLine);

    // Create the lines
    QCPItemLine *markerLeft = new QCPItemLine(Plot);
    markerLeft->start->setCoords(xLeft-Length,Height); //Plot->yAxis->range().lower);
    markerLeft->end->setCoords(xLeft, Height); //Plot->yAxis->range().upper);
    markerLeft->setHead(QCPLineEnding::esSpikeArrow); // Arrow at start
    markerLeft->setPen(pen);

    // Create the lines
    QCPItemLine *markerRight = new QCPItemLine(Plot);
    markerRight->start->setCoords(xRight,Height); //Plot->yAxis->range().lower);
    markerRight->end->setCoords(xRight+Length, Height); //Plot->yAxis->range().upper);
    markerRight->setTail(QCPLineEnding::esSpikeArrow); // Arrow at start
    markerRight->setPen(pen);

}
void MainWindow::AddHorizontalMarkerOutward(QCustomPlot *Plot,double xLeft,double xRight,double Height,const QColor Color)
{
    // Create the line
    QCPItemLine *marker = new QCPItemLine(Plot);
    marker->start->setCoords(xLeft,Height); //Plot->yAxis->range().lower);
    marker->end->setCoords(xRight, Height); //Plot->yAxis->range().upper);

    // Make it look like a marker
    QPen pen;
    pen.setColor(Color);
    pen.setWidth(1);
    //pen.setStyle(Qt::DashLine);
    pen.setStyle(Qt::SolidLine);
    marker->setHead(QCPLineEnding::esSpikeArrow); // Arrow at end
    marker->setTail(QCPLineEnding::esSpikeArrow); // Arrow at start
    marker->setPen(pen);
}
void MainWindow::RemoveMarkersAndText(QCustomPlot *Plot,double rangeMin,double rangeMax)
{
 // Iterate backwards to safely remove items while iterating
 for (int i = Plot->itemCount() - 1; i >= 0; --i)
 {
    QCPAbstractItem *baseItem = Plot->item(i);
    QCPItemLine *lineItem = qobject_cast<QCPItemLine*>(baseItem);
    QCPItemText *textLabel= qobject_cast<QCPItemText*>(baseItem);

    if (lineItem) {
        // Get key coordinates for start and end
        double startKey = lineItem->start->coords().x();
        double endKey = lineItem->end->coords().x();

        // logic: Remove if any part of the line is within range
        if ((startKey >= rangeMin && startKey <= rangeMax) ||
            (endKey >= rangeMin && endKey <= rangeMax)) {
            Plot->removeItem(lineItem);
        }
    }
    else if (textLabel)
    {
        // Get key coordinates for start and end
        double Key = textLabel->position->coords().x();

        // logic: Remove if any part of the line is within range
        if (Key >= rangeMin && Key <= rangeMax)
        {
            Plot->removeItem(textLabel);
        }
    }
 }
}

void MainWindow::HandleInputData(TMasterAudioDataRaw *SharedDataPtr)
{
        SharedDataPtr->Mutex.lock();
        mLocalWriteIndex=SharedDataPtr->WriteIndex;
        mLocalTotalSamplesWritten=SharedDataPtr->TotalSamplesWritten;
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

void MainWindow::HandleAudioInput()
{
HandleInputData(mAudioWorker->mRawAudio);
}
void MainWindow::HandlePlaybackInput()
{
 HandleInputData(mPlaybackWorker->mRawAudio);
}
void MainWindow::HandleSimInput()
{
 HandleInputData(mSimWorker->mRawAudio);
}
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


          mSoundRenderer.processSamples(mInputBlock,slice);

          mMfccExtractor.feedSamples(mInputBlock, slice);

          tg_result_t r;
          if (tg_process(mCtx,mInputBlock, slice, &r) != 0) {
              qInfo()<<"tg_process failed";
              return ;
          }

          double threshhold=r.onset_threshold;
          for (int i=0;i<r.processed_pcm_len;i++)
           {
            double pcm=r.processed_pcm[i];
            ui->ScopePlot->graph(0)->addData(mLocalGraphTicks, pcm);
            ui->ScopePlot->graph(1)->addData(mLocalGraphTicks, threshhold);
            mLocalGraphTicks++;
           }

          if ((!mSoundRenderHasBPH) &&(r.sync_status==TG_SYNC_SYNCED))
           {
              mSoundRenderHasBPH=true;
              mSoundRenderer.setBph(r.detected_bph);
           }

          for (int i=0;i<r.num_events;i++)
             {
               double val;
               if (r.events[i].type==TG_EVENT_A)
               {
                    val=r.events[i].sample_index+r.events[i].sub_sample_offset;
                    AddVerticalMarker(ui->ScopePlot,val,r.events[i].peak_value,Qt::green);
                    if (mHaveLastA)
                    {
                      double delta=val-mLastA;
                      AddHorizontalMarkerOutward(ui->ScopePlot,mLastA,val,r.events[i].peak_value/2.0,Qt::black);
                      QString text = QString(" %1 ms ").arg(delta*1000.0/mCurrentSamplesPerSecond, 0, 'f', 2);
                      AddText(ui->ScopePlot,mLastA+(delta/2.0),r.events[i].peak_value/2.0,text,Qt::black,Qt::AlignHCenter | Qt::AlignTop);
                    }
                    mLastA=val;
                    mHaveLastA=true;
                    A_Event(val,(r.sync_status==TG_SYNC_SYNCED),r.detected_bph);
                    if (mSoundRenderHasBPH)
                    {
                     mSoundRenderer.markAEventAbsoluteSampleIndex(val, qRgba(0, 255, 0, 255), SND_PIXEL_SIZE);
                    }
               }
               else if (r.events[i].type==TG_EVENT_C)
               {
                   double delta;
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

                   delta=val-mLastA;
                   QString text;
                   if (r.sync_status==TG_SYNC_SYNCED) // Have BPH
                   {
                       int Amp=qRound(Amplitude(mLiftAngle,delta/mCurrentSamplesPerSecond,r.detected_bph));
                       if (Amp<360)
                       {
                        text= QString(" %1 ms\n%2°").arg(delta*1000.0/mCurrentSamplesPerSecond, 0, 'f', 1).arg(Amp);
                       }
                       else text= QString(" %1 ms ").arg(delta*1000.0/mCurrentSamplesPerSecond, 0, 'f', 1);
                   }
                   else text= QString(" %1 ms ").arg(delta*1000.0/mCurrentSamplesPerSecond, 0, 'f', 1);
                   AddVerticalMarker(ui->ScopePlot,val,r.events[i].peak_value, Qt::red);
                   AddHorizontalMarkerInward(ui->ScopePlot,mLastA,val,INWARD_MARKER_LENGTH,r.events[i].peak_value,Qt::black);
                   AddText(ui->ScopePlot,val+INWARD_MARKER_LENGTH,r.events[i].peak_value,text,Qt::black,Qt::AlignLeft | Qt::AlignTop);

                   C_Event(val,(r.sync_status==TG_SYNC_SYNCED),r.detected_bph);
                   if (mSoundRenderHasBPH)
                   {
                    mSoundRenderer.markCEventAbsoluteSampleIndex(val, qRgba(0, 0, 255,255), SND_PIXEL_SIZE);
                   } 
               }
               else qInfo()<< "Unkown Event Type";

             }
        mForegroundSampleCount+=slice;
        SamplesToAdd=SamplesToAdd-slice;
        }

        SharedDataPtr->MainThrd_LastTotalSamplesWritten=mLocalTotalSamplesWritten;
        PurgeHistory();
        ui->ScopePlot->xAxis->setRange(mLocalGraphTicks, (double)mCurrentSamplesPerSecond/ui->ScopeScaleSpinBox->value(), Qt::AlignRight);
        //ui->ScopePlot->xAxis->rescale();
        ui->ScopePlot->yAxis->rescale();
        ui->ScopePlot->replot(QCustomPlot::rpQueuedReplot);
        ui->SoundImage->DrawImage();

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
        }
    }
}
void MainWindow::PurgeHistory(void)
{
    for (int i=0;i<ui->ScopePlot->graphCount();i++)
    {
        if (ui->ScopePlot->graph(i)->data()->size()>(GRAPH_HISTORY_IN_SECONDS*mCurrentSamplesPerSecond))
        {
            //qInfo()<<"Data Size 1 -"<<ui->Scope->graph(i)->data()->size();
            bool foundRange;
            QCPRange keyRange = ui->ScopePlot->graph(i)->getKeyRange(foundRange, QCP::sdBoth);
            if (foundRange)
            {
                double minKey = keyRange.lower;
                double maxKey = keyRange.upper;
                double NumKeys=maxKey-minKey;
                //qInfo()<<"Min "<< minKey<<" Max "<<maxKey;
                //qInfo()<<"Data Size 2 -"<< NumKeys;
                double NumToRemove= NumKeys-((GRAPH_HISTORY_IN_SECONDS*mCurrentSamplesPerSecond)/2);
                double RemoveStart=minKey;
                double RemoveEnd=minKey+NumToRemove;
                // qInfo()<<"Remove "<< RemoveStart<<"  "<<RemoveEnd;
                RemoveMarkersAndText(ui->ScopePlot,RemoveStart, RemoveEnd);
                ui->ScopePlot->graph(i)->data()->remove(RemoveStart, RemoveEnd);
            }
            else  qInfo()<<"getKeyRange not found";
        }
    }
}
void MainWindow::Reset(void)
{
    SoundImageRenderer::Config SoundImageCfg;
    qInfo()<<"RESET";
    SoundImageCfg.bph = 0.0; // unknown at initialization
    SoundImageCfg.sample_rate_hz = mCurrentSamplesPerSecond;
    SoundImageCfg.sound_color = qRgba(255, 0, 0, 255);         // red intensity for sound
    SoundImageCfg.background_color = qRgba(255, 255, 255, 255); // white background
    //SoundImageCfg.vertical_time_direction = SoundImageRenderer::TimeStartsAtBottomMovesUp;
    SoundImageCfg.vertical_time_direction = SoundImageRenderer::TimeStartsAtTopMovesDown;

    SoundImageCfg.warmup_columns = 2;
    SoundImageCfg.anchor_columns = 12;
    SoundImageCfg.gamma = 0.5f;//1.0f;
    SoundImageCfg.live_preview_current_column = true;

    mSoundRenderHasBPH=false;

    if (!mSoundRenderer.initialize(ui->SoundImage->GetImage(), SoundImageCfg)) {
        qCritical() << "Failed to initialize SoundImageRenderer.";
        throw std::runtime_error("Failed to initialize SoundImageRenderer.");
    }
    mSoundRenderer.reset();

    mLocalGraphTicks=0;

    for (int i=0;i<ui->ScopePlot->graphCount();i++)
    {
     ui->ScopePlot->graph(i)->data()->clear();
    }
    ui->ScopePlot->clearItems();
    ui->ScopePlot->replot();
    mHaveLastA=false;
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
    StartAudioThread();
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
    StartPlaybackThread(fileDialog.selectedFiles().constFirst());
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
    StartSimThread(cfg);
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
    emit LocalSetAudioInputVolume(ui->MicrophoneHorizontalSlider->sliderPosition()/1000.0);
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
        StopAudioThread();
        AudioCloseCheck();
    }
    else if(ui->ModeComboBox->currentText()==ModeStrings[PLAYBACK])
    {
        StopPlaybackThread();

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
        StopSimThread();
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