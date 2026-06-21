#ifndef TABRATESCOPE_H
#define TABRATESCOPE_H
// Rate/Scope 탭 — rate 오차 시계열(RatePlot) + 실시간 파형 스코프(ScopePlot).
//  과거에는 MainWindow.ui 의 정적 RateTab + MainWindow 인라인 렌더링이었으나, 다른 탭과
//  동일하게 TabView 로 분리한다:
//   · RatePlot  ← onMeasurement (snapshot 의 tic/toc rate 시리즈)
//   · ScopePlot ← onWave        (엔벨로프 + A/C 이벤트 마커)
//  [perf] ScopePlot 의 실제 paint 완료(afterReplot)를 scopeReplotted() 시그널로 알려,
//   MainWindow 가 disp_paint_ms·e2e_full_ms·paint_fps 를 기록한다(perf 상태는 MainWindow 잔류).
#include "TabView.h"
#include <QColor>
class QCustomPlot;
class QSpinBox;
class WaveLodHistory;     // 8분 엔벨로프 이력 버퍼(중앙 1개) — pause 중 스크롤백 렌더 원본

class TabRateScope : public TabView
{
    Q_OBJECT
public:
    explicit TabRateScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Rate/Scope"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    void onSeek(double absSample) override;   // [③] 정지 중 트렌드 클릭 → 그 시점으로 스코프 이동

    // 8분 이력 버퍼 주입(MainWindow 소유). pause 중 이 버퍼를 queryWindow 로 그린다.
    void setHistory(WaveLodHistory *h) { mHistory = h; }

    // 정지 ↔ 8분 이력 스크롤백 전환. 전역 Pause 버튼(MainWindow)이 호출한다.
    void setPaused(bool paused);
signals:
    void scopeReplotted();   // ScopePlot afterReplot → MainWindow::OnScopeReplotted 로 연결(perf)
private:
    void setupPlots();
    void renderHistoryWindow();         // 현재 보이는 시간창을 이력에서 잘라 그림(줌/팬 시 재호출)
    void drawHistoryMarkers(uint64_t fromAbs, uint64_t toAbs);   // 이력 이벤트로 A/C 마커·ms 복원
    void addVerticalMarker(double x, double height, const QColor &color);
    void addText(double x, double height, const QString &text, const QColor &color, Qt::Alignment alignment);
    void addHorizontalMarkerInward(double xLeft, double xRight, double length, double height, const QColor &color);
    void addHorizontalMarkerOutward(double xLeft, double xRight, double height, const QColor &color);
    void removeMarkersAndText(double rangeMin, double rangeMax);
    void purgeHistory();

    QCustomPlot    *mRatePlot   = nullptr;
    QCustomPlot    *mScopePlot  = nullptr;
    QSpinBox       *mScopeScale = nullptr;
    WaveLodHistory *mHistory    = nullptr;      // 주입된 중앙 이력 버퍼(소유 안 함)
    bool            mPaused     = false;        // true=이력 스크롤백 모드
    bool            mInHistoryRender = false;   // rangeChanged 재귀 가드
    bool            mHistActive = false;        // 정지 후 사용자가 드래그/줌해 이력 렌더로 전환됨
    double          mHistOffset = 0.0;          // 이력 절대인덱스 − 라이브 mGraphTicks (정지 시 고정)
    uint64_t        mGraphTicks = 0;            // 엔벨로프 샘플 카운터
    double       mLastA = 0.0; bool mHaveLastA = false;
    int          mSampleRateHz = 48000;
    int          mLiftAngle = 52;        // C 마커 진폭 라벨용(onMeasurement 에서 갱신)
    int          mLastBph = 0;           // 이력 마커 진폭 계산용(onWave 에서 갱신)
    // min/max 데시메이션 누적 상태(고레이트 스코프: 점 수↓ + 피크 보존). 구간을 onWave 경계 넘어 누적.
    int          mDecimCount = 0;
    float        mDecimMin = 0.0f, mDecimMax = 0.0f;
    uint64_t     mDecimMinTick = 0, mDecimMaxTick = 0;
};
#endif // TABRATESCOPE_H
