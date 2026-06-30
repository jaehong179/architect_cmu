#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDir>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QComboBox>
#include <QStringList>
#include <QAudioDevice>
#include "WavStreamWriter.h"      // mWavWriter (녹음 대상)
#include "MeasurementEngine.h"    // mEngine (측정 계산)
#include "CaptureController.h"    // mCapture (오디오 소스/파이프라인) — tg_*·TMasterAudioDataRaw 도 여기서 전이 제공
#include "EventHandler.h"         // mEventHandler (워치독 이벤트 → 사용자 알림)
#include "SimConfigBuilder.h"     // SimStart 의 합성 설정 조립(WatchSynthStreamConfig 포함)
#include "WaveLodHistory.h"       // 8분 엔벨로프 이력 버퍼(중앙 1개, pause/스크롤백용)
#include "PositionTimingModel.h"
#include "PositionSequenceController.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QQuickWidget;
class QTimer;
class TabManager;   
class TabRateScope; 
class TabBeatErrorTrace;
class TabSequenceDisplay;
class ReadoutBar;   
class PositionChangeDialog;
class WarmupOverlay;

#ifdef ENABLE_VISION
class QThread;                                  // [vision] 추론 워커 전용 스레드
namespace vision { class VisionWorker; }        // [vision] 웹캠 1Hz watch-position 추론
#endif
#ifdef ENABLE_DIAG
class QThread;                                  // [diag] 진단 워커 전용 스레드
namespace diag { class DiagWorker; }            // [diag] t1/t3 고장유형 진단 추론
class DiagBanner;                               // [diag] 진단 완료 알림 배너
#endif

#define AUDIO_OUTPUT 0
#define DEBUG_OUTPUT 0

class MainWindow : public QMainWindow
{
    Q_OBJECT

