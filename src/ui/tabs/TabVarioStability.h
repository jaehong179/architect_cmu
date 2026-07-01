#ifndef TABVARIOSTABILITY_H
#define TABVARIOSTABILITY_H
// Vario Display 탭 (FR-RAS): rate·amplitude 장기 안정성 통계(min/max/avg/σ/경과/현재) +
//  그래픽 표현(녹색 허용영역·파란 min/max 화살표·빨간 평균 화살표) (FR-RAS-1.1).
#include "TabView.h"
class QCustomPlot;
class QLabel;

class TabVarioStability : public TabView
{
    Q_OBJECT
public:
    explicit TabVarioStability(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Rate and Amplitude Stability"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    struct Stat {
        long n = 0; double min = 0, max = 0, sum = 0, sumSq = 0, last = 0;
        void add(double v);
        double avg()   const { return n ? sum / n : 0.0; }
        double sigma() const;
    };
    void drawBar(QCustomPlot *p, const Stat &st, double bandLo, double bandHi, double dispLo, double dispHi);
    void refresh();
    Stat         mRate, mAmp;
    double       mT0 = 0.0; bool mHaveT0 = false; double mElapsed = 0.0;
    QLabel      *mRateLbl = nullptr, *mAmpLbl = nullptr, *mElapsedLbl = nullptr;
    QCustomPlot *mRateBar = nullptr, *mAmpBar = nullptr;
    static constexpr double kRateBandLo = -7.0,  kRateBandHi = 7.0;    // rate 양호 밴드(s/d)
    static constexpr double kAmpBandLo  = 270.0, kAmpBandHi  = 300.0;  // amplitude 양호 밴드(°)
    static constexpr double kRateDispLo = -10.0, kRateDispHi = 15.0;   // rate 게이지 기본 스케일(사양)
    static constexpr double kAmpDispLo  = 180.0, kAmpDispHi  = 330.0;  // amplitude 게이지 기본 스케일(사양)
};
#endif // TABVARIOSTABILITY_H
