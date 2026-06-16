// PlaybackWorker.cpp
#include <QtGlobal>
#include <QFile>
#include <QThread>
#include <QDebug>
#include "PlaybackWorker.h"
#include "WaveHeader.h"

#if defined(Q_OS_WIN)
#define PLAYBACK_SAMPLE_PERIOD_MSEC 10
#define DELAY_FUGE_TIME_MS 1
#elif defined(Q_OS_LINUX)
#define PLAYBACK_SAMPLE_PERIOD_MSEC 20
#define DELAY_FUGE_TIME_MS 1
#elif defined(Q_OS_APPLE)
#define PLAYBACK_SAMPLE_PERIOD_MSEC 10
#define DELAY_FUGE_TIME_MS 1
#elif defined(Q_OS_ANDROID)
#define PLAYBACK_SAMPLE_PERIOD_MSEC 20
#define DELAY_FUGE_TIME_MS 1
#endif

#define PLAYBACK_NUMBER_OF_SAMPLES (mSamplesPerSecond/(1000/PLAYBACK_SAMPLE_PERIOD_MSEC))

TPlaybackWorker::TPlaybackWorker(TMasterAudioDataRaw *RawAudio,int SamplesPerSecond,QObject *parent) : QObject(parent)
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
    mDataInSize=SAMPLE_SIZE*PLAYBACK_NUMBER_OF_SAMPLES;
    mDataIn= new char[mDataInSize];
}

TPlaybackWorker::~TPlaybackWorker()
{
    delete [] mDataIn;
    // Clean up if necessary
    qInfo() << "PlaybackWorker Destructor";
}


void TPlaybackWorker::StartPlayback(const QString &FileName)
{
    int BytesIn;
    double CurrentTime;
    qint64 Start,Delta,SleepTime;

    if (!mTimerStarted)
    {
        mTimerStarted=true;
        mTimer.start();
    }

    QFile *file = new QFile(FileName);
    TWaveHeader header;
    if (!file->exists()) {
        delete file;
        emit PlaybackDoneReadingFile();
        emit finished();
        return;
    }

    if (!file->open(QIODevice::ReadOnly))
    {
        delete file;
        emit PlaybackDoneReadingFile();
        emit finished();
        return;
    }

    QDataStream in(file);
    in.setByteOrder(QDataStream::LittleEndian); // WAV is Little Endian

    file->read(header.riffId, 4);
    in >> header.fileSize;
    file->read(header.waveId, 4);
    file->read(header.fmtId, 4);
    in >> header.fmtSize;
    in >> header.audioFormat;
    in >> header.numChannels;
    in >> header.sampleRate;
    in >> header.byteRate;
    in >> header.blockAlign;
    in >> header.bitsPerSample;

    // Skip any extra fmt bytes if fmtSize > 16
    if (header.fmtSize > 16) file->seek(file->pos() + (header.fmtSize - 16));

    // Look for "data" chunk (it might not be immediately after fmt)
    char chunkId[4];
    while (!file->atEnd()) {
        file->read(chunkId, 4);
        uint32_t chunkSize;
        in >> chunkSize;
        if (qstrncmp(chunkId, "data", 4) == 0) {
            header.dataSize = chunkSize;
            break;
        }
        file->seek(file->pos() + chunkSize);
    }
    if (qstrncmp(header.riffId, "RIFF", 4) != 0 || (header.sampleRate!=mSamplesPerSecond) ||
        (header.numChannels!=1)|| (header.bitsPerSample != 32)||
        (header.audioFormat != 3))
    {
     emit PlaybackDoneReadingFile();
        file->close();
        delete file;
        emit PlaybackDoneReadingFile();
        emit finished();
        return;
    }
    int numSamples = header.dataSize / sizeof(float);

    while (!in.atEnd() && (numSamples>0))
    {
        Start=mTimer.elapsed();

        BytesIn=in.readRawData(mDataIn, mDataInSize);
        if (BytesIn<0)
        {
           qInfo() << "Read Error ="<<BytesIn;
           break;
        }
        else if ((BytesIn%4)!=0)
        {
          qInfo() << "Read Error not Modulus of 4";
          break;
        }
        else if (BytesIn==0)
        {
          qInfo() << "Read Error 0";
          break;
        }
        else if (QThread::currentThread()->isInterruptionRequested())
        {
            break; // Exit loop early
        }
        unsigned int NumberOfSamples=BytesIn/SAMPLE_SIZE;

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
        mRawAudio->Mutex.unlock();
        emit PlaybackDataReady(); // Emit data to the main thread

        ++mFrameCount;
        mSampleCount+=NumberOfSamples;
        numSamples-=NumberOfSamples;
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
        SleepTime=PLAYBACK_SAMPLE_PERIOD_MSEC-Delta;
        if (SleepTime<0) SleepTime=0;
        QThread::msleep(SleepTime);
    }
    qInfo()<<"Before Close";
    file->close();
    delete file;
    emit PlaybackDoneReadingFile();
    emit finished();
    qInfo()<<"After Finish";
}

