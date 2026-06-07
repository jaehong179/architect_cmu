// SimWorker.cpp
#include <QtGlobal>
#include <QFile>
#include <QThread>
#include <QDebug>
#include <cstring>            // [PERF 계측] memset (GT 링 초기화)
#include "SimWorker.h"

#if defined(Q_OS_WIN)
#define SIM_SAMPLE_PERIOD_MSEC 10
#define DELAY_FUGE_TIME_MS 1
#elif defined(Q_OS_LINUX)
#define SIM_SAMPLE_PERIOD_MSEC 20
#define DELAY_FUGE_TIME_MS 1
#elif defined(Q_OS_APPLE)
#define SIM_SAMPLE_PERIOD_MSEC 10
#define DELAY_FUGE_TIME_MS 1
#elif defined(Q_OS_ANDROID)
#define SIM_SAMPLE_PERIOD_MSEC 20
#define DELAY_FUGE_TIME_MS 1
#endif

#define SIM_NUMBER_OF_SAMPLES (mSamplesPerSecond/(1000/SIM_SAMPLE_PERIOD_MSEC))

TSimWorker::TSimWorker(TMasterAudioDataRaw *RawAudio,int SamplesPerSecond,QObject *parent) : QObject(parent)
{
    mRawAudio=RawAudio;
    mRawAudio->TotalSamplesWritten=0;
    mRawAudio->WriteIndex=0;
    mRawAudio->MainThrd_LastTotalSamplesWritten=0;
    mRawAudio->MainThrd_LastWriteIndex=0;
    mRawAudio->FPS=0.0;
    mRawAudio->SPF=0.0;
    mRawAudio->SPS=0.0;
    mTimerStarted=false;
    mSamplesPerSecond=SamplesPerSecond;
    mLastTime=0.0;
    mFrameCount=0;
    mSampleCount=0;
    mDataInSize=SIM_NUMBER_OF_SAMPLES;
    mDataIn= new float[mDataInSize];
    // [PERF 계측 · §E/§G-2] Sim 정답(ground-truth) 이벤트 링 초기화
    memset(mRawAudio->GtBeats, 0, sizeof(mRawAudio->GtBeats));
    mRawAudio->GtHead=0;
    mRawAudio->GtTotal=0;
}

TSimWorker::~TSimWorker()
{
    delete [] mDataIn;
    // Clean up if necessary
    qInfo() << "SimWorker Destructor";
}


void TSimWorker::StartSim(WatchSynthStreamConfig cfg)
{
    int                        BytesIn;
    double                     CurrentTime;
    qint64                     Start,Delta,SleepTime;
    char                       err[256];
    WatchSynthStream           stream;
    WatchSynthStreamEvent      events[16];
    WatchSynthStreamFillResult r;
    cfg.sample_rate_hz=mSamplesPerSecond;

    if (!watch_synth_stream_init(&stream, &cfg, err, sizeof(err)))
    {
        fprintf(stderr, "init failed: %s\n", err);
        emit SimDone();
        emit finished();
        return;
    }

    if (!mTimerStarted)
    {
        mTimerStarted=true;
        mTimer.start();
    }

    while (1)
    {
        Start=mTimer.elapsed();

        r = watch_synth_stream_fill_f32(&stream,  (float *)mDataIn, mDataInSize, events, 16);
        if (r.samples_written != mDataInSize) {
            fprintf(stderr, "short fill\n");
            break;
        }
        if (QThread::currentThread()->isInterruptionRequested())
        {
            break; // Exit loop early
        }
        unsigned int NumberOfSamples=r.samples_written;

        mRawAudio->Mutex.lock();
        unsigned int TempWriteIndex = mRawAudio->WriteIndex;
        mRawAudio->Mutex.unlock();
        int SamplesLeft=std::min(NumberOfSamples,mRawAudio->NumberOfAudioSamples-TempWriteIndex);
        memcpy(&mRawAudio->Samples[TempWriteIndex], mDataIn, SamplesLeft * SAMPLE_SIZE);
        if(SamplesLeft < NumberOfSamples)
        {
            memcpy(mRawAudio->Samples, &mDataIn[SamplesLeft], (NumberOfSamples - SamplesLeft) * SAMPLE_SIZE);
            qInfo() << "MasterPlaybackData Samples Rollover";
        }
        mRawAudio->Mutex.lock();
        mRawAudio->WriteIndex = (TempWriteIndex+ NumberOfSamples) %  mRawAudio->NumberOfAudioSamples;
        mRawAudio->TotalSamplesWritten+=NumberOfSamples;
        // ── [PERF 계측 · §E/§G-2 · QA-AC-01/02/03] 이번 블록의 정답(ground-truth) 이벤트 적재 ──
        //  합성기가 알려준 각 비트의 A(onset) 절대샘플과, A→C 시간으로 환산한 C 절대샘플을
        //  링버퍼에 넣는다. 메인 스레드가 검출 이벤트와 대조하여 타이밍/검출 오차를 측정.
        for (size_t e=0; e<r.events_written; ++e)
        {
            uint64_t a = events[e].sample_index;
            uint64_t c = a + (uint64_t)(events[e].a_to_c_time_s * (double)mSamplesPerSecond + 0.5);
            mRawAudio->GtBeats[mRawAudio->GtHead].a_sample = a;
            mRawAudio->GtBeats[mRawAudio->GtHead].c_sample = c;
            mRawAudio->GtHead = (mRawAudio->GtHead + 1) % GT_EVENT_RING;
            mRawAudio->GtTotal++;
        }
        mRawAudio->Mutex.unlock();
        emit SimDataReady(); // Emit data to the main thread

        ++mFrameCount;
        mSampleCount+=NumberOfSamples;
        CurrentTime = mTimer.elapsed()/1000.0;
        if (CurrentTime-mLastTime > 2) // average fps over 2 seconds
        {
            double fdelta;
            fdelta=CurrentTime-mLastTime;
            mRawAudio->FPS=mFrameCount/fdelta;
            mRawAudio->SPS=mSampleCount/fdelta;
            mRawAudio->SPF=mSampleCount/mFrameCount;
            mLastTime=CurrentTime;
            mFrameCount=0;
            mSampleCount=0;
        }
        Delta=(mTimer.elapsed()-Start)+DELAY_FUGE_TIME_MS;
        SleepTime=SIM_SAMPLE_PERIOD_MSEC-Delta;
        if (SleepTime<0) SleepTime=0;
        QThread::msleep(SleepTime);
    }
    emit SimDone();
    emit finished();
    qInfo()<<"After Finish";
}

