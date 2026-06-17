#ifndef SIMCONFIGBUILDER_H
#define SIMCONFIGBUILDER_H
// 시뮬레이션 합성 설정(WatchSynthStreamConfig) 조립 — UI 위젯 읽기와 분리(SoC).
//  '어느 베이스(realistic/clean)에서 시작해 어떤 필드를 채우고, 합성 도메인 상수(예: PCM 출력 레벨)는
//  무엇인가'라는 합성기 도메인 지식을 UI(MainWindow::SimStart) 밖으로 뺀다. UI 는 위젯값을
//  SimConfigParams 로 모아 넘기기만 하고, 합성기 구조·기본값·규약(예: beat_error 부호)은 여기서 안다.
#include "WatchSynthStream.h"

struct SimConfigParams
{
    bool   realistic = true;          // true=현실적 합성(노이즈/변동 포함), false=클린
    int    bph = 0;                   // beats/hour
    int    sampleRateHz = 0;
    double beatErrorMs = 0.0;         // UI 스핀값(양수). 합성기에는 음수 규약으로 전달됨
    double watchAmplitudeDeg = 0.0;
    double liftAngleDeg = 0.0;
    double rateErrorSecPerDay = 0.0;
};

namespace SimConfigBuilder {
    // params + 합성기 도메인 규약으로 완성된 WatchSynthStreamConfig 를 만든다.
    WatchSynthStreamConfig build(const SimConfigParams &p);
}

#endif // SIMCONFIGBUILDER_H
