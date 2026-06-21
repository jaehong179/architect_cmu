#include "CaptureController.h"
#include "AudioWorker.h"
#include "PlaybackWorker.h"
#include "SimWorker.h"
#include "MeasurementEngine.h"
#include "WavStreamWriter.h"
#include "TabManager.h"
#include "MeasurementModel.h"
#include "PerfInstrumentation.h"
#include <QThread>
#include <QVarLengthArray>
#include <QString>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

// 검출기 1회 처리 슬라이스 크기. SAMPLE_SIZE 는 SharedAudio.h.
static constexpr unsigned DETECTOR_NUMBER_OF_SAMPLES = 4096u;

CaptureController::CaptureController(MeasurementEngine *engine, TabManager *tabs, QObject *parent)
    : QObject(parent), mEngine(engine), mTabs(tabs) {}

CaptureController::~CaptureController()
{
    deleteDetectors();
    if (mRawAudio) {
        if (mRawAudio->Samples) { delete[] mRawAudio->Samples; mRawAudio->Samples = nullptr; }
        delete mRawAudio; mRawAudio = nullptr;
    }
}

// ── 공유 링버퍼 (재)할당 ──
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

// ── 검출기(Timegrapher) 생성/파괴 ──
void CaptureController::createDetectors()
{
    deleteDetectors();
    tg_config_default(&mTgCfg);
    mTgCfg.sample_rate = mSampleRate;
    if (mBphAuto) mTgCfg.bph_mode = TG_BPH_MODE_AUTO;
    else { mTgCfg.bph_mode = TG_BPH_MODE_MANUAL; mTgCfg.manual_bph = mManualBph; }
    mTgCfg.suppress_pre_sync_events = true;
    mTgCfg.hpf_cutoff_hz = mHpfCutoffHz;

    mCtx = tg_init(&mTgCfg);
    if (mCtx == nullptr)
        throw std::runtime_error("allocation failed-could not initialize detector");

    mInputBlock = (float *)malloc(DETECTOR_NUMBER_OF_SAMPLES * SAMPLE_SIZE);
    if (!mInputBlock) {
        tg_destroy(mCtx); mCtx = nullptr;
        throw std::runtime_error("allocation failed");
    }
}

void CaptureController::deleteDetectors()
{
    if (mInputBlock) { free(mInputBlock); mInputBlock = nullptr; }
    if (mCtx) { tg_destroy(mCtx); mCtx = nullptr; }
}

// ── 세션 시작 시 파이프라인 상태 비움 ──
void CaptureController::resetPipeline()
{
    mInputAbsSample = 0;
    mForegroundTimerStarted = false;
    createDetectors();
}

// ── 입력 소스 시작 ──
void CaptureController::startLive(const QAudioDevice &device, int sampleRate, float micVol)
{
    mSampleRate = sampleRate;  mLive = true;  mSimMode = false;  mSimActive = false;
    resetPipeline();
    allocBuffer(sampleRate);
    mAudioThread = new QThread();
    mAudioWorker = new TAudioWorker(mRawAudio);
    mAudioWorker->moveToThread(mAudioThread);
    connect(mAudioWorker, &TAudioWorker::finished, mAudioThread, &QThread::quit);
    connect(mAudioThread, &QThread::finished, mAudioWorker, &QObject::deleteLater);
    connect(mAudioThread, &QThread::finished, mAudioThread, &QObject::deleteLater);
    connect(mAudioWorker, &TAudioWorker::AudioDataReady, this, &CaptureController::onAudioData);
    connect(this, &CaptureController::localStartAudio,          mAudioWorker, &TAudioWorker::StartAudioRecording);
    connect(this, &CaptureController::localStopAudio,           mAudioWorker, &TAudioWorker::StopAudioRecording);
    connect(this, &CaptureController::localSetAudioInputVolume, mAudioWorker, &TAudioWorker::SetAudioInputVolume);
    mAudioThread->start(QThread::TimeCriticalPriority);
    emit localStartAudio(device, sampleRate, micVol);
}