    // =========================================================================
    // QML Bindings Interface (Q_PROPERTY)
    // =========================================================================
    Q_PROPERTY(int currentMode READ currentMode WRITE setCurrentMode NOTIFY currentModeChanged)
    Q_PROPERTY(double gain READ gain WRITE setGain NOTIFY gainChanged)
    Q_PROPERTY(QStringList deviceList READ deviceList NOTIFY deviceListChanged)
    Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY deviceIndexChanged)
    Q_PROPERTY(QStringList sampleRateList READ sampleRateList NOTIFY sampleRateListChanged)
    Q_PROPERTY(int sampleRateIndex READ sampleRateIndex WRITE setSampleRateIndex NOTIFY sampleRateIndexChanged)
    Q_PROPERTY(int averagingPeriodIndex READ averagingPeriodIndex WRITE setAveragingPeriodIndex NOTIFY averagingPeriodIndexChanged)
    Q_PROPERTY(QString selectedWavFile READ selectedWavFile NOTIFY selectedWavFileChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY isPausedChanged)
    Q_PROPERTY(bool inWarmup READ inWarmup NOTIFY inWarmupChanged)
    Q_PROPERTY(bool recordSessionEnabled READ recordSessionEnabled WRITE setRecordSessionEnabled NOTIFY recordSessionEnabledChanged)
    Q_PROPERTY(QString detectedPosition READ detectedPosition NOTIFY detectedPositionChanged)
    
    Q_PROPERTY(int detectorBphIndex READ detectorBphIndex WRITE setDetectorBphIndex NOTIFY detectorBphIndexChanged)
    Q_PROPERTY(int liftAngle READ liftAngle WRITE setLiftAngle NOTIFY liftAngleChanged)
    
    Q_PROPERTY(int simBphIndex READ simBphIndex WRITE setSimBphIndex NOTIFY simBphIndexChanged)
    Q_PROPERTY(int simErrorRate READ simErrorRate WRITE setSimErrorRate NOTIFY simErrorRateChanged)
    Q_PROPERTY(int simAmplitude READ simAmplitude WRITE setSimAmplitude NOTIFY simAmplitudeChanged)
    Q_PROPERTY(double simBeatError READ simBeatError WRITE setSimBeatError NOTIFY simBeatErrorChanged)
    Q_PROPERTY(bool simRealistic READ simRealistic WRITE setSimRealistic NOTIFY simRealisticChanged)
    
    Q_PROPERTY(int highPassCutoff READ highPassCutoff WRITE setHighPassCutoff NOTIFY highPassCutoffChanged)
    Q_PROPERTY(bool useConset READ useConset WRITE setUseConset NOTIFY useConsetChanged)
    Q_PROPERTY(QString watchId READ watchId WRITE setWatchId NOTIFY watchIdChanged)
    Q_PROPERTY(QString engineer READ engineer WRITE setEngineer NOTIFY engineerChanged)
    Q_PROPERTY(QObject* positionTiming READ positionTiming CONSTANT)

    // Constant lists for QML Combobox Models
    Q_PROPERTY(QStringList averagingPeriodList READ averagingPeriodList CONSTANT)
    Q_PROPERTY(QStringList bphList READ bphList CONSTANT)
    Q_PROPERTY(QStringList simBphList READ simBphList CONSTANT)

    // [측정 대기] Warm-up delay setting
    Q_PROPERTY(int warmupDelayIndex READ warmupDelayIndex WRITE setWarmupDelayIndex NOTIFY warmupDelayIndexChanged)
    Q_PROPERTY(QStringList warmupDelayList READ warmupDelayList CONSTANT)

    // Control Panel collapse state (QML ↔ C++ two-way binding)
    Q_PROPERTY(bool controlPanelCollapsed READ controlPanelCollapsed WRITE setControlPanelCollapsed NOTIFY controlPanelCollapsedChanged)
    // [설정 팝업] 사이드바 톱니바퀴 → 가운데 설정 팝업 표시 상태 (QML ↔ C++)
    Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged)

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // =========================================================================
    // QML Invokable Operations
    // =========================================================================
    Q_INVOKABLE void startSession();
    Q_INVOKABLE void stopSession();
    Q_INVOKABLE void togglePauseSession();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE bool choosePlaybackFile();
    Q_INVOKABLE void onControlPanelToggled(bool collapsed);
    // [QR] 현재 Watch ID 의 웹 이력 QR 을 화면 가운데 모달로 표시(사이드바 버튼).
    Q_INVOKABLE void showHistoryQr();

    // Getters & Setters for Q_PROPERTY
    int currentMode() const;
    void setCurrentMode(int mode);

    double gain() const;
    void setGain(double newGain);

    QStringList deviceList() const;
    int deviceIndex() const;
    void setDeviceIndex(int idx);

    QStringList sampleRateList() const;
    int sampleRateIndex() const;
    void setSampleRateIndex(int idx);

    int averagingPeriodIndex() const;
    void setAveragingPeriodIndex(int idx);

    QString selectedWavFile() const;
    bool isRunning() const;
    bool isPaused() const;
    bool inWarmup() const;
    QString detectedPosition() const;

    bool recordSessionEnabled() const;
    void setRecordSessionEnabled(bool enabled);

    int detectorBphIndex() const;
    void setDetectorBphIndex(int idx);

    int liftAngle() const;
    void setLiftAngle(int angle);

    int simBphIndex() const;
    void setSimBphIndex(int idx);

    int simErrorRate() const;
    void setSimErrorRate(int val);

    int simAmplitude() const;
    void setSimAmplitude(int val);

    double simBeatError() const;
    void setSimBeatError(double val);

    bool simRealistic() const;
    void setSimRealistic(bool val);

    int highPassCutoff() const;
    void setHighPassCutoff(int val);

    bool useConset() const;
    void setUseConset(bool val);

    QString watchId() const;
    void setWatchId(const QString &id);

    QString engineer() const;
    void setEngineer(const QString &name);

    QObject *positionTiming() const;

    QStringList averagingPeriodList() const { return mAveragingPeriodList; }
    QStringList bphList() const { return mBphList; }
    QStringList simBphList() const { return mSimBphList; }

    int warmupDelayIndex() const { return mWarmupDelayIndex; }
    void setWarmupDelayIndex(int idx);
    QStringList warmupDelayList() const { return mWarmupDelayList; }

    bool controlPanelCollapsed() const { return mControlPanelCollapsed; }
    void setControlPanelCollapsed(bool collapsed);

    bool settingsOpen() const { return mSettingsOpen; }
    void setSettingsOpen(bool open);

