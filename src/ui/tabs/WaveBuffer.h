#ifndef WAVEBUFFER_H
#define WAVEBUFFER_H
// =============================================================================
//  WaveBuffer — 절대 샘플 인덱스로 주소화되는 엔벨로프 링버퍼 (스코프 탭 공용 인프라)
// -----------------------------------------------------------------------------
//  스코프 계열 탭(Escapement·Beat Scope·Waveform·Sync Sweep·Spectrogram·F0~F3)이
//  공통으로 필요로 하는 것: "최근 N 샘플을 절대 인덱스로 조회"하고 "범위 내 A/C 이벤트를
//  얻는" 기능.  각 탭이 WaveBuffer 하나를 소유하고 onWave() 에서 push 한 뒤, 렌더 시
//  copyRange()/eventsInRange() 로 원하는 비트 구간을 잘라 그린다.
//
//  헤더 온리(인라인). 고정 용량 링버퍼라 push 비용은 O(슬라이스 길이)로 일정.
// =============================================================================

#include <QVector>
#include <cstdint>
#include "MeasurementModel.h"

class WaveBuffer
{
public:
    // capacitySamples = 보관할 최근 샘플 수(예: 0.5초 분량). configure 후 사용.
    void configure(int capacitySamples)
    {
        mCap = capacitySamples > 0 ? capacitySamples : 1;
        mBuf.resize(mCap);
        mBuf.fill(0.0f);
        mEnd = 0;
        mHave = false;
        mEvents.clear();
        mSampleRate = 0; mBph = 0; mSynced = false; mOnsetThresh = 0.0f;
    }

    void clear()
    {
        mBuf.fill(0.0f);
        mEnd = 0; mHave = false;
        mEvents.clear();
    }

    int      sampleRate() const { return mSampleRate; }
    int      bph()        const { return mBph; }
    bool     synced()     const { return mSynced; }
    float    onsetThreshold() const { return mOnsetThresh; }   // 검출기 onset 임계(엔벨로프 레벨)
    bool     hasData()    const { return mHave; }
    uint64_t latestAbs()  const { return mEnd; }                 // 마지막+1 절대 인덱스
    uint64_t oldestAbs()  const { return mEnd > (uint64_t)mCap ? mEnd - mCap : 0; }

    // 한 파형 블록을 적재.
    void push(const WaveBlock &w)
    {
        mSampleRate = w.sampleRateHz; mBph = w.bph; mSynced = w.synced; mOnsetThresh = w.onsetThreshold;
        if (mCap <= 1 && w.sampleRateHz > 0) configure(w.sampleRateHz / 2); // 기본 0.5초
        for (int i = 0; i < w.n; ++i) {
            const uint64_t a = w.startSample + (uint64_t)i;
            mBuf[(int)(a % mCap)] = w.env[i];
        }
        if (w.n > 0) { mEnd = w.startSample + (uint64_t)w.n; mHave = true; }
        for (int i = 0; i < w.numEvents; ++i) mEvents.push_back(w.events[i]);
        // 버퍼 밖으로 밀려난 오래된 이벤트 제거.
        const uint64_t oldest = oldestAbs();
        int keep = 0;
        for (int i = 0; i < mEvents.size(); ++i)
            if (mEvents[i].sample + 1 > oldest) mEvents[keep++] = mEvents[i];
        mEvents.resize(keep);
    }

    // 절대 인덱스 a 의 샘플(없으면 false).
    bool sampleAt(uint64_t a, float &out) const
    {
        if (!mHave || a >= mEnd || a + (uint64_t)mCap < mEnd) return false;
        out = mBuf[(int)(a % mCap)];
        return true;
    }

    // [from, from+count) 구간을 out 에 복사(없는 구간은 0). 항상 count 개 채움.
    void copyRange(uint64_t from, int count, QVector<double> &out) const
    {
        out.resize(count);
        for (int i = 0; i < count; ++i) {
            float v = 0.0f;
            sampleAt(from + (uint64_t)i, v);
            out[i] = v;
        }
    }

    // [from, to) 구간의 이벤트를 모은다.
    QVector<WaveEvent> eventsInRange(uint64_t from, uint64_t to) const
    {
        QVector<WaveEvent> r;
        for (const WaveEvent &e : mEvents)
            if (e.sample >= from && e.sample < to) r.push_back(e);
        return r;
    }

    // 가장 최근 type(1=A,2=C) 이벤트의 절대 인덱스(없으면 false).
    bool latestEvent(int type, uint64_t &outSample) const
    {
        for (int i = mEvents.size() - 1; i >= 0; --i)
            if (mEvents[i].type == type) { outSample = mEvents[i].sample; return true; }
        return false;
    }

    // 한 비트의 샘플 길이(동기 시). 미동기면 0.
    int samplesPerBeat() const
    {
        if (mBph <= 0 || mSampleRate <= 0) return 0;
        return (int)((double)mSampleRate * 3600.0 / (double)mBph + 0.5);
    }

private:
    QVector<float>     mBuf;
    int                mCap = 1;
    uint64_t           mEnd = 0;       // 마지막으로 쓴 절대 인덱스 + 1
    bool               mHave = false;
    QVector<WaveEvent> mEvents;
    int                mSampleRate = 0;
    int                mBph = 0;
    bool               mSynced = false;
    float              mOnsetThresh = 0.0f;
};

#endif // WAVEBUFFER_H
