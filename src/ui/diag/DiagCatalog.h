#pragma once
// =============================================================================
//  DiagCatalog — 진단(고장유형) 카탈로그 + 유형별 예시 트레이스 그리기
// -----------------------------------------------------------------------------
//  목적: 진단 모델 출력 라벨(키)을 사람이 읽는 제목·원인·조치사항과
//        "예시 그래프(점선 트레이스 패턴)"로 매핑한다.
//  의존성: Qt Gui(QPainter) 뿐. 다른 앱 모듈(엔진/탭/워커)에 의존하지 않는다.
//  라벨 키는 src/tinyml/DiagConfig.h.in 의 kLabels 와 1:1 동일해야 한다.
// =============================================================================
#include <QString>
#include <QRect>

class QPainter;

namespace diagui {

// 유형별 대표 트레이스 모양(WeiShi/Watch-O-Scope 레퍼런스 도해 스타일).
enum DiagPattern {
    PatternNormal = 0,        // 평행한 두 점선(수평)
    PatternBeatErrorHigh,     // 두 선 간격이 벌어짐(비트에러 큼)
    PatternRateFast,          // 가파른 우상향(빠름)
    PatternRateSlow,          // 가파른 우하향(느림)
    PatternGearTrain,         // 크고 규칙적인 사인 변동(기어트레인 결함)
    PatternIrregularAmp,      // 불규칙 굴곡(진폭 부족)
    PatternKnockOccasional,   // 평평하다가 간헐적 계단 점프(간헐 녹킹)
    PatternKnockContinuous,   // 끊긴 조각 점선(지속 녹킹)
    PatternEscapeWheelUntrue, // 매끄러운 주기적 사인(이스케이프휠 편심)
    PatternEntryPallet,       // 우상향 + 번짐/두꺼워짐(인트리 팰릿 불량)
    PatternHairspring,        // 윗선 + 아래로 흩뿌려진 점(헤어스프링 접촉)
    PatternSlowOscillating,   // U자 곡선(자세 변경 후 느린 진동)
};

struct DiagEntry {
    QString key;       // 모델 출력 라벨 키 (kLabels)
    QString title;     // 사람이 읽는 제목
    QString cause;     // 원인/설명
    QString action;    // 조치 사항
    int     pattern;   // DiagPattern
    bool    healthy;   // normal 여부(배너 색/아이콘 구분)
};

// 라벨 키로 카탈로그 항목을 찾는다. 매칭 없으면 nullptr.
const DiagEntry *lookup(const QString &labelKey);

// 유형별 예시 그래프(점선 트레이스)를 r 영역에 그린다(흰 배경 가정).
void paintExample(QPainter &p, const QRect &r, int pattern);

} // namespace diagui
