#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QDir>
#include <QMainWindow>
#include <QComboBox>
#include <vector>
#include "qcustomplot.h"
#include "AudioWorker.h"
#include "PlaybackWorker.h"
#include "SimWorker.h"
#include "WavStreamWriter.h"
#include "Timegrapher.h"
#include "RollingLeastSquares.h"
#include "SoundImageRenderer.h"
#include "RollingAverage.h"
#include "WatchSynthStream.h"
#include "MfccExtractor.h"
#include "TfliteRunner.h"


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

#define AUDIO_OUTPUT 0
#define DEBUG_OUTPUT 0

typedef struct
{
 uint64_t             TicTocBeatNumber;
 QVector<double>      xTic;
 QVector<double>      xToc;
 QVector<double>      yTic;
 QVector<double>      yToc;
 int                  xTicIndex;
 int                  xTocIndex;
 bool                 HaveStartTime;
 bool                 HaveZeroOffset;
 double               StartTime;
 double               ZeroOffsetValue;
 int                  MaxTicTocDataPoints;
 RollingLeastSquares *RlsTicRate;
 RollingLeastSquares *RlsTocRate;
 double               RlsRate;
 bool                 RlsRateValid;
 int                  BPH;
 bool                 BPH_Valid;
 int                  WatchHertz;
} TRateErrorEvents;

typedef struct
{
 double             BeatErrorTimes[3];
 int                BeatErrorIdx;
 double             BeatErrorMs;
 RollingAverage    *RollBeatError;
} TBeatErrorEvents;


typedef struct
{
 double             Last_A_Event;
 bool               Have_A_Event;
 RollingAverage    *RollAmplitude;
 double             Amplitude_Tic;
 double             Amplitude_Toc;
 bool               Amplitude_Tic_Valid;
} TAmplitudeErrorEvents;


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_RefreshPushButton_clicked();
    void on_MicrophoneHorizontalSlider_sliderMoved(int position);
    void on_StartPushButton_clicked();
    void on_StopPushButton_clicked();
    void on_InputDeviceComboBox_currentIndexChanged(int index);
    void on_SampleRatesComboBox_currentIndexChanged(int index);
    void on_LiftAngleSpinBox_valueChanged(int arg1);
    void on_AveragingPeriodComboBox_currentIndexChanged(int index);
    void on_ModeComboBox_currentTextChanged(const QString &arg1);

public slots:
    void HandleAudioInput();
    void HandlePlaybackInput();
    void HandlePlaybackDoneReadingFile();
    void HandleSimInput();
    void HandleSimDone();

signals:
    void LocalStartAudio(QAudioDevice InputDevice,int SampleRate,float Volume);
    void LocalStopAudio();
    void LocalSetAudioInputVolume(float Volume);
    void LocalStartPlayback(const QString &FileName);
    void LocalStartSim(WatchSynthStreamConfig cfg);

