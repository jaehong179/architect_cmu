#ifndef WAVELODHISTORY_H
#define WAVELODHISTORY_H
// =============================================================================
//  WaveLodHistory — 8분 엔벨로프 이력 저장소 (A안: 풀해상도) + LOD 피라미드
// -----------------------------------------------------------------------------
//  [목적]  pause/스크롤백 기능에서 "최근 ~8분의 엔벨로프"를 보관하고, 줌/팬에 맞춰
//          화면 폭 수준(~수천 점)으로 빠르게 잘라 주기 위한 비시각(non-visual) 저장소.
//
//  [설계 = A안 + LOD]
//   ① 풀해상도 링 (Level 0):
//        - 최근 historySeconds(기본 480s = 8분) 엔벨로프 샘플을 절대 인덱스로 보관.
//        - 48kHz·480s = 23.04M float ≈ 92MB. (메모리는 허용 전제 — A안.)
//        - 줌인 시 원본 그대로 → 어느 구간이든 샘플 단위 정확.
//   ② LOD 피라미드 (Level 1..n): "미리 만든 거친 단계".
//        - 들어올 때 한 패스로 점진 구축(검출 비용에 얹힘). 각 단계는 bucketSize
//          샘플마다 (min,max) 2점만. 단계마다 bucketSize ×LOD_FACTOR.
//        - 줌아웃 시 원본 23M 재훑기 대신 거친 단계만 읽음 → 렌더 비용 일정.
//
//  [질의]  queryWindow(absFrom, absTo, maxPoints, outX, outY):
//        - 보이는 구간 [absFrom,absTo) 를 maxPoints(≈화면폭) 수준으로 잘라
//          (x=절대 샘플 인덱스, y=엔벨로프 값) 점열을 채운다. 호출 측이 x→시간 환산.
//        - 줌 정도에 맞는 해상도 자동 선택: 좁으면 raw 1:1, 중간은 raw 즉석 버킷,
//          넓으면 LOD 단계 읽기. → 어느 줌이든 점 수 ~maxPoints 로 바운드.
//
//  ※ 비시각 단일 인스턴스(중앙 공유)로 두는 것을 의도 — 탭마다 8분을 따로 들지
//    않게(중복 메모리 폭발 방지). onWave 의 env 를 push() 로 흘려넣어 누적한다.
//    (라이브용 탭별 WaveBuffer 0.5~2초는 그대로 두고, 이 저장소는 이력 전용.)
// =============================================================================

#include <QVector>
#include <cstdint>
#include "WaveSink.h"
#include "MeasurementModel.h"   // WaveBlock (onWave 인자)
#include "WaveBuffer.h"         // [③ replay] 스코프 탭 버퍼 채우기 헬퍼용

class WaveLodHistory : public WaveSink
{
public:
    // [WaveSink] TabManager 방송 수신부 — 첫 수신 시 샘플레이트로 지연 구성하고,
    //  레이트가 바뀌면(모드 전환 등) 재구성한다. 이후 엔벨로프를 누적(push)한다.
    void onWave(const WaveBlock &w) override
    {
        if (w.sampleRateHz > 0 && w.sampleRateHz != mSampleRate)
            configure(w.sampleRateHz, mHistorySeconds);
        // 절대 인덱스가 크게 후퇴 = 세션 리셋/모드전환(카운터 0 복귀) → 이력 비우고 새로 시작.
        //  (안 비우면 옛 큰 인덱스 + 새 작은 인덱스가 섞여 이벤트 순서·좌표가 꼬임 → 음수 간격 등.)
        if (mHave && w.n > 0 && w.startSample + 64 < mEnd)
            clear();
        if (w.env && w.n > 0)
            push(w.env, w.n, w.startSample);
        // [A안] raw PCM 도 8분 보관(LOD 없이 풀해상도 링) — seek replay 시 WaveBlock 완전 복원용.
        //  raw 는 작은 윈도우(비트 단위)만 조회하므로 데시메이션 불필요.
        if (w.raw && w.rawN > 0 && mRawPcm.size() == (int)mCap)
            for (int i = 0; i < w.rawN; ++i)
                mRawPcm[(int)((w.rawStart + (uint64_t)i) % mCap)] = w.raw[i];
        // 블록 스칼라 보관(복원 시 사용).
        if (w.bph > 0) mLastBph = w.bph;
        mLastSynced = w.synced;
        mLastOnset  = w.onsetThreshold;
        // [③] A/C 이벤트도 보관 — 스크롤/seek 시 마커·ms 라벨 복원용(엔벨로프와 동일 절대좌표).
        for (int i = 0; i < w.numEvents; ++i) mEvents.push_back(w.events[i]);
        const uint64_t oldest = oldestAbs();
        while (!mEvents.isEmpty() && mEvents.first().markSample + 1 <= oldest) mEvents.removeFirst();
    }

