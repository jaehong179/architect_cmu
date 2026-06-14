#ifndef TABTRACEDISPLAY_H
#define TABTRACEDISPLAY_H
// Trace Display 탭 (FR-TD) — Witschi Trace 스타일: 상단 readout + rate(상)·amplitude(하) 2단 스택.
//  s/d 스무딩(FR-TD-2), late 알림(FR-TD-3), amplitude 270~300° 알림+시각 밴드(FR-TD-4),
//  세션/롤링 평균(FR-TD-6), 파생 측정 DiffTicTac/DiffPeriod/AvgPeriod (Project Plan §Expected Enhancements).
#include "TabView.h"
#include <QVector>
class QCustomPlot;
class QCPItemRect;
class QLabel;
class ReadoutBar;

class TabTraceDisplay : public TabView
{
    Q_OBJECT
public:
    explicit TabTraceDisplay(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Trace"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    ReadoutBar  *mBar   = nullptr;
    QCustomPlot *mRate  = nullptr;   // 상단: rate(raw+smoothed)
    QCustomPlot *mAmp   = nullptr;   // 하단: amplitude
    QCPItemRect *mAmpBand = nullptr; // 진폭 정상범위(270~300°) 시각 밴드
    QLabel      *mAlert = nullptr;
    QLabel      *mDerived = nullptr; // 파생 측정(DiffTicTac/DiffPeriod/AvgPeriod) + 롤링/세션 평균
    double       mT0 = 0.0; bool mHaveT0 = false;
    QVector<double> mRateWin; int mSmoothN = 20;
    double mRateSum=0, mAmpSum=0; long mRateN=0, mAmpN=0;
    // 파생 측정: 비트 주기 편차(ms) — DiffPeriod(최근 4초 창) / AvgPeriod(세션 누적).
    QVector<QPair<double,double>> mDevWin;   // (x초, 편차ms) 최근 4초
    double mDevSum = 0; long mDevN = 0;
    static constexpr double kAmpLo=270.0, kAmpHi=300.0, kLateSlow=-1.0;
    static constexpr double kDiffPeriodWinS = 4.0;   // Chour DiffPeriod 창(초)
};
#endif // TABTRACEDISPLAY_H
