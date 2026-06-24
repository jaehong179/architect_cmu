#include "TabLongTermPerformance.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"

#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>   // [③] 클릭 소스
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
    // 우측 끝(예: 10:00) 눈금 라벨이 위젯 가장자리에 잘리지 않도록 우측 최소 여백 확보.
    p->axisRect()->setMinimumMargins(QMargins(0, 0, 30, 0));
    return p;
}

TabLongTermPerformance::TabLongTermPerformance(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);

    // 컨트롤: X축은 8분 고정이라 기간 선택 없음 — 리셋만 제공.
    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("X axis: fixed 10 min (sliding after)"), this));
    ctl->addStretch(1);
    auto *resetBtn = new QPushButton(QStringLiteral("↻ Reset"), this);
    ctl->addWidget(resetBtn);
    lay->addLayout(ctl);
    connect(resetBtn, &QPushButton::clicked, this, &TabLongTermPerformance::onResetSession);

    mRate.plot = makeLane(this, QStringLiteral("rate s/d"),     QColor(200,40,140), false);
    mAmp.plot  = makeLane(this, QStringLiteral("amplitude °"),  QColor(40,80,200),  false);
    mBe.plot   = makeLane(this, QStringLiteral("beat err ms"),  QColor(30,150,60),  true);
    // 세 레인의 좌/우 여백을 공유 → y라벨 폭이 달라도 플롯 시작/끝 x 가 일치(세로 정렬).
    auto *mg = new QCPMarginGroup(mRate.plot);
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot})
        p->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, mg);
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

    // [③] 클릭 소스: 세 레인 어디를 클릭하든 그 시각(절대 샘플)을 방출 + 커서 표시.
    QCustomPlot *plots[3] = { mRate.plot, mAmp.plot, mBe.plot };
    for (int i = 0; i < 3; ++i) {
        mCursors[i] = new QCPItemStraightLine(plots[i]);
        mCursors[i]->setPen(QPen(QColor(200, 0, 200), 1, Qt::DashLine));
        mCursors[i]->setVisible(false);
        // 파형 이력(8분) 이전 = 선택해도 파형 복원 불가 → 그 구간을 회색 배경으로 음영.
        mWaveShade[i] = new QCPItemRect(plots[i]);
        mWaveShade[i]->setPen(Qt::NoPen);
        mWaveShade[i]->setBrush(QColor(0, 0, 0, 28));                       // 반투명 회색
        mWaveShade[i]->setLayer(QStringLiteral("grid"));                    // 데이터·밴드 뒤(배경)
        mWaveShade[i]->topLeft->setTypeX(QCPItemPosition::ptPlotCoords);    // x=시간, y=축높이 비율(0~1)
        mWaveShade[i]->topLeft->setTypeY(QCPItemPosition::ptAxisRectRatio);
        mWaveShade[i]->bottomRight->setTypeX(QCPItemPosition::ptPlotCoords);
        mWaveShade[i]->bottomRight->setTypeY(QCPItemPosition::ptAxisRectRatio);
        mWaveShade[i]->setVisible(false);
        QCustomPlot *pl = plots[i];
        connect(pl, &QCustomPlot::mousePress, this, [this, pl](QMouseEvent *e) {
            if (mXtoSample.isEmpty()) return;
            const double x = pl->xAxis->pixelToCoord(e->position().x());
            // 파형 이력(8분) 밖이면 경계로 클램프 — 파형 탭이 복원 가능한 가장 오래된 시점.
            //  커서는 onSeek 왕복(broadcastSeek 가 pause 게이트)으로만 표시 → 선택은 정지 중에만.
            const double seekX = (mCurX > kWaveHistorySec) ? qMax(x, mCurX - kWaveHistorySec) : x;
            emit seekRequested(sampleAtX(seekX));
        });
    }

    onResetSession();
}

// [③] 다른 탭에서 온 seek(절대 샘플) → 가장 가까운 점의 x(초)로 세 레인 커서를 옮긴다(트렌드 동기화).
void TabLongTermPerformance::onSeek(double absSample)
{
    if (mXtoSample.isEmpty()) return;
    double bestX = mXtoSample.first().first, bestErr = qAbs(mXtoSample.first().second - absSample);
    for (const auto &p : mXtoSample) {
        const double err = qAbs(p.second - absSample);
        if (err < bestErr) { bestErr = err; bestX = p.first; }
    }
    showCursor(bestX);
}

