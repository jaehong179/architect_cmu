#ifndef CAPTURECONTROLLER_H
#define CAPTURECONTROLLER_H
// CaptureController — 오디오 소스(실시간 캡처·파일 재생·합성)의 스레드·워커·공유버퍼 관리.
//  MainWindow 에서 추출한 '입력 소스 오케스트레이션'. 워커가 채운 신호 블록을 dataReady() 로
//  알리며, 실제 신호 처리(ProcessSamples)는 소비측(현재 MainWindow)이 담당한다.
//   [성능] 핫패스(샘플 처리)는 건드리지 않는다. 블록 단위 dataReady 신호만 추가(저빈도).
#include <QObject>
#include <QString>
#include <QAudioDevice>
#include "SharedAudio.h"
#include "WatchSynthStream.h"
class TAudioWorker;
class TPlaybackWorker;
class TSimWorker;
class QThread;

class CaptureController : public QObject
{
    Q_OBJECT
public:
    explicit CaptureController(QObject *parent = nullptr);
    ~CaptureController() override;

    void startLive(const QAudioDevice &device, int sampleRate, float micVol);
    void startPlayback(const QString &fileName, int sampleRate);
    void startSim(const WatchSynthStreamConfig &cfg, int sampleRate);
    void stopLive();
    void stopPlayback();
    void stopSim();
    void setInputVolume(float vol) { emit localSetAudioInputVolume(vol); }

signals:
    void dataReady(TMasterAudioDataRaw *raw);   // 워커가 새 블록을 채움(메인 스레드로 큐 전달)
    void playbackDoneReadingFile();             // 재생 파일 끝
    void simDone();                             // 합성 종료
    // 워커 제어(컨트롤러 → 워커 스레드)
    void localStartAudio(QAudioDevice device, int sampleRate, float volume);
    void localStopAudio();
    void localSetAudioInputVolume(float volume);
    void localStartPlayback(const QString &fileName);
    void localStartSim(WatchSynthStreamConfig cfg);

private slots:
    void onAudioData()    { emit dataReady(mRawAudio); }
    void onPlaybackData() { emit dataReady(mRawAudio); }
    void onSimData()      { emit dataReady(mRawAudio); }

private:
    void allocBuffer(int sampleRate);   // 공유 링버퍼 (재)할당

    TMasterAudioDataRaw *mRawAudio = nullptr;
    QThread         *mAudioThread    = nullptr;  TAudioWorker    *mAudioWorker    = nullptr;
    QThread         *mPlaybackThread = nullptr;  TPlaybackWorker *mPlaybackWorker = nullptr;
    QThread         *mSimThread      = nullptr;  TSimWorker      *mSimWorker      = nullptr;
};
#endif // CAPTURECONTROLLER_H