void CaptureController::startPlayback(const QString &fileName, int sampleRate)
{
    mSampleRate = sampleRate;  mLive = false;  mSimMode = false;  mSimActive = false;
    resetPipeline();
    allocBuffer(sampleRate);
    mPlaybackThread = new QThread();
    mPlaybackWorker = new TPlaybackWorker(mRawAudio, sampleRate);
    mPlaybackWorker->moveToThread(mPlaybackThread);
    connect(mPlaybackWorker, &TPlaybackWorker::finished, mPlaybackThread, &QThread::quit);
    connect(mPlaybackThread, &QThread::finished, mPlaybackWorker, &QObject::deleteLater);
    connect(mPlaybackThread, &QThread::finished, mPlaybackThread, &QObject::deleteLater);
    connect(this, &CaptureController::localStartPlayback, mPlaybackWorker, &TPlaybackWorker::StartPlayback);
    connect(mPlaybackWorker, &TPlaybackWorker::PlaybackDataReady,       this, &CaptureController::onPlaybackData);
    connect(mPlaybackWorker, &TPlaybackWorker::PlaybackDoneReadingFile, this, &CaptureController::playbackDoneReadingFile);
    mPlaybackThread->start(QThread::TimeCriticalPriority);
    emit localStartPlayback(fileName);
}

void CaptureController::startSim(const WatchSynthStreamConfig &cfg, int sampleRate)
{
    mSampleRate = sampleRate;  mLive = false;  mSimMode = true;
    mLastSimCfg = cfg;  mSimActive = true;     // [G-1] 정답 설정값 보관(측정값과 대비)
    resetPipeline();
    allocBuffer(sampleRate);
    mSimThread = new QThread();
    mSimWorker = new TSimWorker(mRawAudio, sampleRate);
    mSimWorker->moveToThread(mSimThread);
    connect(mSimWorker, &TSimWorker::finished, mSimThread, &QThread::quit);
    connect(mSimThread, &QThread::finished, mSimWorker, &QObject::deleteLater);
    connect(mSimThread, &QThread::finished, mSimThread, &QObject::deleteLater);
    connect(this, &CaptureController::localStartSim, mSimWorker, &TSimWorker::StartSim);
    connect(mSimWorker, &TSimWorker::SimDataReady, this, &CaptureController::onSimData);
    connect(mSimWorker, &TSimWorker::SimDone,      this, &CaptureController::onSimWorkerDone);
    mSimThread->start(QThread::TimeCriticalPriority);
    emit localStartSim(cfg);
}

void CaptureController::stopLive()     { emit localStopAudio(); }
void CaptureController::stopPlayback() { if (mPlaybackThread) mPlaybackThread->requestInterruption(); }
void CaptureController::stopSim()      { if (mSimThread) mSimThread->requestInterruption(); }

// ── 워커 블록 수신 — 메인 스레드 ──
void CaptureController::handleInputData(TMasterAudioDataRaw *p)
{
    p->Mutex.lock();
    mLocalWriteIndex = p->WriteIndex;
    mLocalTotalSamplesWritten = p->TotalSamplesWritten;
    // [PERF · §A-1/A-2] 최신 블록 캡처 시각/드롭 추정 원자 스냅샷.
    mLocalLastBlockCaptureMs = p->LastBlockCaptureMs;
    mLocalDroppedSamples     = p->DroppedSampleEstimate;
#if PERF_ENABLE
    // [PERF · §E/§G-2] Sim 모드에서만 정답(GT) 이벤트 링 스냅샷(검출 정확도 대조용, 계측 전용).
    if (mSimMode) {
        memcpy(mLocalGt, p->GtBeats, sizeof(mLocalGt));
        mLocalGtHead  = p->GtHead;
        mLocalGtTotal = p->GtTotal;
        mLocalGtValid = true;
    } else {
        mLocalGtValid = false;
    }
#endif
    p->Mutex.unlock();

    processSamples(p);

    if ((mBackgroundLastFPS != p->FPS) || (mBackgroundLastSPS != p->SPS) || (mBackgroundLastSPF != p->SPF) ||
        (mForegroundLastFPS != mForegroundFPS) || (mForegroundLastSPS != mForegroundSPS) || (mForegroundLastSPF != mForegroundSPF))
    {
        mBackgroundLastFPS = p->FPS; mBackgroundLastSPS = p->SPS; mBackgroundLastSPF = p->SPF;
        mForegroundLastFPS = mForegroundFPS; mForegroundLastSPS = mForegroundSPS; mForegroundLastSPF = mForegroundSPF;
        emit statusMessage(
            QString("Backgroud Audio Thread Average - FPS:%1, SPS:%2, SPF: %3 Foregroud Audio Handler Average - FPS:%4, SPS:%5, SPF: %6")
                .arg(mBackgroundLastFPS, 0, 'f', 0).arg(mBackgroundLastSPS, 0, 'f', 0).arg(mBackgroundLastSPF, 0, 'f', 0)
                .arg(mForegroundLastFPS, 0, 'f', 0).arg(mForegroundLastSPS, 0, 'f', 0).arg(mForegroundLastSPF, 0, 'f', 0));
    }
}

