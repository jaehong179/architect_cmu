// PlaybackWorker.cpp
#include <QtGlobal>
#include <QFile>
#include <QThread>
#include <QDebug>
#include "PlaybackWorker.h"
#include "WaveHeader.h"
#include "AudioRingBuffer.h"        // 공용 링버퍼 쓰기(3개 워커 DRY)

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

        // [전체 정지] 정지 중에는 파일을 읽지 않고 대기 → 재생 위치 보존(resume 시 그대로 이어짐).
        while (mRawAudio->Paused.load(std::memory_order_relaxed) &&
               !QThread::currentThread()->isInterruptionRequested())
            QThread::msleep(20);

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

        // 공용 링버퍼 쓰기(부가 작업 없음 — 재생은 캡처 시각/정답 이벤트가 불필요).
        //  mDataIn 은 원시 바이트 버퍼(char*)지만 내용은 f32 PCM 이므로 float* 로 해석한다.
        //  (이로써 롤오버 시 잔여 샘플 오프셋이 capture/sim 워커와 동일하게 '샘플' 단위로 맞춰진다.)
        writeSamplesToRing(mRawAudio, reinterpret_cast<const float *>(mDataIn), NumberOfSamples, []{});
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

