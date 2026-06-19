#ifndef WAVESINK_H
#define WAVESINK_H
// =============================================================================
//  WaveSink — 엔벨로프(WaveBlock) 한 갈래만 받는 좁은 청취자 인터페이스
// -----------------------------------------------------------------------------
//  [목적]  TabManager 의 wave 방송(broadcastWave)에, 탭이 아닌 비시각 수집기
//          (예: 8분 이력 버퍼 WaveLodHistory)를 등록하기 위한 최소 계약.
//
//  [SOLID]
//   - ISP: 이력 버퍼는 onWave 만 필요. tabTitle/onMeasurement/onResetSession 등
//          무거운 TabView(QWidget) 계약을 떠안지 않는다(인터페이스 분리).
//   - OCP: TabManager 방송 로직을 수정하지 않고 "청취자만 추가"해 확장한다.
//   - SRP: CaptureController 는 그대로 방송만 하고, 저장은 청취자가 자기 책임으로.
//
//  ※ 순수 추상(데이터 멤버 없음). WaveBlock 은 참조 인자라 전방선언으로 충분.
// =============================================================================

struct WaveBlock;

class WaveSink
{
public:
    virtual ~WaveSink() = default;

    // 파형(엔벨로프+이벤트) 블록 도착 시 호출(메인 스레드, 처리 슬라이스마다).
    //  포인터(env/events)는 호출 동안만 유효 → 필요분을 자기 버퍼로 복사할 것.
    virtual void onWave(const WaveBlock &wave) = 0;
};

#endif // WAVESINK_H
