#ifndef MEASUREMENTMODEL_H
#define MEASUREMENTMODEL_H
// =============================================================================
//  MeasurementModel.h — 탭에 게시(publish)되는 읽기 전용 측정 스냅샷
// -----------------------------------------------------------------------------
//  [목적]  각 디스플레이 탭이 DSP/검출 코어 내부(Timegrapher·Detector·MainWindow
//          멤버)에 직접 의존하지 않도록, 측정 결과를 하나의 평범한 구조체로 묶어
//          전달한다.  탭은 이 스냅샷만 알면 되므로 코어와 분리된다.
//          → 팀 Milestone1 QA-MOD-01("코어 DSP 수정 없이 새 탭 추가") /
//            "Restrict dependencies" 택틱의 핵심 계약(contract)이다.
//
//  [생성·게시]  MainWindow::DisplayResults() 가 매 측정 갱신마다 이 구조체를 채워
//               TabManager::broadcastMeasurement() 로 모든 탭에 전달한다.
//
//  ※ 파형(PCM)·이벤트 단위 데이터는 여기에 넣지 않는다(스칼라 측정만).
//    파형이 필요한 탭(Scope·Escapement·Waveform·Spectrogram)은 후속 단계에서
//    별도 샘플 스트림 게시 경로를 추가한다(현재 스캐폴드는 스칼라만 사용).
// =============================================================================

#include <cstdint>

// 입력 모드 — MainWindow 의 ModeComboBox 인덱스(LIVE/PLAYBACK/SIM)와 동일 의미.
struct MeasurementSnapshot
{
    double   timeMs        = 0.0;   // 게시 시각 (Perf::nowMs, 단조 시계 ms). 시계열 X축 기준.

    bool     bphValid      = false; // BPH 검출/확정 여부
    int      bph           = 0;     // 검출된 beats-per-hour

    bool     rateValid     = false; // rate(s/d) 유효 여부
    double   rate          = 0.0;   // rate deviation, 초/일 (+빠름 / −느림)

    bool     beatErrorValid= false;
    double   beatErrorMs   = 0.0;   // beat error, ms (롤링 평균)

    bool     amplitudeValid= false;
    double   amplitudeDeg  = 0.0;   // amplitude, 도(°) (롤링 평균)

    bool     synced        = false; // 비트 동기(BPH lock) 상태

    int      sampleRateHz  = 0;     // 현재 샘플레이트
    uint64_t totalSamples  = 0;     // 누적 처리 샘플 수
    int      liftAngle     = 0;     // 현재 lift angle 설정(°)
    int      mode          = 0;     // 0=Live,1=Playback,2=Sim (ModeComboBox 인덱스)

    // ── rate 오차 그래프 시리즈(RatePlot 용) ──────────────────────────────────
    //  rate(s/d) 한 값이 아니라 '시간에 따른 tic/toc 래핑 오차 점들'(최근 N개)의 배열.
    //  Rate/Scope 탭이 RatePlot 선을 그리는 데 필요. 포인터는 broadcast 동안만 유효 →
    //  탭은 필요분을 자기 버퍼로 복사할 것. (WaveBlock 의 env/raw 포인터와 동일 규약)
    const double *rateTicX = nullptr;  const double *rateTicY = nullptr;  int rateTicN = 0;
    const double *rateTocX = nullptr;  const double *rateTocY = nullptr;  int rateTocN = 0;
    int           rateMaxPoints = 0;   // tic/toc 롤링 버퍼 크기(점 개수); X값은 초 단위 시각
};

// ── 파형(PCM/엔벨로프) 게시 ────────────────────────────────────────────────
//  스코프 계열 탭(Escapement·Beat Scope·Waveform·Sync Sweep·Spectrogram·F0~F3)은
//  스칼라 측정값이 아니라 '신호 자체'가 필요하다.  MainWindow::ProcessSamples 가 매
//  처리 슬라이스마다 아래 WaveBlock 을 만들어 TabManager::broadcastWave() 로 전달한다.
//   - env: 검출 코어가 산출한 (지연정렬된) 엔벨로프 = 시스템이 가진 '신호의 절대값 표현'.
//          이벤트(A/C)의 절대 샘플 인덱스와 정렬되어 있어 마커 표시에 그대로 쓸 수 있다.
//          (F0~F3 의 F0 "가장 가까운 신호 표현"으로도 이 엔벨로프를 사용)
//   - 포인터(env/events)는 호출 동안만 유효 → 탭은 필요한 만큼 자기 버퍼로 복사할 것.
struct WaveEvent
{
    uint64_t sample = 0;   // 절대 샘플 인덱스 (A=onset/peak, C=peak) — 원시 검출 위치
    int      type   = 0;   // 1=A(unlock), 2=C(drop/lock)  (tg_event_type_t)
    float    peak   = 0.0f;// 엔벨로프 피크값
    uint64_t markSample = 0; // 표시용 해상 위치(A=sample, C=onset 또는 peak; UseConset 선택 반영)
};

struct WaveBlock
{
    const float    *env        = nullptr; // 엔벨로프 샘플 [n] (정류·평활·지연정렬, 이벤트와 동일 좌표)
    int             n          = 0;
    uint64_t        startSample= 0;       // env[0] 의 절대 입력 샘플 인덱스
    int             sampleRateHz = 0;
    int             bph        = 0;       // 검출 BPH(0=미동기)
    bool            synced     = false;
    const WaveEvent*events     = nullptr; // 이 블록의 이벤트 [numEvents]
    int             numEvents  = 0;
    // ── 원신호(raw PCM) ── F0~F3 필터 뷰는 정류 전 바이폴라 원신호가 필요하다.
    //  env 와 동일한 0-기반 입력 좌표를 쓰므로 이벤트/엔벨로프와 정렬된다.
    const float    *raw        = nullptr; // 원 입력 샘플 [rawN]
    int             rawN       = 0;
    uint64_t        rawStart   = 0;       // raw[0] 의 절대 입력 샘플 인덱스
    // 검출기 onset 임계(엔벨로프 절대 레벨) — Escapement 탭의 threshold 선 출처(tg_result_t.onset_threshold).
    float           onsetThreshold = 0.0f;
};

#endif // MEASUREMENTMODEL_H