signals:
    void currentModeChanged();
    void gainChanged();
    void deviceListChanged();
    void deviceIndexChanged();
    void sampleRateListChanged();
    void sampleRateIndexChanged();
    void averagingPeriodIndexChanged();
    void selectedWavFileChanged();
    void isRunningChanged();
    void isPausedChanged();
    void inWarmupChanged();
    void detectedPositionChanged();
    void recordSessionEnabledChanged();
    void detectorBphIndexChanged();
    void liftAngleChanged();
    void simBphIndexChanged();
    void simErrorRateChanged();
    void simAmplitudeChanged();
    void simBeatErrorChanged();
    void simRealisticChanged();
    void highPassCutoffChanged();
    void useConsetChanged();
    void watchIdChanged();
    void engineerChanged();
    void controlPanelCollapsedChanged();
    void settingsOpenChanged();
    void warmupDelayIndexChanged();

public slots:
    void HandlePlaybackDoneReadingFile();
    void HandleSimDone();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    Ui::MainWindow *ui;
    TabManager     *mTabManager = nullptr;
    TabRateScope   *mRateScope  = nullptr;
    TabBeatErrorTrace *mBedTab = nullptr;
    TabSequenceDisplay *mSequenceDisplay = nullptr;
    QLabel         *mSeekLabel  = nullptr;
    WaveLodHistory  mWaveHistory;

    QQuickWidget   *mControlPanelQuickWidget = nullptr; // Embedding QML widget
    QQuickWidget   *mPositionToast = nullptr;           // [포지션 토스트] 탭 위 플로팅 알림(투명)
    QTimer         *mPositionToastHideTimer = nullptr;  // [포지션 토스트] 표시 후 자동 숨김

    void   RegisterDisplayTabs(void);
    void   PublishMeasurementToTabs(void);
    void   applyPanelLayout(void);   // [설정 팝업] 사이드바/전체화면(설정 창) 지오메트리 적용
    void   updateSeekLabel(double absSample);
    void   onPositionPhaseChanged(const QString &positionName, const QString &phaseLabel, int remainingSec);
    void   onPositionMeasurementEnded(int positionIndex, const QString &positionName,
                                      const QString &nextPositionName, bool sequenceComplete);
    void   onAllPositionsMeasured();
    void   ConfigureSoundCard(void);
    void   Reset(void);
    void   onWatchdogEvent(const WatchdogEvent &ev);  // 오디오 장치 분리 시 자동 정지 + 초기화
    void   LoadAudioDevices(void);
    bool   OpenFile(const QString &FileName);
    void   EventsReset(void);
    bool   RecordSessionCheck(void);
    void   AudioCloseCheck(void);
    
    // Internal Helper Helpers
    bool   SetAudioRate(int Rate);
    bool   SetAudioDevice(QString Name);
    void   GetAudioRate(int &Rate);
    void   GetAudioDevice(QString &Name);
    void   PopulateSampleRates(const QAudioDevice &device);
    void   pushCaptureConfig(void);
    void   DisplayResults(void);
    void   updateDetectedPositionUiSync(const QString &detectedLabel);
    static int sequenceIndexFromDetectedPosition(const QString &detectedLabel);
    void   LoadBPH(void);
    void   LoadSimBPH(void);
    void   LoadMode(void);
    void   LoadAverageingPeriod(void);
    
    void   LiveStart(void);
    void   PlaybackStart(void);
    void   SimStart(void);

    void   startWarmup(int seconds);
    void   cancelWarmup();
    void   onWarmupFinished();
    // [측정 대기] 포지션 활성화 시 컨트롤러가 요청 → warm-up 수행 후 측정 시작.
    void   onSequenceWarmupRequested(bool firstPosition);
    
    void   SyncDetectorBphToSimBph(void);
    bool   SetPlaybackFile(const QString &fileName);
    void   SetGuiRunMode(void);
    void   SetGuiStopMode(void);
    void   clearPauseState(void);
    void   updatePauseStatusMessage(void);
    // 세션 종료 공통 정리(수동 Stop·Playback/Sim 자동 종료). wasRunning==false 면 no-op(중복 호출 방지).
    void   finishSession(bool runDiag);
    void   triggerDiagnosis();

    WavStreamWriter           *mWavWriter= nullptr;
    MeasurementEngine          mEngine;
    CaptureController         *mCapture= nullptr;
    EventHandler              *mEventHandler= nullptr;  // 워치독 이벤트 → 알림 표시(severity 별)
    PositionTimingModel       *mPositionTiming = nullptr;
    PositionSequenceController *mPositionSequence = nullptr;
    
    int                        mAvalableRates[5];
    int                        mNumberofRates;

    // Binding backend storage values
    int                        mCurrentMode = 0;
    double                     mGain = 0.1;
    QList<QAudioDevice>        mAudioInputDevices;
    QStringList                mDeviceList;
    QStringList                mSampleRateList;
    QStringList                mBphList;
    QStringList                mSimBphList;
    QStringList                mAveragingPeriodList;
    QStringList                mWarmupDelayList;
    int                        mWarmupDelayIndex = 1;   // 기본 5초 (0/5/10/15/20)
    bool                       mInWarmup = false;
    WarmupOverlay             *mWarmupOverlay = nullptr;

    // [MPS 포지션 전환] 대기 팝업~다음 포지션 안정화 동안 상단 요약바(readout) 고정.
    //  포지션 측정 종료 시 true → 다음 포지션이 'measuring' 진입하면 false.
    bool                       mReadoutFrozen = false;

    // [측정 대기] 현재 진행 중인 warm-up 이 포지션 전환용인지(true) 세션 시작용인지(false).
    //  종료 시 시퀀스 누적표 보존 여부를 결정.
    bool                       mWarmupIsPositionChange = false;

    int                        mDeviceIndex = -1;
    int                        mSampleRateIndex = -1;
    int                        mAveragingPeriodIndex = -1;
    int                        mDetectorBphIndex = -1;
    int                        mSimBphIndex = -1;
    int                        mSimErrorRate = 0;
    int                        mSimAmplitude = 300;
    double                     mSimBeatError = 0.0;
    bool                       mSimRealistic = true;
    int                        mHighPassCutoff = 200;
    bool                       mUseConset = false;
    QString                    mWatchId;
    QString                    mEngineer;

    bool                       mIsRunning = false;
    bool                       mIsPaused = false;
    QString                    mLastPhaseName;
    QString                    mLastPhaseLabel;
    int                        mLastPhaseRemainingSec = 0;
    bool                       mRecordSessionEnabled = false;
    QString                    mDetectedPosition;
    int                        mDetectedStableCandidateIndex = -1;
    int                        mDetectedConfirmedIndex = -1;
    QElapsedTimer              mDetectedStableTimer;
    PositionChangeDialog      *mActivePositionDialog = nullptr;
    bool                       mControlPanelCollapsed = false;
    bool                       mSettingsOpen = false;   // [설정 팝업] 가운데 설정 창 표시 여부

    double                     mLiftAngle;
    int                        mAveragingPeriod;
    QDir                       mCurrentDir;
    int                        mCurrentSamplesPerSecond;
    int                        mRateBeforePlaybackOrSim;
    QString                    mDeviceNameBeforePlaybackOrSim;
    QString                    mPlaybackFileName;
    int                        mLastMode = -1;
    ReadoutBar                *mReadoutBar = nullptr;
