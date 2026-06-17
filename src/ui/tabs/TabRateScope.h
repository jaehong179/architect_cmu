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

class TabRateScope : public TabView
{
    Q_OBJECT
public:
    explicit TabRateScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Rate/Scope"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
signals:
    void scopeReplotted();   // ScopePlot afterReplot → MainWindow::OnScopeReplotted 로 연결(perf)
private:
    void setupPlots();
    void addVerticalMarker(double x, double height, const QColor &color);
    void addText(double x, double height, const QString &text, const QColor &color, Qt::Alignment alignment);
    void addHorizontalMarkerInward(double xLeft, double xRight, double length, double height, const QColor &color);
    void addHorizontalMarkerOutward(double xLeft, double xRight, double height, const QColor &color);
    void removeMarkersAndText(double rangeMin, double rangeMax);
    void purgeHistory();

    QCustomPlot *mRatePlot   = nullptr;
    QCustomPlot *mScopePlot  = nullptr;
    QSpinBox    *mScopeScale = nullptr;
    uint64_t     mGraphTicks = 0;        // 엔벨로프 샘플 카운터(구 mLocalGraphTicks)
    double       mLastA = 0.0; bool mHaveLastA = false;
    int          mSampleRateHz = 48000;
    int          mLiftAngle = 52;        // C 마커 진폭 라벨용(onMeasurement 에서 갱신)
    // min/max 데시메이션 누적 상태(고레이트 스코프: 점 수↓ + 피크 보존). 구간을 onWave 경계 넘어 누적.
    int          mDecimCount = 0;
    float        mDecimMin = 0.0f, mDecimMax = 0.0f;
    uint64_t     mDecimMinTick = 0, mDecimMaxTick = 0;
};
#endif // TABRATESCOPE_H