private:
    Ui::MainWindow *ui;
    void   StartAudioThread(void);
    void   ConfigureSoundCard(void);
    void   Reset(void);
    void   PurgeHistory(void);
    void   CreateGraphs(void);
    void   CreateDectectors(void);
    void   DeleteDectectors(void);
    void   LoadAudioDevices(void);
    void   StopAudioThread(void);
    void   StartPlaybackThread(const QString &FileName);
    void   StopPlaybackThread(void);
    void   StartSimThread(WatchSynthStreamConfig cfg);
    void   StopSimThread(void);
    void   AddOrOverwrite(QVector<double>& xvec,QVector<double>& yvec, double value, int maxS, int& index);
    void   AddVerticalMarker(QCustomPlot *Plot, double x,double height,const QColor color);
    void   AddHorizontalMarkerOutward(QCustomPlot *Plot,double xLeft,double xRight,double Height,const QColor Color);
    void   AddHorizontalMarkerInward(QCustomPlot *Plot,double xLeft,double xRight,double Length,double Height,const QColor Color);
    void   AddText(QCustomPlot *Plot, double x,double height,QString text,const QColor color,Qt::Alignment alignment);
    void   RemoveMarkersAndText(QCustomPlot *Plot, double rangeMin,double rangeMax);
    bool   OpenFile(const QString &FileName);
    void   HandleInputData(TMasterAudioDataRaw *SharedDataPtr);
    void   CreateEvents(void);
    void   EventsReset(void);
    double WrapInToRange(double number, double lower_bound, double upper_bound);
    bool   RecordSessionCheck(void);
    void   AudioCloseCheck(void);
    bool   SetAudioRate(int Rate);
    bool   SetAudioDevice(QString Name);
    void   GetAudioRate(int &Rate);
    void   GetAudioDevice(QString &Name);
    double Amplitude(double LiftAngle,double T1,double BPH);
    void   ProcessSamples(TMasterAudioDataRaw *SharedDataPtr);
    void   PopulateSampleRates(QComboBox *comboBox, const QAudioDevice &device);
    void   A_Event(double A_EventTime,bool haveValidBPH, double BPH);
    void   C_Event(double C_EventTime,bool haveValidBPH, double BPH);
    void   ComputeRateError(double A_EventTime,bool haveValidBPH, double BPH);
    void   ComputeBeatError(double A_EventTime,bool haveValidBPH, double BPH);
    void   ComputeAmplitude(double A_EventTime,bool haveValidBPH, double BPH);
    void   DisplayResults(void);
    void   LoadBPH(void);
    void   LoadSimBPH(void);
    void   LoadMode(void);
    void   LoadAverageingPeriod(void);
    void   SetGuiRunMode(void);
    void   SetGuiStopMode(void);
    void   LiveStart(void);
    void   PlaybackStart(void);
    void   SimStart(void);


    WavStreamWriter           *mWavWriter= nullptr;
    TRateErrorEvents           mRateErrorEvents;
    TBeatErrorEvents           mBeatErrorEvents;
    TAmplitudeErrorEvents      mAmplitudeEvents;
    TMasterAudioDataRaw       *mRawAudio= nullptr;
    QThread                   *mAudioWorkerThread= nullptr;
    TAudioWorker              *mAudioWorker= nullptr;
    QThread                   *mPlaybackWorkerThread= nullptr;
    TPlaybackWorker           *mPlaybackWorker= nullptr;
    QThread                   *mSimWorkerThread= nullptr;
    TSimWorker                *mSimWorker= nullptr;
    int                        mAvalableRates[5];
    int                        mNumberofRates;
    double                     mLiftAngle;
    int                        mAveragingPeriod;
    unsigned int               mLocalWriteIndex;
    uint64_t                   mLocalTotalSamplesWritten;
    uint64_t                   mLocalGraphTicks;
    QDir                       mCurrentDir;
    int                        mCurrentSamplesPerSecond;
    int                        mRateBeforePlaybackOrSim;
    QString                    mDeviceNameBeforePlaybackOrSim;
    tg_config_t                mCfg;
    tg_context_t              *mCtx= nullptr;
    float                     *mInputBlock = nullptr;
    SoundImageRenderer         mSoundRenderer;
    bool                       mSoundRenderHasBPH=false;
    MfccExtractor              mMfccExtractor;
    static constexpr int       kMfccNumFrames = 49;
    static constexpr int       kMfccNumCoeffs = 13;
    std::vector<float>         mMfccFrameBuffer; // accumulates kMfccNumFrames * kMfccNumCoeffs floats
    TfliteRunner*              mTfliteRunner = nullptr;
    double                     mLastA;
    bool                       mHaveLastA=false;
    double                     mBackgroundLastFPS=0.0;
    double                     mBackgroundLastSPF=0.0;
    double                     mBackgroundLastSPS=0.0;
    double                     mForegroundFPS=0.0;
    double                     mForegroundSPF=0.0;
    double                     mForegroundSPS=0.0;
    double                     mForegroundLastFPS=0.0;
    double                     mForegroundLastSPF=0.0;
    double                     mForegroundLastSPS=0.0;
    bool                       mForegroundTimerStarted=false;
    QElapsedTimer              mForegroundTimer;
    double                     mForegroundLastTime=0.0;
    uint64_t                   mForegroundFrameCount=0;
    uint64_t                   mForegroundSampleCount=0;

};
#endif
