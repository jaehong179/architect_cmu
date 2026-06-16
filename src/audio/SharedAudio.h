#ifndef SHAREDAUDIO_H
#define SHAREDAUDIO_H
#include <QMutex>

#define CHANNELS      1
#define SAMPLE_FORMAT QAudioFormat::Float
#define SAMPLE_SIZE   sizeof(float)
#define SECONDS_OF_BUFFER 30

// ── [PERF 계측 · §E/§G-2 · QA-AC-01/02/03] Sim 정답값(ground-truth) 이벤트 링 ──
//  Sim 합성기는 각 비트의 '진짜' A(onset)/C 위치를 알고 있다. SimWorker가 이를
//  아래 링버퍼에 적재하고, 메인 스레드가 '검출된' 이벤트와 대조해 검출/타이밍
//  오차(E·G)를 산출한다. Sim 모드 전용(다른 입력 모드에서는 미사용).
#define GT_EVENT_RING 256
typedef struct
{
    uint64_t a_sample;   // 정답 A(onset) 절대 샘플 인덱스 (0 = 빈 슬롯)
    uint64_t c_sample;   // 정답 C 절대 샘플 인덱스 (= a_sample + a_to_c_time × fs)
} TGtBeat;

typedef struct
{
    QMutex         Mutex;
    float          *Samples;
    int            NumberOfAudioSamples;
    unsigned int   WriteIndex;
    uint64_t       TotalSamplesWritten;
    uint64_t       MainThrd_LastTotalSamplesWritten;
    unsigned int   MainThrd_LastWriteIndex;
    double         FPS;
    double         SPF;
    double         SPS;
    // ── [PERF 계측] docs/PERF_VERIFICATION_GUIDE.md 측정용 (제품 기능 아님) ──
    //  아래 두 필드는 캡처 스레드(워커)가 Mutex 보호 하에 기록하고,
    //  메인 스레드가 같은 Mutex 안에서 읽어 지연/드롭을 산출한다.
    double         LastBlockCaptureMs;   // [A-1/A-2 · QA-LT-01] 최신 오디오 블록을 링버퍼에 쓴 시각(Perf::nowMs). 캡처→처리→표시 지연 기준점.
    uint64_t       DroppedSampleEstimate;// [B-1 · QA-RT-02] 기대 대비 부족 샘플 누적 추정치(드롭 추정). Live 캡처에서만 의미.
    // [E/G-2 · QA-AC-01/02/03] Sim 정답 이벤트 링 (SimWorker가 기록, 메인이 대조). Sim 전용.
    TGtBeat        GtBeats[GT_EVENT_RING];
    unsigned int   GtHead;               // 다음 쓰기 위치(ring)
    uint64_t       GtTotal;              // 누적 정답 비트 수 (G-2 검출률 분모)
} TMasterAudioDataRaw;

#endif // SHAREDAUDIO_H
