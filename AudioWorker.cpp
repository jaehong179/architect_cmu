// AudioWorker.cpp
#include "AudioWorker.h"
#include <QThread>
#include "PerfInstrumentation.h"   // [PERF 계측] 캡처 지연/드롭/처리량 측정 (docs/PERF_VERIFICATION_GUIDE.md)


TAudioWorker::TAudioWorker(TMasterAudioDataRaw *RawAudio,QObject *parent) : QObject(parent)
{
    mRawAudio=RawAudio;
    mRawAudio->TotalSamplesWritten=0;
    mRawAudio->WriteIndex=0;
    mRawAudio->MainThrd_LastTotalSamplesWritten=0;
    mRawAudio->MainThrd_LastWriteIndex=0;
    mRawAudio->FPS=0.0;
    mRawAudio->SPF=0.0;
    mRawAudio->SPS=0.0;
    // [PERF 계측 초기화 · §A-1/§B-1] 지연/드롭 측정 필드 초기화
    mRawAudio->LastBlockCaptureMs=0.0;
    mRawAudio->DroppedSampleEstimate=0;
    TimerStarted=false;
    LastTime=0.0;
    FrameCount=0;
    SampleCount=0;
}

TAudioWorker::~TAudioWorker()
{
    // Clean up if necessary
    //qInfo() << "AudioWorker Destructor";
}

void TAudioWorker::stateChangeAudioInput(QAudio::State s)
{
    qDebug() << "Input Audio State change: " << s;
    // [PERF 계측 · §B-1 · QA-RT-02] 캡처 장치 상태 전이 기록(예: 예기치 않은 Idle = 캡처 중단 신호)
    Perf::log("B-1","QA-RT-02","audio_state", (double)(int)s, "state","");
}

void TAudioWorker::StartAudioRecording(QAudioDevice InputDevice,int SampleRate,float Volume)
{
    QAudioFormat InputFormat;
    InputFormat.setSampleRate(SampleRate);
    InputFormat.setChannelCount(CHANNELS);
    InputFormat.setSampleFormat(SAMPLE_FORMAT);

    // [PERF 계측 · §B-1 · QA-RT-02] 드롭 추정에 쓸 캡처 샘플레이트 저장
    mSampleRate = SampleRate;
    mLastDropLogGap = 0.0;

    if (mAudioInput) delete mAudioInput;
    mAudioInput = nullptr;

    if (InputDevice.isNull()) {
        qWarning() << "No default audio input device found.";
        emit finished();
        return;
    }

    mAudioInput = new QAudioSource(InputDevice,InputFormat,this);
    mAudioInput->setVolume(Volume);
    connect(mAudioInput, &QAudioSource::stateChanged,this, &TAudioWorker::stateChangeAudioInput);
    mAudioInputDevice = mAudioInput->start(); // Start recording
    connect( mAudioInputDevice, &QIODevice::readyRead, this, &TAudioWorker::ProcessAudioInput);
    qDebug() << "Audio recording started in worker thread.";
}

void TAudioWorker::SetAudioInputVolume(float Volume)
{
 if(mAudioInput) mAudioInput->setVolume(Volume);
}

