#include "TabLongTermPerformance.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"

#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
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
    // 각 레인 X축에 경과 시간(mm:ss) 눈금 표시.
    auto timeTicker = QSharedPointer<QCPAxisTickerTime>::create();
    timeTicker->setTimeFormat(QStringLiteral("%m:%s"));
    p->xAxis->setTicker(timeTicker);
    p->xAxis->setTickLabels(true);
    if (xLabels) p->xAxis->setLabel(QStringLiteral("time (mm:ss)"));
    return p;
}

TabLongTermPerformance::TabLongTermPerformance(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);

    // 컨트롤: X축은 8분 고정이라 기간 선택 없음 — 리셋만 제공.
    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("X axis: fixed 8 min (sliding after)"), this));
    ctl->addStretch(1);
    auto *resetBtn = new QPushButton(QStringLiteral("↻ Reset"), this);
    ctl->addWidget(resetBtn);
    lay->addLayout(ctl);
    connect(resetBtn, &QPushButton::clicked, this, &TabLongTermPerformance::onResetSession);

    mRate.plot = makeLane(this, QStringLiteral("rate s/d"),     QColor(200,40,140), false);
    mAmp.plot  = makeLane(this, QStringLiteral("amplitude °"),  QColor(40,80,200),  false);
    mBe.plot   = makeLane(this, QStringLiteral("beat err ms"),  QColor(30,150,60),  true);
    // 변동범위 밴드(반투명) + 우측 상단 통계 텍스트(최대/최소/σ) — 각 레인.
    struct LB { Lane *L; QColor c; } lanes[] = {
        {&mRate, QColor(200,40,140)}, {&mAmp, QColor(40,80,200)}, {&mBe, QColor(30,150,60)} };
    for (const LB &lb : lanes) {
        Lane *L = lb.L;
        L->band = new QCPItemRect(L->plot); L->band->setPen(Qt::NoPen);
        L->band->setBrush(QColor(lb.c.red(), lb.c.green(), lb.c.blue(), 40));
        // 그래프 맨 오른쪽(우상단)에 고정된 통계 라벨.
        L->stats = new QCPItemText(L->plot);
        L->stats->setLayer(QStringLiteral("overlay"));
        L->stats->position->setType(QCPItemPosition::ptAxisRectRatio);
        L->stats->position->setCoords(0.995, 0.04);
        L->stats->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
        L->stats->setTextAlignment(Qt::AlignRight);
        L->stats->setFont(QFont(QStringLiteral("monospace"), 8));
        L->stats->setColor(lb.c.darker(150));
        L->stats->setPadding(QMargins(4,2,4,2));
        L->stats->setBrush(QColor(255,255,255,190));
        L->stats->setPen(QPen(QColor(lb.c.red(), lb.c.green(), lb.c.blue(), 120)));
    }
    lay->addWidget(mRate.plot, 1);
    lay->addWidget(mAmp.plot, 1);
    lay->addWidget(mBe.plot, 1);
    onResetSession();
}

void TabLongTermPerformance::redrawLane(Lane &L, const QString &unit)
{
    if (!L.have) return;
    L.plot->graph(1)->setData({L.xFirst, L.xLast}, {L.avg(), L.avg()});   // 기간 평균선
    // Plan: "visually indicate the range of typical variation" → 평균±σ 밴드(이상치에 안 끌려감).
    const double sigma = L.sigma();
    L.band->topLeft->setCoords(L.xFirst, L.avg() + sigma);
    L.band->bottomRight->setCoords(L.xLast, L.avg() - sigma);
    // 그래프 맨 오른쪽 통계 라벨: 최대/최소/표준편차.
    if (L.stats)
        L.stats->setText(QStringLiteral("max %1%4\nmin %2%4\n σ  %3%4")
                             .arg(L.max, 0, 'f', 2).arg(L.min, 0, 'f', 2)
                             .arg(sigma, 0, 'f', 2).arg(unit));
}

void TabLongTermPerformance::applyView()
{
    // X축 8분 고정: 경과<8분이면 [0,8분], 그 이후엔 최근 8분만 보이도록 흘러간다.
    const double lo = (mCurX <= kWindowSec) ? 0.0 : (mCurX - kWindowSec);
    const double hi = lo + kWindowSec;
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) {
        if (!p) continue;
        p->xAxis->setRange(lo, hi);
        p->graph(0)->rescaleValueAxis(false, true);       // 보이는 구간 기준 세로 스케일
        p->yAxis->scaleRange(1.1, p->yAxis->range().center());
    }
}

void TabLongTermPerformance::onMeasurement(const MeasurementSnapshot &s)
{
    mBar->update(s);
    if (!mHaveT0) { mT0 = s.timeMs; mHaveT0 = true; }
    const double x = (s.timeMs - mT0) / 1000.0;
    mCurX = x;

    // 데시메이션: 경과(분) 증가 → 점 추가 간격 K 증가(장시간 가독성/효율).
    const long K = 1 + (long)(x / 60.0);
    const bool addPoint = (mTick++ % K == 0);
    if (addPoint) {
        if (s.rateValid)      { mRate.add(x, s.rate);        mRate.plot->graph(0)->addData(x, s.rate); }
        if (s.amplitudeValid) { mAmp.add(x, s.amplitudeDeg); mAmp.plot->graph(0)->addData(x, s.amplitudeDeg); }
        if (s.beatErrorValid) { mBe.add(x, s.beatErrorMs);   mBe.plot->graph(0)->addData(x, s.beatErrorMs); }
        redrawLane(mRate, QString());
        redrawLane(mAmp, QStringLiteral("°"));
        redrawLane(mBe, QStringLiteral("ms"));
    }
    if (isVisible()) {
        applyView();
        for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) p->replot(QCustomPlot::rpQueuedReplot);
    }
}

void TabLongTermPerformance::onShown()
{
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) if (p) p->replot();
}

void TabLongTermPerformance::onResetSession()
{
    mHaveT0 = false; mTick = 0; mCurX = 0.0;
    for (Lane *L : {&mRate, &mAmp, &mBe}) {
        L->sum=0; L->sumSq=0; L->min=0; L->max=0; L->n=0; L->have=false; L->xFirst=L->xLast=0;
        if (L->plot) { L->plot->graph(0)->data()->clear(); L->plot->graph(1)->data()->clear(); }
        if (L->band) { L->band->topLeft->setCoords(0,0); L->band->bottomRight->setCoords(0,0); }
        if (L->stats) L->stats->setText(QString());
    }
    if (mBar) mBar->update(MeasurementSnapshot{});
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) if (p) p->replot();
}