// [③] 선택 해제 — 세 레인의 클릭 커서를 숨긴다(데이터·축은 유지).
void TabLongTermPerformance::onSeekClear()
{
    QCustomPlot *plots[3] = { mRate.plot, mAmp.plot, mBe.plot };
    for (int i = 0; i < 3; ++i) {
        if (mCursors[i]) mCursors[i]->setVisible(false);
        if (plots[i]) plots[i]->replot(QCustomPlot::rpQueuedReplot);
    }
}

double TabLongTermPerformance::sampleAtX(double xSeconds) const
{
    if (mXtoSample.isEmpty()) return 0.0;
    double best = mXtoSample.first().second, bestDx = qAbs(mXtoSample.first().first - xSeconds);
    for (const auto &p : mXtoSample) {
        const double dx = qAbs(p.first - xSeconds);
        if (dx < bestDx) { bestDx = dx; best = p.second; }
    }
    return best;
}

void TabLongTermPerformance::showCursor(double xSeconds)
{
    QCustomPlot *plots[3] = { mRate.plot, mAmp.plot, mBe.plot };
    for (int i = 0; i < 3; ++i) {
        if (!mCursors[i]) continue;
        mCursors[i]->point1->setCoords(xSeconds, 0); mCursors[i]->point2->setCoords(xSeconds, 1);
        mCursors[i]->setVisible(true);
        if (plots[i]) plots[i]->replot(QCustomPlot::rpQueuedReplot);
    }
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
    // X축 10분 고정: 경과<10분이면 [0,10분], 그 이후엔 최근 10분만 보이도록 흘러간다.
    const double lo = (mCurX <= kWindowSec) ? 0.0 : (mCurX - kWindowSec);
    const double hi = lo + kWindowSec;
    // 파형 이력(8분) 경계: 경과가 8분을 넘어 윈도우에 '파형 없는' 오래된 구간이 생길 때만 표시.
    const bool   showLimit = (mCurX > kWaveHistorySec);
    const double limitX = mCurX - kWaveHistorySec;
    QCustomPlot *plots[3] = { mRate.plot, mAmp.plot, mBe.plot };
    for (int i = 0; i < 3; ++i) {
        QCustomPlot *p = plots[i];
        if (!p) continue;
        p->xAxis->setRange(lo, hi);
        p->graph(0)->rescaleValueAxis(false, true);       // 보이는 구간 기준 세로 스케일
        p->yAxis->scaleRange(1.1, p->yAxis->range().center());
        if (mWaveShade[i]) {
            mWaveShade[i]->setVisible(showLimit);
            if (showLimit) {                                   // [lo, 경계] × 전체 높이 음영
                mWaveShade[i]->topLeft->setCoords(lo, 0);
                mWaveShade[i]->bottomRight->setCoords(limitX, 1);
            }
        }
    }
    redrawAnomalies(mRate);
    redrawAnomalies(mAmp);
    redrawAnomalies(mBe);
}

// 이상치 발생 시각들을 배경 세로 음영(빨강 반투명)으로 표시. 마킹은 풀을 재사용한다.
void TabLongTermPerformance::redrawAnomalies(Lane &L)
{
    if (!kShowAnomalyShade) {        // 음영 off — 혹시 남은 마킹 숨기고 종료(검출은 엔진 유지)
        for (QCPItemRect *m : L.anomMarks) if (m) m->setVisible(false);
        return;
    }
    if (!L.plot) return;
    while (L.anomMarks.size() < L.anomX.size() && L.anomMarks.size() < kMaxAnomMarks) {
        auto *r = new QCPItemRect(L.plot);
        r->setPen(Qt::NoPen);
        r->setBrush(QColor(220, 30, 30, 45));               // 반투명 빨강(이상치)
        r->setLayer(QStringLiteral("grid"));                 // 데이터·밴드 뒤(배경)
        r->topLeft->setTypeX(QCPItemPosition::ptPlotCoords);
        r->topLeft->setTypeY(QCPItemPosition::ptAxisRectRatio);
        r->bottomRight->setTypeX(QCPItemPosition::ptPlotCoords);
        r->bottomRight->setTypeY(QCPItemPosition::ptAxisRectRatio);
        L.anomMarks.push_back(r);
    }
    const double half = kAnomBandSec * 0.5;
    for (int i = 0; i < L.anomMarks.size(); ++i) {
        const bool on = (i < L.anomX.size());
        L.anomMarks[i]->setVisible(on);
        if (on) {
            L.anomMarks[i]->topLeft->setCoords(L.anomX[i] - half, 0);
            L.anomMarks[i]->bottomRight->setCoords(L.anomX[i] + half, 1);
        }
    }
}