// ── A/C 이벤트 → 측정 엔진 ──
void CaptureController::aEvent(double t, bool haveValidBph, double bph)
{
    mEngine->onAEvent(t, haveValidBph, bph);
}

void CaptureController::cEvent(double t, bool haveValidBph, double bph)
{
    mEngine->onCEvent(t, haveValidBph, bph);

#if PERF_ENABLE
    // [PERF · §G-1] Sim 모드 측정 정확도(측정값 − 설정값).
    if (mSimActive) {
        MeasurementEngine::Results res = mEngine->results();
        if (res.rateValid)
            PERF_LOG("G-1","QA-CO-01","rate_err_s_per_d", res.rateSecPerDay - mLastSimCfg.rate_error_s_per_day, "s/d",
                      QString("meas=%1;set=%2").arg(res.rateSecPerDay,0,'f',2).arg(mLastSimCfg.rate_error_s_per_day,0,'f',2));
        if (res.beatErrorValid) {
            double measBE = res.beatErrorMs, setBE = qAbs(mLastSimCfg.beat_error_ms);
            PERF_LOG("G-1","QA-CO-01","beaterr_err_ms", measBE - setBE, "ms",
                      QString("meas=%1;set=%2").arg(measBE,0,'f',3).arg(setBE,0,'f',3));
        }
        if (res.amplitudeValid) {
            double measAmp = res.amplitudeDeg;
            PERF_LOG("G-1","QA-CO-01","amp_err_deg", measAmp - mLastSimCfg.watch_amplitude_degrees, "deg",
                      QString("meas=%1;set=%2").arg(measAmp,0,'f',1).arg(mLastSimCfg.watch_amplitude_degrees,0,'f',1));
        }
        PERF_LOG("G-2","QA-AC-01","gt_total", (double)mLocalGtTotal, "beats","");
    }
#endif

    emit measurementReady();   // → MainWindow DisplayResults(readout + 탭 게시)
}

#if PERF_ENABLE
// [PERF · §E-2/§G-2] 검출 이벤트(val, 절대 샘플 위치)를 Sim 정답 이벤트 링과 대조해
//  타이밍/검출 오차를 기록한다. A/C 두 경로가 필드·로그 이름만 다르고 알고리즘이 동일해 공용화.
//   isAEvent=true  → 정답 A(onset, a_sample) / onset_err_ms / a_match·a_unmatched
//   isAEvent=false → 정답 C(c_sample)        / peak_err_ms  / c_match·c_unmatched
//  Sim 모드(정답 유효)에서만 동작. tol = 비트 주기의 절반(가장 가까운 정답에 매칭).
void CaptureController::matchGroundTruth(double val, bool isAEvent)
{
    if (!(mLocalGtValid && mLastSimCfg.bph>0.0)) return;
    const double tol = 0.5 * ((double)mSampleRate * 3600.0 / mLastSimCfg.bph);
    double bestErr=0.0; bool found=false;
    for (unsigned k=0;k<GT_EVENT_RING;k++){
        uint64_t g = isAEvent ? mLocalGt[k].a_sample : mLocalGt[k].c_sample;
        if (g==0) continue;
        double e=val-(double)g;
        if(!found || qAbs(e)<qAbs(bestErr)){bestErr=e;found=true;}
    }
    if (found && qAbs(bestErr)<tol){
        PERF_LOG("E-2","QA-AC-02", isAEvent?"onset_err_ms":"peak_err_ms", bestErr*1000.0/mSampleRate,"ms","");
        PERF_LOG("G-2","QA-AC-01", isAEvent?"a_match":"c_match", 1,"event","");
    } else PERF_LOG("G-2","QA-AC-01", isAEvent?"a_unmatched":"c_unmatched", 1,"event","");
}
#endif

