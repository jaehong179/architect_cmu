#ifndef TABLONGTERMPERFORMANCE_H
#define TABLONGTERMPERFORMANCE_H
// Long-Term Performance 탭 (FR-LTP) — Watch-O-Scope 스타일: 상단 readout + rate/amplitude/beat error
//  3단 스택. 각 단에 기간평균(점선)·변동범위 밴드(min/max). 경과 따라 데시메이션.
//  X축은 10분 폭으로 고정, 10분 경과 후에는 최근 10분만 보이도록 흘러간다(슬라이딩).
//  ※ 파형 이력(WaveLodHistory)은 8분이라, 8분보다 오래된 지점은 클릭해도 파형 탭이 복원 못 함.
//    → 그 선택 불가 구간(8분 이전)을 회색 배경으로 음영 처리하고, 클릭 seek 는 그 경계로 클램프한다.
//  각 레인 우측 상단에 최대/최소/표준편차 수치를 고정 표시.
#include "TabView.h"
#include <QVector>
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
    void onSeek(double absSample) override;   // [③] 다른 탭 seek → 커서 동기화
    void onSeekClear() override;              // [③] 선택 해제 → 세 레인 커서 숨김
    void onResumeLive(bool seeked) override { (void)seeked; onSeekClear(); }  // [③] resume → 선택 리셋(커서 숨김)
signals:
    void seekRequested(double absSample);   // [③] 정지 중 클릭 → 그 시점(스코프 탭 점프)
protected:
    void onShown() override;
private:
    double sampleAtX(double xSeconds) const;     // 클릭 x(초) → 가장 가까운 점의 절대 샘플
    void   showCursor(double xSeconds);          // 세 레인에 클릭 커서선
    QVector<QPair<double,double>> mXtoSample;    // (x초, totalSamples)
    QCPItemStraightLine *mCursors[3]   = {nullptr, nullptr, nullptr};
    QCPItemRect         *mWaveShade[3] = {nullptr, nullptr, nullptr};   // 8분 이전(선택 불가) 구간 회색 배경
    struct Lane { QCustomPlot *plot=nullptr; QCPItemRect *band=nullptr; QCPItemText *stats=nullptr;
                  double sum=0,sumSq=0,min=0,max=0,xFirst=0,xLast=0; long n=0; bool have=false;
                  void add(double x,double v); double avg() const { return n?sum/n:0; }
                  double sigma() const; };
    void redrawLane(Lane &L, const QString &unit);
    void applyView();                       // 8분 고정 + 8분 경과 후 슬라이딩, 세로 스케일 갱신
    ReadoutBar *mBar = nullptr;
    Lane mRate, mAmp, mBe;
    double mT0=0.0; bool mHaveT0=false; long mTick=0; double mCurX=0.0;
    static constexpr double kWindowSec     = 600.0;   // X축 고정 폭(10분, 넘으면 슬라이딩)
    static constexpr double kWaveHistorySec = 480.0;  // 파형 이력 보존(8분) = 클릭 seek 가능 한계
};
#endif // TABLONGTERMPERFORMANCE_H