// 시간축 x(초) — 정책을 한 곳에 모음(2번↔1번 전환은 mXTimeBase 만 바꾸면 됨).
//  · DataTime(2번): 누적샘플/샘플레이트 → Live pause 중 샘플이 안 늘어 '갭' 없이 연속.
//  · WallClock(1번): 벽시계 경과(pause 포함) → onMeasurement 의 선 끊기(NaN)와 함께 사용.
double TabLongTermPerformance::measurementX(const MeasurementSnapshot &s) const
{
    if (mXTimeBase == XTimeBase::DataTime && s.sampleRateHz > 0)
        return (double)s.totalSamples / (double)s.sampleRateHz;
    return (s.timeMs - mT0) / 1000.0;   // WallClock (또는 sr 미상 시 폴백)
}

void TabLongTermPerformance::onMeasurement(const MeasurementSnapshot &s)
{
    mBar->update(s);
    if (!mHaveT0) { mT0 = s.timeMs; mHaveT0 = true; }
    const double x = measurementX(s);
    mCurX = x;
    mXtoSample.push_back({ x, (double)s.totalSamples });   // [③] x(초) → 절대 샘플(클릭→시점)
    while (!mXtoSample.isEmpty() && mXtoSample.first().first < mCurX - kWindowSec) mXtoSample.removeFirst();

    // [이상치] 라인 그래프 음영은 현재 off(kShowAnomalyShade). 토글 on 일 때만 누적(불필요 계산 방지).
    if (kShowAnomalyShade) {
        auto detect = [&](Lane &L, bool valid, bool outlier) {
            const bool on = valid && outlier;
            if (on && !L.prevOut) L.anomX.push_back(x);
            L.prevOut = on;
            while (!L.anomX.isEmpty() && L.anomX.first() < mCurX - kWindowSec) L.anomX.removeFirst();
        };
        detect(mRate, s.rateValid,      s.rateOutlier);
        detect(mAmp,  s.amplitudeValid, s.amplitudeOutlier);
        detect(mBe,   s.beatErrorValid, s.beatErrorOutlier);
    }

    // 데시메이션: 경과(분) 증가 → 점 추가 간격 K 증가(장시간 가독성/효율).
    const long K = 1 + (long)(x / 60.0);
    const bool addPoint = (mTick++ % K == 0);
    if (addPoint) {
        // [1번/WallClock] pause 등으로 x 가 크게 점프하면 직선 연결을 끊는다(NaN). DataTime 에선 갭이 없어 미발동.
        if (mXTimeBase == XTimeBase::WallClock && mHaveLastAdd && (x - mLastAddX) > kPauseGapSec) {
            const double gx = mLastAddX + 1e-3;
            mRate.plot->graph(0)->addData(gx, qQNaN());
            mAmp.plot->graph(0)->addData(gx, qQNaN());
            mBe.plot->graph(0)->addData(gx, qQNaN());
        }
        if (s.rateValid)      { mRate.add(x, s.rate);        mRate.plot->graph(0)->addData(x, s.rate); }
        if (s.amplitudeValid) { mAmp.add(x, s.amplitudeDeg); mAmp.plot->graph(0)->addData(x, s.amplitudeDeg); }
        if (s.beatErrorValid) { mBe.add(x, s.beatErrorMs);   mBe.plot->graph(0)->addData(x, s.beatErrorMs); }
        mHaveLastAdd = true; mLastAddX = x;
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
    applyView();   // 숨은 동안/정지 중 미적용된 축(x 8분 창 + y 스케일)을 적용해 제대로 보이게.
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) if (p) p->replot();
}

void TabLongTermPerformance::onResetSession()
{
    mHaveT0 = false; mTick = 0; mCurX = 0.0;
    mHaveLastAdd = false; mLastAddX = 0.0;
    mXtoSample.clear();
    for (Lane *L : {&mRate, &mAmp, &mBe}) {
        L->sum=0; L->sumSq=0; L->min=0; L->max=0; L->n=0; L->have=false; L->xFirst=L->xLast=0;
        if (L->plot) { L->plot->graph(0)->data()->clear(); L->plot->graph(1)->data()->clear(); }
        if (L->band) { L->band->topLeft->setCoords(0,0); L->band->bottomRight->setCoords(0,0); }
        if (L->stats) L->stats->setText(QString());
        L->prevOut = false; L->anomX.clear();                 // [이상치] 마킹 리셋(검출은 엔진 담당)
        for (QCPItemRect *m : L->anomMarks) if (m) m->setVisible(false);
    }
    if (mBar) mBar->update(MeasurementSnapshot{});
    for (int i = 0; i < 3; ++i) if (mCursors[i]) mCursors[i]->setVisible(false);   // [③] seek 커서 리셋
    for (QCustomPlot *p : {mRate.plot, mAmp.plot, mBe.plot}) if (p) p->replot();
}
