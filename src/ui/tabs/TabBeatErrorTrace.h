#ifndef TABBEATERRORTRACE_H
#define TABBEATERRORTRACE_H
// Beat Error Display & Diagnostic Trace 탭 (FR-BED) — WeiShi 1000 / Watch-O-Scope 스타일.
//  Equations_v0 Part I 그대로: 비트 n 마다 E_n = T_measured − (T_start + n·I_target)  (E2),
//  Y = E_n 의 모듈로 랩(±10ms 창)  (E3),  짝/홀 비트(위상)별 두 점-라인.
//  기울기 = rate (E6: R = −(m/I_target)·86400), 두 라인 간 수직 간격 = beat error.
//  Good/Bad: beat error(스냅샷)만 — grading-rationale 밴드: Good≤0.7 · Caution≤1.0 · Service>1.0 ms.
//  rate 는 readout 정보로만 표시(조정 항목, Good/Bad 판정 제외).
#include "TabView.h"
#include "TrendSeek.h"   // [③] 클릭→seek 헬퍼
class QCustomPlot;
class QCPItemLine;
class QCPItemText;
class QLabel;

class TabBeatErrorTrace : public TabView
{
    Q_OBJECT
public:
    explicit TabBeatErrorTrace(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Beat Error Display and Diagnostic Trace"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    bool resetOnResume() const override { return true; }   // [pause→start] resume 시 그래프·데이터 초기화
    void onSeek(double absSample) override { mSeek.showCursorAtSample(absSample); }   // [③] 다른 탭 seek → 커서 동기화
    void onResumeLive(bool seeked) override { (void)seeked; mSeek.hideCursor(); }  // [③] resume → 선택 리셋(커서 숨김)
signals:
    void seekRequested(double absSample);   // [③] 정지 중 점 클릭 → 그 비트의 절대 샘플
protected:
    void onShown() override;
private:
    void updateStatusAlert();
    TrendSeek mSeek;   // x(초) → 절대 샘플 매핑 + 클릭 커서
    double   mPlotOriginSec = 0.0;   // 측정 시작(워밍업 종료) 시각(초) — x축·툴팁 t 좌표 원점 통일
    QCustomPlot *mPlot = nullptr;
    // 최신 비트에서 Tic·Toc 두 선 사이 간격(=beat error)을 표시하는 양방향 화살표 + 라벨.
    QCPItemLine *mGapLine = nullptr;
    QCPItemText *mGapText = nullptr;
    // E2 앵커: 측정 시작 후 첫 유효 A 비트.
    bool     mAnchored = false;
    uint64_t mTstart   = 0;      // T_start (절대 샘플)
    long     mN        = 0;      // 다음 비트 번호 n
    uint64_t mLastA    = 0;      // 마지막으로 처리한 A 샘플(중복 방지)
    int      mBph      = 0;      // 앵커 시점 BPH (변경 시 재앵커)
    double   mPrevE    = 0.0; bool mHavePrevE = false;   // 기울기(ΔE/Δn)용
    long     mPrevN    = 0;                              // 직전 비트 번호(누락 보정)
    double   mSlopeAvg = 0.0;                            // ΔE 지수평활(기울기 안정화)
    double   mBeatErrMs = 0.0; bool mBeatErrValid = false;  // 스냅샷 beat error(간격 경고)
    double   mRateSd    = 0.0; bool mRateValid    = false;  // 스냅샷 rate — 표시용
    int      mAlertTier = 0;   // [BED-status] 0=unknown 1=Good 2=Caution 3=Bad
    // grading-rationale.md beat-error bands (ms)
    static constexpr double kGoodMs    = 0.7;   // Good 상한 (≤)
    static constexpr double kServiceMs = 1.0;   // Service 경고 (>)
    static constexpr double kWrapMs   = 20.0;  // 모듈로 랩 창(±10ms) — E3 Plot Height
    static constexpr int    kMaxDots  = 2000;  // 점 보존 한도(메모리 바운드)
};
#endif // TABBEATERRORTRACE_H