    // [A안/replay] 임의 구간 [from, from+count) 를 WaveBlock 으로 복원(env+raw+events+스칼라).
    //  반환 WaveBlock 의 포인터는 인자 rb 의 벡터를 가리키므로, 호출자가 rb 를 살아있게 유지해야 한다.
    struct ReconBlock {
        QVector<float>     env, raw;
        QVector<WaveEvent> events;
        WaveBlock          block;
    };
    void reconstruct(uint64_t from, int count, ReconBlock &rb) const
    {
        if (count < 0) count = 0;
        rb.env.resize(count);
        rb.raw.resize(count);
        for (int i = 0; i < count; ++i) {
            float v = 0.0f; rawAt(from + (uint64_t)i, v);     rb.env[i] = v;   // 엔벨로프(Level0)
            float r = 0.0f; rawPcmAt(from + (uint64_t)i, r);  rb.raw[i] = r;   // raw PCM
        }
        rb.events = eventsInRange(from, from + (uint64_t)count);
        WaveBlock &b = rb.block;
        b.env = rb.env.constData(); b.n = count; b.startSample = from;
        b.raw = rb.raw.constData(); b.rawN = count; b.rawStart = from;
        b.events = rb.events.constData(); b.numEvents = rb.events.size();
        b.sampleRateHz = mSampleRate; b.bph = mLastBph; b.synced = mLastSynced; b.onsetThreshold = mLastOnset;
    }

    // [③ replay] 시점(absSample) 주변 win 샘플을 복원해 스코프 탭의 버퍼(엔벨로프 + 선택적 raw)에 채운다.
    //  탭은 이후 자기 render() 만 호출하면 그 시점 파형이 그려진다(누적부는 손대지 않음).
    static void replayInto(const WaveLodHistory &hist, WaveBuffer &envBuf, WaveBuffer *rawBuf,
                           double absSample, int win)
    {
        if (win <= 0 || !hist.hasData()) return;
        const uint64_t half = (uint64_t)(win / 2);
        const uint64_t from = ((uint64_t)absSample > half) ? (uint64_t)absSample - half : 0;
        ReconBlock rb;
        hist.reconstruct(from, win, rb);
        envBuf.push(rb.block);                                  // 엔벨로프 + 이벤트
        if (rawBuf && rb.block.raw && rb.block.rawN > 0) {      // 원신호 → rawBuf
            WaveBlock raw;
            raw.env = rb.block.raw; raw.n = rb.block.rawN; raw.startSample = rb.block.rawStart;
            raw.sampleRateHz = rb.block.sampleRateHz; raw.bph = rb.block.bph; raw.synced = rb.block.synced;
            rawBuf->push(raw);
        }
    }

    // [from,to) 절대 인덱스 구간의 A/C 이벤트(markSample 기준).
    QVector<WaveEvent> eventsInRange(uint64_t from, uint64_t to) const
    {
        QVector<WaveEvent> r;
        for (const WaveEvent &e : mEvents)
            if (e.markSample >= from && e.markSample < to) r.push_back(e);
        return r;
    }

    // bucketSize 가 단계마다 곱해지는 비율. 8 → 단계: 8,64,512,4096,32768,262144...
    //  (작을수록 줌 단계가 촘촘해 점 수가 화면폭에 더 근접하지만 LOD 메모리↑.
    //   8 ≈ LOD 합 ~26MB @48k/8분. 메모리 허용 전제(A안)라 해상도 우선.)
    static constexpr int LOD_FACTOR = 8;
    // raw 를 즉석에서 훑어 버킷할 상한(샘플). 이보다 넓은 구간만 LOD 사용.
    //  이 값 이하면 매 질의마다 raw 를 직접 훑어도 가벼움(<~1ms) → 항상 선명.
    static constexpr uint64_t RAW_SCAN_LIMIT = 262144;   // ≈ 5.5초 @48k

