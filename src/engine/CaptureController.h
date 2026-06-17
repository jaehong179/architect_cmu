#ifndef CAPTURECONTROLLER_H
#define CAPTURECONTROLLER_H
// CaptureController — 입력 소스 + 신호 파이프라인 오케스트레이션 (MainWindow 에서 추출).
//  · 오디오 소스: 캡처/재생/합성 워커·스레드·공유버퍼 관리.
//  · 파이프라인: 검출기(Timegrapher) 구동, 샘플 처리, A/C 이벤트 → MeasurementEngine,
//    스코프 탭에 WaveBlock 게시, perf(지연·FPS·정확도) 기록.
//  UI(상태바·readout)는 시그널로 알리고, 측정값 표시는 소비측(MainWindow)이 담당한다.
//   [성능] 핫패스(샘플·이벤트 처리)는 전부 직접 호출. UI 시그널은 저빈도(블록/비트당)만 → 샘플당 오버헤드 0.
#include <QObject>
#include <QString>
#include <QAudioDevice>
#include <QElapsedTimer>
#include "SharedAudio.h"
#include "WatchSynthStream.h"
#include "Timegrapher.h"
class TAudioWorker;
class TPlaybackWorker;
class TSimWorker;
class QThread;
class MeasurementEngine;
class TabManager;
class WavStreamWriter;

class CaptureController : public QObject
{
    Q_OBJECT
public:
    // engine/tabs 는 소유하지 않음(MainWindow). 파이프라인이 측정계산·탭게시에 사용.
    CaptureController(MeasurementEngine *engine, TabManager *tabs, QObject *parent = nullptr);
    ~CaptureController() override;

    // 세션 시작/정지 (start* 는 검출기 생성 + 파이프라인 리셋 + 워커 스레드 기동).
    void startLive(const QAudioDevice &device, int sampleRate, float micVol);
    void startPlayback(const QString &fileName, int sampleRate);
    void startSim(const WatchSynthStreamConfig &cfg, int sampleRate);
    void stopLive();
    void stopPlayback();
    void stopSim();

    // 설정 주입 — start 전/런타임 변경 시 MainWindow 가 갱신.
    void setEngineParams(int sampleRate, int averagingPeriod, int liftAngle)
        { mSampleRate = sampleRate; mAveragingPeriod = averagingPeriod; mLiftAngle = liftAngle; }
    void setDetectorConfig(bool bphAuto, int manualBph, double hpfCutoffHz)
        { mBphAuto = bphAuto; mManualBph = manualBph; mHpfCutoffHz = hpfCutoffHz; }
    void setUseConset(bool v) { mUseConset = v; }
    void setInputVolume(float vol) { emit localSetAudioInputVolume(vol); }
    void setWavWriter(WavStreamWriter *w) { mWavWriter = w; }   // 녹음 대상(소유 안 함)

    uint64_t totalSamples() const { return mLocalTotalSamplesWritten; }  // 탭 스냅샷 게시용

public slots:
    void onScopeReplotted();   // 탭 ScopePlot afterReplot → disp_paint_ms·e2e_full_ms·paint_fps

signals:
    void statusMessage(const QString &msg);     // → MainWindow 상태바(FPS/정지 메시지)
    void measurementReady();                    // → MainWindow DisplayResults(readout + 탭 게시)
    void playbackDoneReadingFile();
    void simDone();
    // 워커 제어(컨트롤러 → 워커 스레드)
    void localStartAudio(QAudioDevice device, int sampleRate, float volume);
    void localStopAudio();
    void localSetAudioInputVolume(float volume);
    void localStartPlayback(const QString &fileName);
    void localStartSim(WatchSynthStreamConfig cfg);

private slots:
    void onAudioData()    { handleInputData(mRawAudio); }
    void onPlaybackData() { handleInputData(mRawAudio); }
    void onSimData()      { handleInputData(mRawAudio); }
    void onSimWorkerDone() { mSimActive = false; emit simDone(); }

private:
    void allocBuffer(int sampleRate);
    void createDetectors();
    void deleteDetectors();
    void resetPipeline();                       // 세션 시작 시 파이프라인 상태 비움
    void handleInputData(TMasterAudioDataRaw *p);
    void processSamples(TMasterAudioDataRaw *p);
    void aEvent(double t, bool haveValidBph, double bph);
    void cEvent(double t, bool haveValidBph, double bph);
    void matchGroundTruth(double val, bool isAEvent);   // [PERF] 검출 vs Sim 정답 대조(A/C 공용)

    MeasurementEngine *mEngine = nullptr;       // 소유 안 함(MainWindow)
    TabManager        *mTabs   = nullptr;        // 소유 안 함
    WavStreamWriter   *mWavWriter = nullptr;     // 소유 안 함(MainWindow 녹음 흐름)

    // ── 오디오 소스 ──
    TMasterAudioDataRaw *mRawAudio = nullptr;
    QThread *mAudioThread = nullptr;     TAudioWorker    *mAudioWorker    = nullptr;
    QThread *mPlaybackThread = nullptr;  TPlaybackWorker *mPlaybackWorker = nullptr;
    QThread *mSimThread = nullptr;       TSimWorker      *mSimWorker      = nullptr;

    // ── 검출기(Timegrapher) ──
    tg_config_t   mTgCfg;
    tg_context_t *mCtx = nullptr;
    float        *mInputBlock = nullptr;

    // ── 설정 ──
    int    mSampleRate = 48000, mAveragingPeriod = 20, mLiftAngle = 52;
    bool   mBphAuto = true;  int mManualBph = 0;  double mHpfCutoffHz = 200.0;
    bool   mUseConset = false;
    bool   mLive = false;        // perf: Live 모드(e2e 지연 의미 있음)
    bool   mSimMode = false;     // GT 스냅샷 대상(Sim)

    // ── 파이프라인 상태 ──
    unsigned int mLocalWriteIndex = 0;
    uint64_t     mLocalTotalSamplesWritten = 0;
    uint64_t     mInputAbsSample = 0;

    // ── perf ──
    double   mLocalLastBlockCaptureMs = 0.0;  uint64_t mLocalDroppedSamples = 0;
    double   mPerfReplotRequestMs = 0.0, mPerfCaptureForReplotMs = 0.0;
    bool     mPerfReplotLive = false, mPerfReplotPending = false;
    uint64_t mPaintCount = 0, mReplotReqCount = 0;  double mPaintLastEmitMs = 0.0;  bool mPaintHave = false;
    // FPS(전경 핸들러)
    double mBackgroundLastFPS = 0, mBackgroundLastSPF = 0, mBackgroundLastSPS = 0;
    double mForegroundFPS = 0, mForegroundSPF = 0, mForegroundSPS = 0;
    double mForegroundLastFPS = 0, mForegroundLastSPF = 0, mForegroundLastSPS = 0;
    bool mForegroundTimerStarted = false;  QElapsedTimer mForegroundTimer;
    double mForegroundLastTime = 0.0;  uint64_t mForegroundFrameCount = 0, mForegroundSampleCount = 0;

    // ── Sim 정답(GT)/정확도 ──
    bool mSimActive = false;  WatchSynthStreamConfig mLastSimCfg;
    TGtBeat mLocalGt[GT_EVENT_RING];  unsigned int mLocalGtHead = 0;  uint64_t mLocalGtTotal = 0;  bool mLocalGtValid = false;
};
#endif // CAPTURECONTROLLER_H
