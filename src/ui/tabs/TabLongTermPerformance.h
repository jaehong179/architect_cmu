#ifndef TABLONGTERMPERFORMANCE_H
#define TABLONGTERMPERFORMANCE_H
// Long-Term Performance 탭 (FR-LTP) — Watch-O-Scope 스타일: 상단 readout + rate/amplitude/beat error
//  3단 스택. 각 단에 기간평균(점선)·변동범위 밴드(min/max). 경과 따라 데시메이션.
//  X축은 8분 폭으로 고정, 8분 경과 후에는 최근 8분만 보이도록 흘러간다(슬라이딩).
//  각 레인 우측 상단에 최대/최소/표준편차 수치를 고정 표시.
#include "TabView.h"
class QCustomPlot;
class ReadoutBar;
class QCPItemRect;
class QCPItemStraightLine;
class QCPItemText;

class TabLongTermPerformance : public TabView
{
    Q_OBJECT
public:
    explicit TabLongTermPerformance(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Long-Term Performance Graph"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    struct Lane { QCustomPlot *plot=nullptr; QCPItemRect *band=nullptr; QCPItemText *stats=nullptr;
                  double sum=0,sumSq=0,min=0,max=0,xFirst=0,xLast=0; long n=0; bool have=false;
                  void add(double x,double v); double avg() const { return n?sum/n:0; }
                  double sigma() const; };
    void redrawLane(Lane &L, const QString &unit);
    void applyView();                       // 8분 고정 + 8분 경과 후 슬라이딩, 세로 스케일 갱신
    ReadoutBar *mBar = nullptr;
    Lane mRate, mAmp, mBe;
    double mT0=0.0; bool mHaveT0=false; long mTick=0; double mCurX=0.0;
    static constexpr double kWindowSec = 480.0;   // X축 고정 폭(8분)
};
#endif // TABLONGTERMPERFORMANCE_H
