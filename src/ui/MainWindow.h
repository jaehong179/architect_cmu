#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QDir>
#include <QMainWindow>
#include <QComboBox>
#include "WavStreamWriter.h"      // mWavWriter (녹음 대상)
#include "MeasurementEngine.h"    // mEngine (측정 계산)
#include "CaptureController.h"    // mCapture (오디오 소스/파이프라인) — tg_*·TMasterAudioDataRaw 도 여기서 전이 제공
#include "SimConfigBuilder.h"     // SimStart 의 합성 설정 조립(WatchSynthStreamConfig 포함)
#include "WaveLodHistory.h"       // 8분 엔벨로프 이력 버퍼(중앙 1개, pause/스크롤백용)


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class TabManager;   // [탭 모듈] 디스플레이 탭 등록·갱신 브로드캐스트 (tabs/TabManager.h)
class TabRateScope; // Rate/Scope 탭 — ScopePlot afterReplot 신호를 CaptureController perf 로 연결
class QPushButton;  // 전역 Pause/Resume(탭바 코너위젯)
class QLabel;       // 전역 seek 위치 표시(코너)

#define AUDIO_OUTPUT 0
#define DEBUG_OUTPUT 0

// rate/beat/amplitude 측정 상태·계산은 MeasurementEngine 로 분리됨.


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
    void HandlePlaybackDoneReadingFile();
    void HandleSimDone();

private:
    Ui::MainWindow *ui;
    TabManager     *mTabManager = nullptr;   // [탭 모듈] 디스플레이 탭을 등록·갱신(QA-MOD-01)
    TabRateScope   *mRateScope  = nullptr;    // Rate/Scope 탭(perf afterReplot → 컨트롤러 배선용)
    QPushButton    *mPauseBtn   = nullptr;    // 전역 Pause/Resume(탭바 코너 — 모든 탭에 표시)
    QLabel         *mSeekLabel  = nullptr;    // 전역 seek 위치(t/샘플) — 모든 탭에서 보임
    WaveLodHistory  mWaveHistory;             // 8분 엔벨로프 이력(중앙 1개) — TabManager 방송에 청취자로 등록
    void   RegisterDisplayTabs(void);        // [탭 모듈] 신규 탭 모듈들을 생성·등록
    void   PublishMeasurementToTabs(void);   // [탭 모듈] 현재 측정값을 스냅샷으로 탭에 게시
    void   updateSeekLabel(double absSample);// [③] 코너 seek 위치 라벨 갱신(트렌드 클릭 공통)
    void   ConfigureSoundCard(void);
    void   Reset(void);
    void   LoadAudioDevices(void);
    bool   OpenFile(const QString &FileName);
    void   EventsReset(void);
    bool   RecordSessionCheck(void);
    void   AudioCloseCheck(void);
    bool   SetAudioRate(int Rate);
    bool   SetAudioDevice(QString Name);
    void   GetAudioRate(int &Rate);
    void   GetAudioDevice(QString &Name);
    void   PopulateSampleRates(QComboBox *comboBox, const QAudioDevice &device);
    void   pushCaptureConfig(void);          // 캡처/파이프라인 설정을 UI 에서 읽어 CaptureController 에 주입
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
    MeasurementEngine          mEngine;   // rate/beat/amplitude 측정 계산
    CaptureController         *mCapture= nullptr;  // 오디오 소스(스레드·워커·버퍼) 오케스트레이션
    int                        mAvalableRates[5];
    int                        mNumberofRates;
    double                     mLiftAngle;
    int                        mAveragingPeriod;
    QDir                       mCurrentDir;
    int                        mCurrentSamplesPerSecond;
    int                        mRateBeforePlaybackOrSim;
    QString                    mDeviceNameBeforePlaybackOrSim;
    // [PERF 계측 · §A-3] UI 이벤트 루프 응답성은 UiResponsivenessSampler 가 자체 타이머로 담당.
    //  (파이프라인 perf: cap2proc·proc2disp·e2e·disp_paint·fps·GT 정확도는 CaptureController 로 이동)
};
#endif