    void configure(int sampleRateHz, double historySeconds = 480.0)
    {
        mSampleRate = sampleRateHz > 0 ? sampleRateHz : 48000;
        mHistorySeconds = historySeconds > 0 ? historySeconds : 480.0;
        uint64_t cap = (uint64_t)((double)mSampleRate * mHistorySeconds);
        if (cap < 2) cap = 2;
        mCap = cap;
        mRaw.resize((int)mCap);
        mRaw.fill(0.0f);
        mRawPcm.resize((int)mCap);     // [A안] raw PCM 풀해상도 링
        mRawPcm.fill(0.0f);
        mEnd = 0; mHave = false;

        // LOD 단계 구축: bucketSize 가 LOD_FACTOR 배씩 커지며, 버킷 수가 너무
        //  적어질 때까지(최소 ~64) 단계를 만든다.
        mLevels.clear();
        uint64_t bsize = LOD_FACTOR;
        while (true) {
            uint64_t nb = (mCap + bsize - 1) / bsize;          // 이 단계의 버킷 수
            if (nb < 64) break;
            Level L;
            L.bucketSize = bsize;
            L.capBuckets = (int)nb;
            L.mn.resize(L.capBuckets); L.mn.fill(0.0f);
            L.mx.resize(L.capBuckets); L.mx.fill(0.0f);
            L.lastBucket = 0; L.started = false;
            mLevels.push_back(L);
            if (bsize > mCap) break;
            bsize *= LOD_FACTOR;
        }
    }

    void clear()
    {
        mRaw.fill(0.0f);
        mRawPcm.fill(0.0f);
        mEnd = 0; mHave = false;
        for (Level &L : mLevels) { L.started = false; L.mn.fill(0.0f); L.mx.fill(0.0f); }
        mEvents.clear();
    }

    int      sampleRate() const { return mSampleRate; }
    bool     hasData()    const { return mHave; }
    uint64_t latestAbs()  const { return mEnd; }                              // 마지막+1 절대 인덱스
    uint64_t oldestAbs()  const { return mEnd > mCap ? mEnd - mCap : 0; }     // 보관 중 가장 오래된 인덱스

    // onWave 의 엔벨로프 한 슬라이스를 누적. startSample = env[0] 의 절대 입력 인덱스.
    //  연속 호출은 절대 인덱스가 이어진다는 가정(엔벨로프는 연속). 미세 gap 은 무시.
    void push(const float *env, int n, uint64_t startSample)
    {
        if (!env || n <= 0 || mCap < 2) return;
        for (int i = 0; i < n; ++i)
            pushSample(startSample + (uint64_t)i, env[i]);
        mEnd = startSample + (uint64_t)n;
        mHave = true;
    }

    // 보이는 구간 [absFrom,absTo) 를 ~maxPoints 로 잘라 (x=절대 인덱스, y=값) 채움.
    //  out* 은 항상 x 증가 순. 한 버킷은 (min,max) 2점으로 표현(피크 보존).
    void queryWindow(uint64_t absFrom, uint64_t absTo, int maxPoints,
                     QVector<double> &outX, QVector<double> &outY) const
    {
        outX.clear(); outY.clear();
        if (!mHave || maxPoints < 2) return;
        const uint64_t oldest = oldestAbs(), latest = mEnd;
        if (absFrom < oldest) absFrom = oldest;
        if (absTo   > latest) absTo   = latest;
        if (absTo <= absFrom) return;

        const uint64_t win = absTo - absFrom;

        // ── 1) 아주 좁음: raw 1:1 (샘플마다 1점) ──────────────────────────────
        if (win <= (uint64_t)maxPoints) {
            outX.reserve((int)win); outY.reserve((int)win);
            for (uint64_t a = absFrom; a < absTo; ++a) {
                float v; if (rawAt(a, v)) { outX.push_back((double)a); outY.push_back((double)v); }
            }
            return;
        }

        // ── 2) 중간: raw 즉석 버킷(min/max) → 선명, 비용 가벼움 ────────────────
        if (win <= RAW_SCAN_LIMIT) {
            bucketRaw(absFrom, absTo, maxPoints, outX, outY);
            return;
        }

        // ── 3) 넓음: LOD 단계 읽기(재훑기 없이) ───────────────────────────────
        //  win/bucketSize 가 2*maxPoints 이하가 되는 가장 고운 단계 선택.
        const Level *chosen = nullptr;
        for (const Level &L : mLevels) {
            if ((double)win / (double)L.bucketSize <= 2.0 * maxPoints) { chosen = &L; break; }
        }
        if (!chosen) chosen = &mLevels.last();   // 제일 거친 단계로도 넘치면 그걸로
        readLevel(*chosen, absFrom, absTo, outX, outY);
    }

private:
    struct Level {
        uint64_t       bucketSize;   // 이 단계 한 버킷이 덮는 raw 샘플 수
        int            capBuckets;   // 링 슬롯 수
        QVector<float> mn, mx;       // 버킷별 최소/최대 (절대 버킷번호 % capBuckets)
        uint64_t       lastBucket;   // 현재 누적 중 버킷 번호
        bool           started;
    };

    // 절대 인덱스 a 의 엔벨로프(Level0) 값(보관 범위 밖이면 false).
    inline bool rawAt(uint64_t a, float &out) const
    {
        if (!mHave || a >= mEnd || a + mCap < mEnd) return false;
        out = mRaw[(int)(a % mCap)];
        return true;
    }

