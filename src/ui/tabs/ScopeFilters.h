#ifndef SCOPEFILTERS_H
#define SCOPEFILTERS_H
// F0~F3 스코프 필터 — Project Plan §Scope Function with Multiple Filter Views (FR-SFM).
//  하드웨어 스코프(블루 스코프 4패널)에 맞춘 '주파수 필터링된 바이폴라 파형' 모델.
//  한 개의 캡처 원신호(raw)에 대해(avg = raw 평균, 화면 중심):
//   F0 = raw - avg            (필터 없음 — 하이한 노이즈)                 [bipolar, mirror]
//   F1 = BP 2~10kHz           (광대역 — 고주파 노이즈 깎임, 덩어리)        [bipolar, mirror]
//   F2 = BP 4~6kHz            (협대역 — 타격 주파수만, 버스트+무음구간)    [bipolar, mirror]
//   F3 = LPF(|raw-avg|)       (포락선 검파 — 거대한 산 엔벨로프)           [upper]
//   T1/T2/T3 = 한 Tick 의 1·2·3번째 펄스(F2 엔벨로프 기준 검출)
//  표시: 미러 패널은 ±|out|(컬럼별 최대=오실로스코프 포락) → 필터링된 파형이 채워져 보임.
#include <QVector>

// rawFull(원신호)로 F0~F3 출력 4개와 펄스 인덱스(rawFull 공간)를 계산.
//  out[0..2] bipolar, out[3] 비음수. pulses[0..2]=T1,T2,T3(없으면 -1). sr=샘플레이트(필터용).
void computeScopeFilters(const QVector<double> &rawFull, int sr, QVector<double> out[4], int pulses[3]);

extern const bool   kScopeMirror[4];       // {true,true,true,false} — 미러/upper 표시
extern const char  *kScopeFilterShort[4];  // {"F0 Raw","F1 BP2-10k","F2 BP4-6k","F3 엔벨로프"}
#endif // SCOPEFILTERS_H
