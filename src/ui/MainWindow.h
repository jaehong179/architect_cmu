#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDir>
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
class TabManager;   
class TabRateScope; 
class TabSequenceDisplay;
class ReadoutBar;   

#ifdef ENABLE_VISION
class QThread;                                  // [vision] 추론 워커 전용 스레드
namespace vision { class VisionWorker; }        // [vision] 웹캠 1Hz watch-position 추론
#endif
#ifdef ENABLE_DIAG
class QThread;                                  // [diag] 진단 워커 전용 스레드
namespace diag { class DiagWorker; }            // [diag] t1/t3 고장유형 진단 추론
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
    Q_PROPERTY(QObject* positionTiming READ positionTiming CONSTANT)

    // Constant lists for QML Combobox Models
    Q_PROPERTY(QStringList averagingPeriodList READ averagingPeriodList CONSTANT)
    Q_PROPERTY(QStringList bphList READ bphList CONSTANT)
    Q_PROPERTY(QStringList simBphList READ simBphList CONSTANT)

    // Control Panel collapse state (QML ↔ C++ two-way binding)
    Q_PROPERTY(bool controlPanelCollapsed READ controlPanelCollapsed WRITE setControlPanelCollapsed NOTIFY controlPanelCollapsedChanged)

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // =========================================================================
    // QML Invokable Operations
    // =========================================================================
    Q_INVOKABLE void startSession();
    Q_INVOKABLE void stopSession();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE bool choosePlaybackFile();
    Q_INVOKABLE void onControlPanelToggled(bool collapsed);

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

    QObject *positionTiming() const;

    QStringList averagingPeriodList() const { return mAveragingPeriodList; }
    QStringList bphList() const { return mBphList; }
    QStringList simBphList() const { return mSimBphList; }

    bool controlPanelCollapsed() const { return mControlPanelCollapsed; }
    void setControlPanelCollapsed(bool collapsed);

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
    void controlPanelCollapsedChanged();

public slots:
    void HandlePlaybackDoneReadingFile();
    void HandleSimDone();

private:
    Ui::MainWindow *ui;
    TabManager     *mTabManager = nullptr;
    TabRateScope   *mRateScope  = nullptr;
    TabSequenceDisplay *mSequenceDisplay = nullptr;
    QLabel         *mSeekLabel  = nullptr;
    WaveLodHistory  mWaveHistory;

    QQuickWidget   *mControlPanelQuickWidget = nullptr; // Embedding QML widget

    void   RegisterDisplayTabs(void);
    void   PublishMeasurementToTabs(void);
    void   updateSeekLabel(double absSample);
    void   onPositionPhaseChanged(const QString &positionName, const QString &phaseLabel, int remainingSec);
    void   onPositionMeasurementEnded(int positionIndex, const QString &positionName,
                                      const QString &nextPositionName, bool sequenceComplete);
    void   ConfigureSoundCard(void);
    void   Reset(void);
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
    void   LoadBPH(void);
    void   LoadSimBPH(void);
    void   LoadMode(void);
    void   LoadAverageingPeriod(void);
    
    void   LiveStart(void);
    void   PlaybackStart(void);
    void   SimStart(void);
    
    void   SyncDetectorBphToSimBph(void);
    bool   SetPlaybackFile(const QString &fileName);
    void   SetGuiRunMode(void);
    void   SetGuiStopMode(void);

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

    bool                       mIsRunning = false;
    bool                       mRecordSessionEnabled = false;
    QString                    mDetectedPosition;
    bool                       mControlPanelCollapsed = false;

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
#endif
};
#endif // MAINWINDOW_H
