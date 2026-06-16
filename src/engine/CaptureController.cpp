#include "CaptureController.h"
#include "AudioWorker.h"
#include "PlaybackWorker.h"
#include "SimWorker.h"
#include <QThread>

CaptureController::CaptureController(QObject *parent) : QObject(parent) {}

CaptureController::~CaptureController()
{
    if (mRawAudio) {
        if (mRawAudio->Samples) { delete[] mRawAudio->Samples; mRawAudio->Samples = nullptr; }
        delete mRawAudio; mRawAudio = nullptr;
    }
}

// 공유 링버퍼 (재)할당 (구 StartXThread 의 공통 블록).
void CaptureController::allocBuffer(int sampleRate)
{
    if (mRawAudio) {
        if (mRawAudio->Samples) { delete[] mRawAudio->Samples; mRawAudio->Samples = nullptr; }
        delete mRawAudio; mRawAudio = nullptr;
    }
    mRawAudio = new TMasterAudioDataRaw;
    mRawAudio->NumberOfAudioSamples = sampleRate * SECONDS_OF_BUFFER;
    mRawAudio->Samples = new float[mRawAudio->NumberOfAudioSamples];
}

void CaptureController::startLive(const QAudioDevice &device, int sampleRate, float micVol)
{
    allocBuffer(sampleRate);
    mAudioThread = new QThread();
    mAudioWorker = new TAudioWorker(mRawAudio);
    mAudioWorker->moveToThread(mAudioThread);

    connect(mAudioWorker, &TAudioWorker::finished, mAudioThread, &QThread::quit);
    connect(mAudioThread, &QThread::finished, mAudioWorker, &QObject::deleteLater);
    connect(mAudioThread, &QThread::finished, mAudioThread, &QObject::deleteLater);

    connect(mAudioWorker, &TAudioWorker::AudioDataReady, this, &CaptureController::onAudioData);
    connect(this, &CaptureController::localStartAudio,         mAudioWorker, &TAudioWorker::StartAudioRecording);
    connect(this, &CaptureController::localStopAudio,          mAudioWorker, &TAudioWorker::StopAudioRecording);
    connect(this, &CaptureController::localSetAudioInputVolume, mAudioWorker, &TAudioWorker::SetAudioInputVolume);

    mAudioThread->start(QThread::TimeCriticalPriority);
    emit localStartAudio(device, sampleRate, micVol);
}

void CaptureController::startPlayback(const QString &fileName, int sampleRate)
{
    allocBuffer(sampleRate);
    mPlaybackThread = new QThread();
    mPlaybackWorker = new TPlaybackWorker(mRawAudio, sampleRate);
    mPlaybackWorker->moveToThread(mPlaybackThread);

    connect(mPlaybackWorker, &TPlaybackWorker::finished, mPlaybackThread, &QThread::quit);
    connect(mPlaybackThread, &QThread::finished, mPlaybackWorker, &QObject::deleteLater);
    connect(mPlaybackThread, &QThread::finished, mPlaybackThread, &QObject::deleteLater);

    connect(this, &CaptureController::localStartPlayback, mPlaybackWorker, &TPlaybackWorker::StartPlayback);
    connect(mPlaybackWorker, &TPlaybackWorker::PlaybackDataReady,        this, &CaptureController::onPlaybackData);
    connect(mPlaybackWorker, &TPlaybackWorker::PlaybackDoneReadingFile,  this, &CaptureController::playbackDoneReadingFile);

    mPlaybackThread->start(QThread::TimeCriticalPriority);
    emit localStartPlayback(fileName);
}

void CaptureController::startSim(const WatchSynthStreamConfig &cfg, int sampleRate)
{
    allocBuffer(sampleRate);
    mSimThread = new QThread();
    mSimWorker = new TSimWorker(mRawAudio, sampleRate);
    mSimWorker->moveToThread(mSimThread);

    connect(mSimWorker, &TSimWorker::finished, mSimThread, &QThread::quit);
    connect(mSimThread, &QThread::finished, mSimWorker, &QObject::deleteLater);
    connect(mSimThread, &QThread::finished, mSimThread, &QObject::deleteLater);

    connect(this, &CaptureController::localStartSim, mSimWorker, &TSimWorker::StartSim);
    connect(mSimWorker, &TSimWorker::SimDataReady, this, &CaptureController::onSimData);
    connect(mSimWorker, &TSimWorker::SimDone,      this, &CaptureController::simDone);

    mSimThread->start(QThread::TimeCriticalPriority);
    emit localStartSim(cfg);
}

void CaptureController::stopLive()
{
    emit localStopAudio();
}

void CaptureController::stopPlayback()
{
    if (mPlaybackThread) mPlaybackThread->requestInterruption();
}

void CaptureController::stopSim()
{
    if (mSimThread) mSimThread->requestInterruption();
}
