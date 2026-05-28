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
};

#endif // AUDIOWORKER_H