#ifdef ENABLE_VISION
    QThread              *mVisionThread = nullptr;   // [vision] 추론 워커 스레드(병렬 실행)
    vision::VisionWorker *mVisionWorker = nullptr;   // [vision] 웹캠 1Hz watch-position 추론
#endif
#ifdef ENABLE_DIAG
    QThread          *mDiagThread = nullptr;         // [diag] 진단 워커 스레드(병렬 실행)
    diag::DiagWorker *mDiagWorker = nullptr;         // [diag] Stop 시 t1/t3 고장유형 진단
    DiagBanner       *mDiagBanner = nullptr;         // [diag] 진단 완료 알림 배너(우측 상단)
    QString           mLastDiagKey;                  // [diag] 마지막 진단 라벨 키(상세창용)
    QString           mLastDiagTitle;                // [diag] 배너 표시용 사람이 읽는 제목
    float             mLastDiagConf = 0.0f;          // [diag] 마지막 진단 신뢰도
    bool              mDiagBannerPending = false;    // [diag] 미확인 진단 결과 — BED 탭에서만 배너 노출
    // [diag] 배너는 Beat Error Display and Diagnostic Trace 탭에서만 보인다.
    //  보류된 결과가 있고 현재 탭이 BED 탭이면 노출, 아니면 숨김(탭 전환 시 호출).
    void   updateDiagBannerVisibility();
#endif
};
#endif // MAINWINDOW_H
