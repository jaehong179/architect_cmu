// SimWorker.h
#ifndef SIMWORKER_H
#define SIMWORKER_H
#include <QObject>
#include <QMutex>
#include <QElapsedTimer>
#include "SharedAudio.h"
#include "WatchSynthStream.h"

class TSimWorker : public QObject
{
    Q_OBJECT

public:
    TSimWorker(TMasterAudioDataRaw *RawAudio,int SamplesPerSecond,QObject *parent = nullptr);
     ~TSimWorker();
    TMasterAudioDataRaw *mRawAudio;
public slots:
    void StartSim(WatchSynthStreamConfig cfg);


signals:
    // Signal to send captured audio data to the main thread (e.g., for processing/visualization)
    void SimDataReady();
    void SimDone();
    void finished();
    void cancelled();

private:
    bool                    mTimerStarted=false;
    double                  mLastTime=0.0;
    uint64_t                mFrameCount=0;
    uint64_t                mSampleCount=0;
    QElapsedTimer           mTimer;
    int                     mSamplesPerSecond;
    float                   *mDataIn;
    int                     mDataInSize;
};

#endif // SIMWORKER_H
