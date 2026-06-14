#ifndef TABLONGTERMPERFORMANCE_H
#define TABLONGTERMPERFORMANCE_H
// Long-Term Performance 탭 (FR-LTP) — Watch-O-Scope 스타일: 상단 readout + rate/amplitude/beat error
//  3단 스택. 각 단에 기간평균(점선)·변동범위 밴드(min/max). 경과 따라 데시메이션.
#include "TabView.h"
class QCustomPlot;
class ReadoutBar;
class QCPItemRect;
class QCPItemStraightLine;

class TabLongTermPerformance : public TabView
{
    Q_OBJECT
public:
    explicit TabLongTermPerformance(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Long-Term"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    struct Lane { QCustomPlot *plot=nullptr; QCPItemRect *band=nullptr;
                  double sum=0,sumSq=0,min=0,max=0,xFirst=0,xLast=0; long n=0; bool have=false;
                  void add(double x,double v); double avg() const { return n?sum/n:0; }
                  double sigma() const; };
    void redrawLane(Lane &L, const QColor &bandColor);
    ReadoutBar *mBar = nullptr;
    Lane mRate, mAmp, mBe;
    double mT0=0.0; bool mHaveT0=false; long mTick=0;
};
#endif // TABLONGTERMPERFORMANCE_H
