#include "TabLongTermPerformance.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"

#include <cmath>

void TabLongTermPerformance::Lane::add(double x, double v)
{
    if (!have) { min = max = v; xFirst = x; have = true; }
    else { if (v < min) min = v; if (v > max) max = v; }
    sum += v; sumSq += v * v; ++n; xLast = x;
}

double TabLongTermPerformance::Lane::sigma() const
{
    if (n < 2) return 0.0;
    const double m = avg(); const double var = sumSq / n - m * m;
    return var > 0.0 ? std::sqrt(var) : 0.0;
}

static QCustomPlot *makeLane(QWidget *parent, const QString &yLabel, const QColor &line, bool xLabels)
{
    auto *p = new QCustomPlot(parent);
    p->addGraph(); p->graph(0)->setPen(QPen(line, 1));                          // 메인 트레이스
    p->addGraph(); p->graph(1)->setPen(QPen(line.darker(140), 1, Qt::DashLine));// 평균선
    p->yAxis->setLabel(yLabel);
    p->xAxis->setTickLabels(xLabels);
    if (xLabels) p->xAxis->setLabel(QStringLiteral("time (s)"));
    return p;
}

TabLongTermPerformance::TabLongTermPerformance(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    mRate.plot = makeLane(this, QStringLiteral("rate s/d"),     QColor(200,40,140), false);
    mAmp.plot  = makeLane(this, QStringLiteral("amplitude °"),  QColor(40,80,200),  false);
    mBe.plot   = makeLane(this, QStringLiteral("beat err ms"),  QColor(30,150,60),  true);
    // 변동범위 밴드(반투명) — 각 레인.
    mRate.band = new QCPItemRect(mRate.plot); mRate.band->setPen(Qt::NoPen); mRate.band->setBrush(QColor(200,40,140,40));
    mAmp.band  = new QCPItemRect(mAmp.plot);  mAmp.band->setPen(Qt::NoPen);  mAmp.band->setBrush(QColor(40,80,200,40));
    mBe.band   = new QCPItemRect(mBe.plot);   mBe.band->setPen(Qt::NoPen);   mBe.band->setBrush(QColor(30,150,60,40));
    lay->addWidget(mRate.plot, 1);
    lay->addWidget(mAmp.plot, 1);
    lay->addWidget(mBe.plot, 1);
    onResetSession();
}

void TabLongTermPerformance::redrawLane(Lane &L, const QColor &)
{
    if (!L.have) return;
    L.plot->graph(1)->setData({L.xFirst, L.xLast}, {L.avg(), L.avg()});   // 기간 평균선
    // Plan: "visually indicate the range of typical variation" → 평균±σ 밴드(이상치에 안 끌려감).
    const double sg = L.sigma();
    L.band->topLeft->setCoords(L.xFirst, L.avg() + sg);
    L.band->bottomRight->setCoords(L.xLast, L.avg() - sg);
}

void TabLongTermPerformance::onMeasurement(const MeasurementSnapshot &s)
{
    mBar->update(s);
    if (!mHaveT0) { mT0 = s.timeMs; mHaveT0 = true; }
    const double x = (s.timeMs - mT0) / 1000.0;

    // 데시메이션: 경과(분) 증가 → 점 추가 간격 K 증가(장시간 가독성/효율).
    const long K = 1 + (long)(x / 60.0);
    const bool addPoint = (mTick++ % K == 0);
    if (addPoint) {
        if (s.rateValid)      { mRate.add(x, s.rate);        mRate.plot->graph(0)->addData(x, s.rate); }
        if (s.amplitudeValid) { mAmp.add(x, s.amplitudeDeg); mAmp.plot->graph(0)->addData(x, s.amplitudeDeg); }
        if (s.beatErrorValid) { mBe.add(x, s.beatErrorMs);   mBe.plot->graph(0)->addData(x, s.beatErrorMs); }
        redrawLane(mRate, {}); redrawLane(mAmp, {}); redrawLane(mBe, {});
    }
    if (isVisible()) {
        for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) { p->rescaleAxes(); p->replot(QCustomPlot::rpQueuedReplot); }
    }
}

void TabLongTermPerformance::onShown()
{
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) if (p) p->replot();
}

void TabLongTermPerformance::onResetSession()
{
    mHaveT0 = false; mTick = 0;
    for (Lane *L : {&mRate, &mAmp, &mBe}) {
        L->sum=0; L->sumSq=0; L->min=0; L->max=0; L->n=0; L->have=false; L->xFirst=L->xLast=0;
        if (L->plot) { L->plot->graph(0)->data()->clear(); L->plot->graph(1)->data()->clear(); }
        if (L->band) { L->band->topLeft->setCoords(0,0); L->band->bottomRight->setCoords(0,0); }
    }
    if (mBar) mBar->update(MeasurementSnapshot{});
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) if (p) p->replot();
}
