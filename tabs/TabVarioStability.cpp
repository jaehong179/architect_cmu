#include "TabVarioStability.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <cmath>

void TabVarioStability::Stat::add(double v)
{
    if (n == 0) { min = max = v; }
    else { if (v < min) min = v; if (v > max) max = v; }
    sum += v; sumSq += v * v; last = v; ++n;
}
double TabVarioStability::Stat::sigma() const
{
    if (n < 2) return 0.0;
    const double m = avg(); const double var = sumSq / n - m * m;
    return var > 0.0 ? std::sqrt(var) : 0.0;
}

static QCustomPlot *makeBar(QWidget *parent)
{
    auto *p = new QCustomPlot(parent);
    p->yAxis->setRange(0, 1); p->yAxis->setVisible(false);
    p->xAxis->setVisible(true);
    p->setMinimumHeight(70);
    return p;
}

TabVarioStability::TabVarioStability(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Vario Display</b> — rate·amplitude 장기 안정성. "
        "녹색=허용범위, 노랑=평균±σ, 파란 화살표=측정 min/max, 빨간 화살표=평균. min/max 폭이 좁을수록 안정. (FR-RAS)"), this));

    mRateLbl = new QLabel(this); mRateLbl->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mRateLbl);
    mRateBar = makeBar(this); lay->addWidget(mRateBar);
    mAmpLbl = new QLabel(this); mAmpLbl->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mAmpLbl);
    mAmpBar = makeBar(this); lay->addWidget(mAmpBar);
    mElapsedLbl = new QLabel(this); mElapsedLbl->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mElapsedLbl);
    lay->addStretch(1);

    onResetSession();
}

static void addArrow(QCustomPlot *p, double x, double yTop, double yBot, const QColor &c, double width)
{
    auto *ln = new QCPItemLine(p);
    ln->start->setCoords(x, yTop);
    ln->end->setCoords(x, yBot);
    ln->setPen(QPen(c, width));
    ln->setHead(QCPLineEnding(QCPLineEnding::esSpikeArrow, 8, 10));
}

void TabVarioStability::drawBar(QCustomPlot *p, const Stat &st, double bandLo, double bandHi,
                                double dispLo, double dispHi)
{
    p->clearItems();
    if (st.n == 0) { p->xAxis->setRange(dispLo, dispHi); p->replot(); return; }
    // 표시 범위: 사양 기본 스케일(dispLo..dispHi)을 기준으로 하되 측정 min/max 가 벗어나면 확장.
    double lo = std::min(dispLo, std::min(st.min, bandLo));
    double hi = std::max(dispHi, std::max(st.max, bandHi));
    p->xAxis->setRange(lo, hi);

    // 녹색 허용영역.
    auto *rect = new QCPItemRect(p);
    rect->topLeft->setCoords(bandLo, 0.78);
    rect->bottomRight->setCoords(bandHi, 0.22);
    rect->setPen(QPen(QColor(0, 150, 0)));
    rect->setBrush(QBrush(QColor(0, 200, 0, 60)));

    // σ 영역(노란 밴드, X±σ) — Witschi Vario 의 평균 주위 노란 구간.
    const double sg = st.sigma();
    if (sg > 0.0) {
        auto *sig = new QCPItemRect(p);
        sig->topLeft->setCoords(st.avg() - sg, 0.74);
        sig->bottomRight->setCoords(st.avg() + sg, 0.26);
        sig->setPen(Qt::NoPen);
        sig->setBrush(QBrush(QColor(240, 220, 0, 110)));
    }

    addArrow(p, st.min, 0.95, 0.55, QColor(0, 80, 220), 2);   // 파란 min
    addArrow(p, st.max, 0.95, 0.55, QColor(0, 80, 220), 2);   // 파란 max
    addArrow(p, st.avg(), 0.05, 0.45, QColor(220, 0, 0), 2);  // 빨간 평균(아래→위)
    p->replot();
}

void TabVarioStability::refresh()
{
    // 사양 수치줄 순서: Min · X(평균) · σ · Max.
    mRateLbl->setText(QString("RATE   Min=%1   X=%2   σ=%3   Max=%4   s/d")
        .arg(mRate.min,0,'f',1).arg(mRate.avg(),0,'f',1).arg(mRate.sigma(),0,'f',2).arg(mRate.max,0,'f',1));
    mAmpLbl->setText(QString("AMP    Min=%1   X=%2   σ=%3   Max=%4   °")
        .arg(mAmp.min,0,'f',0).arg(mAmp.avg(),0,'f',0).arg(mAmp.sigma(),0,'f',2).arg(mAmp.max,0,'f',0));
    // Witschi Vario 표기(예: 1:16)와 동일한 분:초 형식.
    const int es = (int)mElapsed;
    mElapsedLbl->setText(QString("ELAPSED  %1:%2").arg(es / 60).arg(es % 60, 2, 10, QLatin1Char('0')));
    drawBar(mRateBar, mRate, kRateBandLo, kRateBandHi, kRateDispLo, kRateDispHi);
    drawBar(mAmpBar,  mAmp,  kAmpBandLo,  kAmpBandHi,  kAmpDispLo,  kAmpDispHi);
}

void TabVarioStability::onMeasurement(const MeasurementSnapshot &s)
{
    if (mBar) mBar->update(s);
    if (!mHaveT0) { mT0 = s.timeMs; mHaveT0 = true; }
    mElapsed = (s.timeMs - mT0) / 1000.0;
    if (s.rateValid)      mRate.add(s.rate);
    if (s.amplitudeValid) mAmp.add(s.amplitudeDeg);
    if (isVisible()) refresh();
}

void TabVarioStability::onShown() { refresh(); }

void TabVarioStability::onResetSession()
{
    mRate = Stat(); mAmp = Stat(); mHaveT0 = false; mElapsed = 0.0;
    refresh();
}
