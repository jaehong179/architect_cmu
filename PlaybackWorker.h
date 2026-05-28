// PlaybackWorker.h
#ifndef PLAYBACKWORKER_H
#define PLAYBACKWORKER_H
#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include "SharedAudio.h"

class TPlaybackWorker : public QObject
{
    Q_OBJECT

public:
    TPlaybackWorker(TMasterAudioDataRaw *RawAudio,int SamplesPerSecond,QObject *parent = nullptr);
     ~TPlaybackWorker();
    TMasterAudioDataRaw *mRawAudio;
public slots:
    void StartPlayback(const QString &FileName);


signals:
    // Signal to send captured audio data to the main thread (e.g., for processing/visualization)
    void PlaybackDataReady();
    void PlaybackDoneReadingFile();
    void finished();
    void cancelled();

private:
    bool          mTimerStarted=false;
    double        mLastTime=0.0;
    uint64_t      mFrameCount=0;
    uint64_t      mSampleCount=0;
    QElapsedTimer mTimer;
    int           mSamplesPerSecond;
    char          *mDataIn;
    int           mDataInSize;
};

#endif // PLAYBACKWORKER_H
