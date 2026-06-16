#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QDir>
#include <QMainWindow>
#include <QComboBox>
#include <QTimer>     // [PERF 계측] CPU/메모리 1Hz 샘플링 타이머 (docs/PERF_VERIFICATION_GUIDE.md §C/§D)
#include "qcustomplot.h"
#include "AudioWorker.h"
#include "PlaybackWorker.h"
#include "SimWorker.h"
#include "WavStreamWriter.h"
#include "Timegrapher.h"
#include "MeasurementEngine.h"
#include "WatchSynthStream.h"


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class TabManager;   // [탭 모듈] 디스플레이 탭 등록·갱신 브로드캐스트 (tabs/TabManager.h)

#define AUDIO_OUTPUT 0
#define DEBUG_OUTPUT 0

// rate/beat/amplitude 측정 상태·계산은 MeasurementEngine 로 분리됨(구 T*Events 구조체 대체).


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

    // [PERF 계측 · §A-3 · QA-RT-01] UI 이벤트 루프 응답성(지연) 표본화 슬롯(0.1초 주기)
    void SamplePerfUiResponsiveness();
    // [PERF 계측 · §A-1/A-2 · QA-LT-01] 실제 그리기 완료(afterReplot) 시점 계측 → 진짜 종단간
    void OnScopeReplotted();
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
    TabManager     *mTabManager = nullptr;   // [탭 모듈] 12개 디스플레이 탭을 등록·갱신(QA-MOD-01)
    uint64_t        mInputAbsSample = 0;      // [탭 모듈] 원신호(raw) 게시용 0-기반 입력 샘플 인덱스(이벤트/엔벨로프와 동일 좌표)
    void   RegisterDisplayTabs(void);        // [탭 모듈] 신규 탭 모듈들을 생성·등록
    void   PublishMeasurementToTabs(void);   // [탭 모듈] 현재 측정값을 스냅샷으로 탭에 게시
    void   StartAudioThread(void);
    void   ConfigureSoundCard(void);
    void   Reset(void);
    void   CreateDectectors(void);
    void   DeleteDectectors(void);
    void   LoadAudioDevices(void);
    void   StopAudioThread(void);
    void   StartPlaybackThread(const QString &FileName);
    void   StopPlaybackThread(void);
    void   StartSimThread(WatchSynthStreamConfig cfg);
    void   StopSimThread(void);
    bool   OpenFile(const QString &FileName);
    void   HandleInputData(TMasterAudioDataRaw *SharedDataPtr);
    void   EventsReset(void);
    bool   RecordSessionCheck(void);
    void   AudioCloseCheck(void);
    bool   SetAudioRate(int Rate);
    bool   SetAudioDevice(QString Name);
    void   GetAudioRate(int &Rate);
    void   GetAudioDevice(QString &Name);
    void   ProcessSamples(TMasterAudioDataRaw *SharedDataPtr);
    void   PopulateSampleRates(QComboBox *comboBox, const QAudioDevice &device);
    void   A_Event(double A_EventTime,bool haveValidBPH, double BPH);
    void   C_Event(double C_EventTime,bool haveValidBPH, double BPH);
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
    MeasurementEngine          mEngine;   // rate/beat/amplitude 측정 계산(구 T*Events 대체)
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
    QDir                       mCurrentDir;
    int                        mCurrentSamplesPerSecond;
    int                        mRateBeforePlaybackOrSim;
    QString                    mDeviceNameBeforePlaybackOrSim;
    tg_config_t                mCfg;
    tg_context_t              *mCtx= nullptr;
    float                     *mInputBlock = nullptr;
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

    // ── [PERF 계측] docs/PERF_VERIFICATION_GUIDE.md 측정용 멤버 (제품 기능 아님) ──
    double                     mLocalLastBlockCaptureMs=0.0; // [A-1/A-2] HandleInputData에서 Mutex로 읽은 최신 블록 캡처 시각
    uint64_t                   mLocalDroppedSamples=0;       // [B-1] 드롭 추정 스냅샷
    QTimer                    *mPerfUiTimer=nullptr;         // [A-3] UI 응답성 0.1s 하트비트 타이머
    double                     mPerfUiLastMs=0.0;            // [A-3] 직전 하트비트 시각
    bool                       mPerfUiHave=false;            // [A-3] 첫 회(워밍업) 제외 플래그
    // [A-1/A-2] 실제 페인트 완료(afterReplot) 계측용 — replot 요청 시점을 보관했다가 페인트 후 차이 산출
    double                     mPerfReplotRequestMs=0.0;     // replot 요청 시각
    double                     mPerfCaptureForReplotMs=0.0;  // 그 replot에 대응하는 캡처 시각(Live)
    bool                       mPerfReplotLive=false;        // 그 replot이 Live 모드였나
    bool                       mPerfReplotPending=false;     // afterReplot 대기 중(중복/잡음 replot 무시)
    // [F-1 · QA-SC-01] 프레임(화면 갱신) 측정 — 실제 paint 횟수 vs replot 요청 횟수(코얼레싱/드롭)
    uint64_t                   mPaintCount=0;                // 1초간 실제 paint(afterReplot) 수
    uint64_t                   mReplotReqCount=0;            // 1초간 replot 요청 수(ProcessSamples)
    double                     mPaintLastEmitMs=0.0;         // paint_fps 직전 emit 시각
    bool                       mPaintHave=false;
    bool                       mSimActive=false;             // [G-1] 현재 Sim 모드 측정 중인가
    WatchSynthStreamConfig     mLastSimCfg;                  // [G-1] Sim 설정값(정답) — 측정값 대비 비교용
    TGtBeat                    mLocalGt[GT_EVENT_RING];      // [E/G-2] GT 이벤트 링 스냅샷(메인 스레드 로컬)
    unsigned int               mLocalGtHead=0;               // [E/G-2]
    uint64_t                   mLocalGtTotal=0;              // [E/G-2] 누적 정답 비트 수(검출률 분모)
    bool                       mLocalGtValid=false;          // [E/G-2] 이번 호출에서 GT 스냅샷 유효 여부
};
#endif