    // 절대 인덱스 a 의 raw PCM 값(보관 범위 밖이면 false).
    inline bool rawPcmAt(uint64_t a, float &out) const
    {
        if (!mHave || a >= mEnd || a + mCap < mEnd || mRawPcm.size() != (int)mCap) return false;
        out = mRawPcm[(int)(a % mCap)];
        return true;
    }

    // 샘플 1개를 raw 링 + 모든 LOD 단계에 반영.
    inline void pushSample(uint64_t a, float v)
    {
        mRaw[(int)(a % mCap)] = v;
        for (Level &L : mLevels) {
            const uint64_t b = a / L.bucketSize;
            const int slot = (int)(b % (uint64_t)L.capBuckets);
            if (!L.started || b != L.lastBucket) {     // 새 버킷 → 그 슬롯을 v 로 리셋
                L.mn[slot] = v; L.mx[slot] = v;
                L.lastBucket = b; L.started = true;
            } else {                                   // 같은 버킷 → min/max 갱신
                if (v < L.mn[slot]) L.mn[slot] = v;
                if (v > L.mx[slot]) L.mx[slot] = v;
            }
        }
    }

    // raw 를 [from,to) 에서 ~maxPoints 버킷으로 즉석 min/max.
    void bucketRaw(uint64_t from, uint64_t to, int maxPoints,
                   QVector<double> &outX, QVector<double> &outY) const
    {
        const uint64_t win = to - from;
        const uint64_t bsize = (win + (uint64_t)maxPoints - 1) / (uint64_t)maxPoints;  // ceil
        outX.reserve(2 * maxPoints + 4); outY.reserve(2 * maxPoints + 4);
        for (uint64_t bs = from; bs < to; bs += bsize) {
            const uint64_t be = qMin(bs + bsize, to);
            float mn = 0, mx = 0; bool any = false;
            for (uint64_t a = bs; a < be; ++a) {
                float v; if (!rawAt(a, v)) continue;
                if (!any) { mn = mx = v; any = true; }
                else { if (v < mn) mn = v; if (v > mx) mx = v; }
            }
            if (!any) continue;
            emitMinMax((double)bs + (double)(be - bs) * 0.5, mn, mx, outX, outY);
        }
    }

    // 한 LOD 단계의 [from,to) 버킷들을 읽어 점열로.
    void readLevel(const Level &L, uint64_t from, uint64_t to,
                   QVector<double> &outX, QVector<double> &outY) const
    {
        const uint64_t bFrom = from / L.bucketSize;
        const uint64_t bTo   = (to + L.bucketSize - 1) / L.bucketSize;   // ceil
        const uint64_t oldestBucket = (mEnd > mCap ? (mEnd - mCap) : 0) / L.bucketSize;
        const uint64_t newestBucket = mEnd / L.bucketSize;
        outX.reserve((int)(bTo - bFrom) * 2 + 4); outY.reserve((int)(bTo - bFrom) * 2 + 4);
        for (uint64_t b = bFrom; b < bTo; ++b) {
            if (b < oldestBucket || b > newestBucket) continue;   // 보관 범위 밖(덮어써짐)
            const int slot = (int)(b % (uint64_t)L.capBuckets);
            const double cx = (double)(b * L.bucketSize) + (double)L.bucketSize * 0.5;
            emitMinMax(cx, L.mn[slot], L.mx[slot], outX, outY);
        }
    }

    // 한 버킷을 (min,max) 2점으로 — 같은 x 에 min→max(세로 폭 = 그 구간 진동 범위).
    static inline void emitMinMax(double x, float mn, float mx,
                                  QVector<double> &outX, QVector<double> &outY)
    {
        outX.push_back(x); outY.push_back((double)mn);
        outX.push_back(x); outY.push_back((double)mx);
    }

    QVector<float> mRaw;            // Level 0: 풀해상도 링
    uint64_t       mCap   = 0;      // 링 용량(샘플)
    uint64_t       mEnd   = 0;      // 마지막으로 쓴 절대 인덱스 + 1
    bool           mHave  = false;
    int            mSampleRate = 0;
    double         mHistorySeconds = 480.0;   // 보관 길이(초) — 재구성 시 유지
    QVector<Level> mLevels;         // Level 1..n (거친 단계들)
    QVector<WaveEvent> mEvents;     // [③] A/C 이벤트(마커 복원용, 8분 보관 — 희소)
    QVector<float> mRawPcm;         // [A안] raw PCM Level0 링(LOD 없음 — seek 윈도우만 조회)
    int            mLastBph = 0;    // 복원용 블록 스칼라(최근값)
    bool           mLastSynced = false;
    float          mLastOnset = 0.0f;
};

#endif // WAVELODHISTORY_H