// ── 샘플 처리 파이프라인 — 핫패스(전부 직접 호출) ──
void CaptureController::processSamples(TMasterAudioDataRaw *p)
{
    mEngine->setConfig(mSampleRate, mAveragingPeriod, mLiftAngle);
    // [견고성] total 은 단조 증가가 정상이지만, 소스(버퍼) 재시작/되감김 또는 잔류 시그널로
    //  mLocalTotalSamplesWritten 가 MainThrd_Last 보다 작아지면 uint64 뺄셈이 언더플로하여
    //  int 로 좁혀질 때 음수/쓰레기 슬라이스가 된다(예: Playback 재시작 직후). 되감김을 감지하면
    //  현재 위치로 재동기하고 이번 블록은 건너뛴다(다음 블록부터 정상 처리).
    const uint64_t totalNow = mLocalTotalSamplesWritten;
    const uint64_t lastDone = p->MainThrd_LastTotalSamplesWritten;
    if (totalNow < lastDone) {
        qInfo() << "processSamples: total rewind detected (resync) total=" << totalNow
                << "last=" << lastDone;
        p->MainThrd_LastTotalSamplesWritten = totalNow;
        p->MainThrd_LastWriteIndex          = mLocalWriteIndex;
        return;
    }
    int SamplesToAdd = (int)(totalNow - lastDone);

    int slice;
    if (!mForegroundTimerStarted) {
        mForegroundTimer.restart();
        mForegroundTimerStarted = true;
        mForegroundLastTime = 0.0; mForegroundFrameCount = 0; mForegroundSampleCount = 0;
    }
    if (SamplesToAdd > 0) {
        const double perfProcStartMs = PERF_NOW();
        const bool   perfIsLive      = mLive;
        PERF_LOG("A-2","QA-LT-01","backlog_samples",(double)SamplesToAdd,"samp",
                  perfIsLive ? "mode=Live" : "mode=PlaybackOrSim");
        if (perfIsLive && mLocalLastBlockCaptureMs>0.0)
            PERF_LOG("A-2","QA-LT-01","cap2proc_latency_ms", perfProcStartMs - mLocalLastBlockCaptureMs, "ms",
                      QString("backlog=%1;drop_est=%2").arg(SamplesToAdd).arg(mLocalDroppedSamples));

        while (SamplesToAdd > 0) {
            if (SamplesToAdd > (int)DETECTOR_NUMBER_OF_SAMPLES) slice = DETECTOR_NUMBER_OF_SAMPLES;
            else slice = SamplesToAdd;

            for (int i=0;i<slice;i++) {
                mInputBlock[i] = p->Samples[p->MainThrd_LastWriteIndex];
                p->MainThrd_LastWriteIndex = (p->MainThrd_LastWriteIndex+1) % p->NumberOfAudioSamples;
            }
            if (mWavWriter) mWavWriter->write(mInputBlock, slice);

            tg_result_t r;
            if (tg_process(mCtx, mInputBlock, slice, &r) != 0) { qInfo()<<"tg_process failed"; return; }

            if (r.sync_lost_event)      PERF_LOG("A-4","QA-US-01","fault_sync_lost", 1, "event","");
            if (r.sync_acquired_event)  PERF_LOG("A-4","QA-US-01","sync_acquired",   1, "event","");
            if (r.detector_reset_event) PERF_LOG("A-4","QA-US-01","detector_reset",  1, "event","");

            // [탭] 엔벨로프/마커 렌더링은 TabRateScope.onWave 가 담당.
            for (int i=0;i<r.num_events;i++) {
                double val;
                if (r.events[i].type==TG_EVENT_A) {
                    val = r.events[i].sample_index + r.events[i].sub_sample_offset;
                    aEvent(val,(r.sync_status==TG_SYNC_SYNCED),r.detected_bph);
#if PERF_ENABLE
                    matchGroundTruth(val, /*isAEvent=*/true);   // [PERF · §E-2/§G-2] 검출 A vs 정답 A 대조
#endif
                } else if (r.events[i].type==TG_EVENT_C) {
                    if (mUseConset) {
                        if (r.events[i].onset_valid)
                            val = r.events[i].onset_sample_index + r.events[i].onset_sub_sample_offset;
                        else { qInfo()<<"Invalid C Onset using C peak"; val = r.events[i].sample_index + r.events[i].sub_sample_offset; }
                    } else val = r.events[i].sample_index + r.events[i].sub_sample_offset;

                    cEvent(val,(r.sync_status==TG_SYNC_SYNCED),r.detected_bph);
#if PERF_ENABLE
                    matchGroundTruth(val, /*isAEvent=*/false);  // [PERF · §E-2/§G-2] 검출 C vs 정답 C 대조
#endif
                } else qInfo()<<"Unkown Event Type";
            }

            // [탭] 이 슬라이스의 엔벨로프 + 이벤트를 스코프 계열 탭에 게시.
            if (mTabs) {
                QVarLengthArray<WaveEvent, 32> wevs;
                const bool useConset = mUseConset;
                for (size_t ei = 0; ei < r.num_events; ++ei) {
                    uint64_t mark = r.events[ei].sample_index;
                    if (r.events[ei].type == TG_EVENT_C && useConset && r.events[ei].onset_valid)
                        mark = r.events[ei].onset_sample_index;
                    wevs.append(WaveEvent{ r.events[ei].sample_index, (int)r.events[ei].type, r.events[ei].peak_value, mark });
                }
                WaveBlock wb;
                wb.env = r.processed_pcm; wb.n = (int)r.processed_pcm_len; wb.startSample = r.processed_pcm_start_sample;
                wb.sampleRateHz = mSampleRate; wb.bph = r.detected_bph; wb.synced = (r.sync_status == TG_SYNC_SYNCED);
                wb.events = wevs.data(); wb.numEvents = wevs.size();
                wb.raw = mInputBlock; wb.rawN = slice; wb.rawStart = mInputAbsSample;
                wb.onsetThreshold = r.onset_threshold;
                mTabs->broadcastWave(wb);
            }
            mInputAbsSample += (uint64_t)slice;

            mForegroundSampleCount += slice;
            SamplesToAdd -= slice;
        }

        p->MainThrd_LastTotalSamplesWritten = mLocalTotalSamplesWritten;

        // [PERF · §A-1/A-2] 처리→표시 요청 시각 → 지연 산출 + afterReplot 대기 등록.
        const double perfDispMs = PERF_NOW();
        PERF_LOG("A-2","QA-LT-01","proc2disp_latency_ms", perfDispMs - perfProcStartMs, "ms","");
        if (perfIsLive && mLocalLastBlockCaptureMs>0.0)
            PERF_LOG("A-1","QA-LT-01","e2e_latency_ms", perfDispMs - mLocalLastBlockCaptureMs, "ms",
                      QString("set_sps=%1").arg(mSampleRate));
#if PERF_ENABLE
        // afterReplot(실제 paint 완료) 에서 disp_paint/e2e_full 을 산출하기 위한 대기 등록(계측 전용).
        mPerfReplotRequestMs    = perfDispMs;
        mPerfCaptureForReplotMs = mLocalLastBlockCaptureMs;
        mPerfReplotLive         = perfIsLive;
        mPerfReplotPending      = true;
        mReplotReqCount++;
#endif

        mForegroundFrameCount++;
        double CurrentTime = mForegroundTimer.elapsed()/1000.0;
        if (CurrentTime-mForegroundLastTime > 2) {
            double fdelta = CurrentTime-mForegroundLastTime;
            mForegroundFPS = mForegroundFrameCount/fdelta;
            mForegroundSPS = mForegroundSampleCount/fdelta;
            mForegroundSPF = mForegroundSampleCount/(double)mForegroundFrameCount;
            mForegroundLastTime = CurrentTime; mForegroundFrameCount = 0; mForegroundSampleCount = 0;
            PERF_LOG("B-3","QA-RT-01","fg_sps", mForegroundSPS, "samp/s","");
            PERF_LOG("B-3","QA-RT-01","fg_fps", mForegroundFPS, "frame/s","");
            PERF_LOG("B-3","QA-RT-01","fg_spf", mForegroundSPF, "samp/frame","");
        }
    }
}

// ── 실제 paint 완료(탭 ScopePlot afterReplot) → 표시 지연/프레임율 ──
void CaptureController::onScopeReplotted()
{
#if PERF_ENABLE
    if (!mPerfReplotPending) return;
    mPerfReplotPending = false;
    double now = PERF_NOW();
    PERF_LOG("A-2","QA-LT-01","disp_paint_ms", now - mPerfReplotRequestMs, "ms","");
    if (mPerfReplotLive && mPerfCaptureForReplotMs > 0.0)
        PERF_LOG("A-1","QA-LT-01","e2e_full_ms", now - mPerfCaptureForReplotMs, "ms", "paint_included");
    mPaintCount++;
    if (!mPaintHave) { mPaintLastEmitMs = now; mPaintHave = true; }
    if (now - mPaintLastEmitMs >= 1000.0) {
        double sec = (now - mPaintLastEmitMs) / 1000.0;
        PERF_LOG("F-1","QA-SC-01","paint_fps", (double)mPaintCount / sec, "frame/s",
                  QString("replot_req=%1").arg(mReplotReqCount));
        mPaintCount = 0; mReplotReqCount = 0; mPaintLastEmitMs = now;
    }
#endif
}