void TAudioWorker::ProcessAudioInput()
{
    if (!TimerStarted)
    {
        TimerStarted=true;
        Timer.start();
    }
    double CurrentTime;

    // ── [PERF 계측 · §B-1 · QA-RT-02] 장치 직접 보고 캡처 오류(xrun/overrun) ──
    //  Qt가 ALSA 핸들을 소유하므로 snd_pcm_status를 직접 못 읽는 대신, QAudioSource::error()로
    //  장치 레벨 under/overrun을 받는다(Pi의 ALSA xrun도 Qt가 이 값으로 surface). 변화 시에만 기록.
    //  → 추정치 capture_gap(시간-부족)과 상호보완: 이쪽은 '장치가 실제로 오류를 보고했나'.
    if (mAudioInput) {
        int err = (int)mAudioInput->error();   // 0=NoError,1=Open,2=IO,3=Underrun,4=Fatal
        if (err != 0 && err != mLastAudioErr)
            Perf::log("B-1","QA-RT-02","audio_xrun", (double)err, "errcode",
                      QString("state=%1").arg((int)mAudioInput->state()));
        mLastAudioErr = err;
    }

    QByteArray ba =  mAudioInputDevice->readAll();

    unsigned int NumberOfSamples = ba.length() / SAMPLE_SIZE;
    float *AudioSamples=(float *)ba.constData();
    mRawAudio->Mutex.lock();
    unsigned int TempWriteIndex = mRawAudio->WriteIndex;
    mRawAudio->Mutex.unlock();
    int SamplesLeft=std::min(NumberOfSamples,mRawAudio->NumberOfAudioSamples-TempWriteIndex);
    memcpy(&mRawAudio->Samples[TempWriteIndex], AudioSamples, SamplesLeft * SAMPLE_SIZE);
    //qInfo() << "Bytes in "<< ba.length();
    if(SamplesLeft < NumberOfSamples)
    {
        memcpy(mRawAudio->Samples, &AudioSamples[SamplesLeft], (NumberOfSamples - SamplesLeft) * SAMPLE_SIZE);
        qInfo() << "MasterAudioData Samples Rollover";
    }
    mRawAudio->Mutex.lock();
    mRawAudio->WriteIndex = (TempWriteIndex+ NumberOfSamples) % mRawAudio->NumberOfAudioSamples;
    mRawAudio->TotalSamplesWritten+=NumberOfSamples;
    // [PERF 계측 · §A-1/§A-2 · QA-LT-01] 종단간 지연 측정의 시작점:
    //   이 오디오 블록이 캡처되어 링버퍼에 막 기록된 시각을 남긴다.
    //   메인 스레드가 동일 Mutex 안에서 읽어 (표시시각 - 이 값) = 종단간 지연을 산출.
    mRawAudio->LastBlockCaptureMs = Perf::nowMs();
    mRawAudio->Mutex.unlock();
    ++FrameCount;
    SampleCount+=NumberOfSamples;
    CurrentTime = Timer.elapsed()/1000.0;
    if (CurrentTime-LastTime > 2) // average fps over 2 seconds
    {
        double fdelta;
        fdelta=CurrentTime-LastTime;
        mRawAudio->FPS=FrameCount/fdelta;
        mRawAudio->SPS=SampleCount/fdelta;
        mRawAudio->SPF=SampleCount/FrameCount;
        LastTime=CurrentTime;
        FrameCount=0;
        SampleCount=0;

        // ── [PERF 계측 · §B-3 · QA-RT-02/RT-01] 실효 처리량 ──────────────────
        //  설정 샘플레이트 대비 실제 처리 SPS 가 유지되는지(=underrun/드롭 조기 발견).
        Perf::log("B-3","QA-RT-02","bg_sps", mRawAudio->SPS, "samp/s",
                  QString("set_sps=%1").arg(mSampleRate));
        Perf::log("B-3","QA-RT-02","bg_fps", mRawAudio->FPS, "frame/s","");
        Perf::log("B-3","QA-RT-02","bg_spf", mRawAudio->SPF, "samp/frame","");

        // ── [PERF 계측 · §B-1/§B-2 · QA-RT-02] 캡처 드롭 추정 ────────────────
        //  Qt(QAudioSource)는 드롭 프레임 직접 보고 API가 없으므로,
        //  '기대 누적 샘플(경과시간×샘플레이트) - 실제 누적' = capture_gap 으로 추정한다.
        //  gap 자체는 초기 버퍼링 지연을 포함하지만, gap 이 '지속 증가'하면 실제 드롭 신호.
        if (mSampleRate > 0) {
            double expected = CurrentTime * (double)mSampleRate;     // 기대 누적 샘플
            double gap      = expected - (double)mRawAudio->TotalSamplesWritten;
            if (gap < 0) gap = 0;
            double growth   = gap - mLastDropLogGap;                 // 이번 구간 증가분(>0 → 드롭 의심)
            mLastDropLogGap = gap;
            mRawAudio->DroppedSampleEstimate = (uint64_t)gap;
            Perf::log("B-1","QA-RT-02","capture_gap_samples", gap, "samp",
                      QString("set_sps=%1").arg(mSampleRate));
            Perf::log("B-1","QA-RT-02","capture_gap_growth", growth, "samp/2s","");
        }
    }
    //qDebug() << "worker thread: handleResults slot is running in thread" << QThread::currentThreadId()<<" "<<count;
    emit AudioDataReady(); // Emit data to the main thread

}

void TAudioWorker::StopAudioRecording()
{
    if (mAudioInput) {
        mAudioInput->stop();
        delete mAudioInput;
        mAudioInput = nullptr;
        qDebug() << "Audio recording stopped.";
    }
    emit finished();
}
