#ifndef AUDIORINGBUFFER_H
#define AUDIORINGBUFFER_H
// 공유 링버퍼에 한 블록(샘플들)을 기록하는 공용 루틴 — capture/playback/sim 워커의 중복 제거(DRY).
//  Mutex 사용·랩어라운드·WriteIndex/TotalSamplesWritten 갱신은 기존 3개 워커 코드와 동일하다.
//  인덱스 갱신과 '원자적으로'(같은 Mutex 안에서) 실행해야 하는 부가 작업
//  (예: AudioWorker 의 캡처 시각, SimWorker 의 정답 이벤트 적재)은 extraUnderLock 콜백으로 처리한다.
#include "SharedAudio.h"
#include <algorithm>
#include <cstring>
#include <QDebug>

template <typename ExtraUnderLock>
inline void writeSamplesToRing(TMasterAudioDataRaw *mRawAudio, const float *src,
                               unsigned int NumberOfSamples, ExtraUnderLock extraUnderLock)
{
    mRawAudio->Mutex.lock();
    unsigned int TempWriteIndex = mRawAudio->WriteIndex;
    mRawAudio->Mutex.unlock();

    int SamplesLeft = std::min(NumberOfSamples, mRawAudio->NumberOfAudioSamples - TempWriteIndex);
    memcpy(&mRawAudio->Samples[TempWriteIndex], src, SamplesLeft * SAMPLE_SIZE);
    if (SamplesLeft < NumberOfSamples) {
        memcpy(mRawAudio->Samples, &src[SamplesLeft], (NumberOfSamples - SamplesLeft) * SAMPLE_SIZE);
        qInfo() << "Audio ring buffer rollover";
    }

    mRawAudio->Mutex.lock();
    mRawAudio->WriteIndex = (TempWriteIndex + NumberOfSamples) % mRawAudio->NumberOfAudioSamples;
    mRawAudio->TotalSamplesWritten += NumberOfSamples;
    extraUnderLock();   // 인덱스/총합 갱신과 같은 Mutex 안에서(원자적으로) 부가 작업
    mRawAudio->Mutex.unlock();
}
#endif // AUDIORINGBUFFER_H
