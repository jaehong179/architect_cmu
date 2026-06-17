// AudioWorker.h
#ifndef AUDIOWORKER_H
#define AUDIOWORKER_H

#include <QObject>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QAudioSource>
#include <QAudioSink>
#include <QBuffer>
#include <QIODevice>
#include <QByteArray>
#include <QDebug>
#include <QMutex>
#include <QElapsedTimer>
#include "SharedAudio.h"


class TAudioWorker : public QObject
{
    Q_OBJECT

public:
    TAudioWorker(TMasterAudioDataRaw *RawAudio,QObject *parent = nullptr);
     ~TAudioWorker();
    TMasterAudioDataRaw *mRawAudio;
public slots:
    void StartAudioRecording(QAudioDevice InputDevice,int SampleRate,float Volume);
    void SetAudioInputVolume(float Volume);
    void StopAudioRecording();

private slots:
    void stateChangeAudioInput(QAudio::State s);
    void ProcessAudioInput();

signals:
    // Signal to send captured audio data to the main thread (e.g., for processing/visualization)
    void AudioDataReady();
    void finished();

private:
    QAudioSource *mAudioInput = nullptr;
    QIODevice    *mAudioInputDevice = nullptr;
    bool          TimerStarted=false;
    double        LastTime=0.0;
    uint64_t      FrameCount=0;
    uint64_t      SampleCount=0;
    QElapsedTimer Timer;
    // ── [PERF 계측 · §B-1/B-3 · QA-RT-02] 드롭 추정/실효 처리량용 (측정 전용) ──
    int           mSampleRate=0;        // StartAudioRecording 에서 받은 캡처 샘플레이트
    double        mLastDropLogGap=0.0;  // 직전 로그 시점의 capture_gap(샘플) — 증가분(드롭 신호) 계산용
    int           mLastAudioErr=0;      // [B-1] QAudioSource::error() 직전값(변화 시만 로그). 0=NoError
};

#endif // AUDIOWORKER_H
