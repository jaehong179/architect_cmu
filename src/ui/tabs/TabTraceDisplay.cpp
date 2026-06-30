#include "TabTraceDisplay.h"
#include "PlotHelpers.h"
#include "qcustomplot.h"
#include "TrendSeek.h"   // 청록 롤리팝 커서 공용 스타일
#include <QMouseEvent>   // [③] 트렌드 클릭 → 시점 변환

TabTraceDisplay::TabTraceDisplay(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mAlert = new QLabel(this);
    mAlert->setWordWrap(true);
    mAlert->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(mAlert);
    mDerived = new QLabel(this);
    mDerived->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mDerived);

    mRate = new QCustomPlot(this);
    // 보율 그래프: 스냅샷의 스칼라 RlsRate(현재 보율)를 시간축으로 누적해 추세를 그린다.
    //  (문서 §1.3의 xTic/yTic·xToc/yToc 점 표현은 Rate/Scope 탭 것이고, 본 탭은 스냅샷
    //   아키텍처상 스칼라 RlsRate만 게시받아 raw+smoothed 두 선으로 표시 — 의도된 설계.)
    //  색 대비: raw=옅은 회색(배경 추세), smoothed=진한 파랑(FR-TD-2 "진한선=스무딩").
    PlotHelpers::addLineGraph(mRate, QColor(180,180,180));        // raw (옅은 회색)
    PlotHelpers::addLineGraph(mRate, QColor(20,60,160), 2);       // smoothed (진한 파랑)
    mRate->yAxis->setLabel(QStringLiteral("rate s/d"));
    mRate->xAxis->setTickLabels(false);

    mAmp = new QCustomPlot(this);
    PlotHelpers::addLineGraph(mAmp, QColor(150,150,60));
    mAmp->yAxis->setLabel(QStringLiteral("amplitude °"));
    mAmp->xAxis->setLabel(QStringLiteral("time (s)"));
    // 진폭 정상범위(270~300°) 시각 밴드 — Plan: "show whether the watch remains within a normal range".
    mAmpBand = new QCPItemRect(mAmp);
    mAmpBand->setPen(Qt::NoPen);
    mAmpBand->setBrush(QColor(0, 200, 0, 40));
    mAmpBand->topLeft->setCoords(-1e12, kAmpHi);
    mAmpBand->bottomRight->setCoords(1e12, kAmpLo);

    lay->addWidget(mRate, 1);
    lay->addWidget(mAmp, 1);

    // [③] 클릭 지점 롤리팝 커서(청록, 줄기+머리+툴팁) — 선택 시각을 두 그래프에 표시.
    TrendSeek::makeLollipop(mRate, mCurRate, mCurRateHead, mCurRateTip);
    TrendSeek::makeLollipop(mAmp,  mCurAmp,  mCurAmpHead,  mCurAmpTip);

    // [③] 트렌드 클릭 → 커서 표시 + 그 x(초)의 측정 시점(절대 샘플) 방출. 정지 중 스코프 탭이 점프.
    auto onClick = [this](QCustomPlot *pl, QMouseEvent *e) {
        if (mXtoSample.isEmpty()) return;
        const double x = pl->xAxis->pixelToCoord(e->position().x());
        // 커서는 onSeek 왕복(pause 게이트)으로만 표시 → 선택은 정지 중에만.
        emit seekRequested(sampleAtX(x));
    };
    connect(mRate, &QCustomPlot::mousePress, this, [this, onClick](QMouseEvent *e) { onClick(mRate, e); });
    connect(mAmp,  &QCustomPlot::mousePress, this, [this, onClick](QMouseEvent *e) { onClick(mAmp,  e); });

    onResetSession();
}

// [③] 선택한 시각(x초)에 두 그래프 롤리팝 커서를 표시(툴팁=선택 시각).
void TabTraceDisplay::showCursor(double xSeconds)
{
    const QString label = QString("%1 s").arg(xSeconds, 0, 'f', 1);
    TrendSeek::showLollipop(mCurRate, mCurRateHead, mCurRateTip, xSeconds, label);
    TrendSeek::showLollipop(mCurAmp,  mCurAmpHead,  mCurAmpTip,  xSeconds, label);
    if (mRate) mRate->replot(QCustomPlot::rpQueuedReplot);
    if (mAmp)  mAmp->replot(QCustomPlot::rpQueuedReplot);
}

// [③] 다른 탭에서 온 seek(절대 샘플) → 가장 가까운 측정점의 x(초)로 커서를 옮긴다(트렌드 동기화).
void TabTraceDisplay::onSeek(double absSample)
{
    if (mXtoSample.isEmpty()) return;
    double bestX = mXtoSample.first().first, bestErr = qAbs(mXtoSample.first().second - absSample);
    for (const auto &p : mXtoSample) {
        const double err = qAbs(p.second - absSample);
        if (err < bestErr) { bestErr = err; bestX = p.first; }
    }
    showCursor(bestX);   // 롤리팝 툴팁이 선택 시각을 표시(전역 코너 라벨은 제거됨)
}

// 클릭한 x(초)에 가장 가까운 측정점의 절대 샘플 인덱스(totalSamples). (점 수 적어 선형 탐색)
// [③] 선택 해제 — 클릭 커서/라벨을 숨긴다(데이터·축은 유지).
void TabTraceDisplay::onSeekClear()
{
    TrendSeek::hideLollipop(mCurRate, mCurRateHead, mCurRateTip);
    TrendSeek::hideLollipop(mCurAmp,  mCurAmpHead,  mCurAmpTip);
    if (mRate) mRate->replot(QCustomPlot::rpQueuedReplot);
    if (mAmp)  mAmp->replot(QCustomPlot::rpQueuedReplot);
}

double TabTraceDisplay::sampleAtX(double xSeconds) const
{
    if (mXtoSample.isEmpty()) return 0.0;
    double best = mXtoSample.first().second, bestDx = qAbs(mXtoSample.first().first - xSeconds);
    for (const auto &p : mXtoSample) {
        const double dx = qAbs(p.first - xSeconds);
        if (dx < bestDx) { bestDx = dx; best = p.second; }
    }
    return best;
}

void TabTraceDisplay::onMeasurement(const MeasurementSnapshot &s)
{
    if (!mHaveT0) { mT0 = s.timeMs; mHaveT0 = true; }
    const double x = (s.timeMs - mT0) / 1000.0;
    mXtoSample.push_back({ x, (double)s.totalSamples });   // [③] x(초) → 절대 샘플(클릭→시점)

    double smoothed = 0.0; bool haveSmoothed = false;
    if (s.rateValid) {
        mRate->graph(0)->addData(x, s.rate);
        mRateSum += s.rate; ++mRateN;
        mRateWin.push_back(s.rate);
        while (mRateWin.size() > mSmoothN) mRateWin.removeFirst();
        double sum = 0; for (double v : mRateWin) sum += v;
        smoothed = sum / mRateWin.size(); haveSmoothed = true;
        mRate->graph(1)->addData(x, smoothed);
    }
    if (s.amplitudeValid) { mAmp->graph(0)->addData(x, s.amplitudeDeg); mAmpSum += s.amplitudeDeg; ++mAmpN; }

    // 무한 누적 방지: 최근 kHistorySec 구간만 유지(누적 평균/통계는 그대로 유지됨).
    const double cutoff = x - kHistorySec;
    mRate->graph(0)->data()->removeBefore(cutoff);
    mRate->graph(1)->data()->removeBefore(cutoff);
    mAmp->graph(0)->data()->removeBefore(cutoff);
    while (!mXtoSample.isEmpty() && mXtoSample.first().first < cutoff) mXtoSample.removeFirst();

    // [이상치] 라인 음영 현재 off(kShowAnomalyShade). 토글 on 일 때만 누적(불필요 계산 방지).
    if (kShowAnomalyShade) {
        auto detect = [&](bool valid, bool out, QVector<double> &anomX, bool &prev) {
            const bool on = valid && out;
            if (on && !prev) anomX.push_back(x);
            prev = on;
            while (!anomX.isEmpty() && anomX.first() < cutoff) anomX.removeFirst();
        };
        detect(s.rateValid,      s.rateOutlier,      mRateAnomX, mRatePrevOut);
        detect(s.amplitudeValid, s.amplitudeOutlier, mAmpAnomX,  mAmpPrevOut);
    }

    // ── 파생 측정 (Plan §Expected Enhancements / Chour) ─────────────────────
    //  비트당 주기 편차(ms) = rate(s/d) × I_target / 86400 × 1000,  I_target = 3600/BPH (E1).
    //  DiffPeriod = 최근 4초 창 평균,  AvgPeriod = 세션 시작부터 누적 평균,
    //  DiffTicTac = t_tic − t_tac = 2 × beat error (E7: BE = (t1−t2)/2).
    if (s.rateValid && s.bphValid && s.bph > 0) {
        const double iTarget = 3600.0 / s.bph;                       // E1 (s)
        const double devMs = s.rate * iTarget / 86400.0 * 1000.0;
        mDevWin.push_back({x, devMs});
        while (!mDevWin.isEmpty() && x - mDevWin.first().first > kDiffPeriodWinS)
            mDevWin.removeFirst();
        mDevSum += devMs; ++mDevN;
        double winSum = 0; for (const auto &p : mDevWin) winSum += p.second;
        const double diffPeriod = mDevWin.isEmpty() ? 0.0 : winSum / mDevWin.size();
        const double avgPeriod  = mDevN ? mDevSum / mDevN : 0.0;
        const double diffTicTac = s.beatErrorValid ? 2.0 * s.beatErrorMs : 0.0;
        mDerived->setText(QString("DiffTicTac=%1 ms   DiffPeriod(4s)=%2 ms   AvgPeriod=%3 ms   "
                                  "rolling avg=%4 s/d   session avg=%5 s/d")
            .arg(diffTicTac, 0, 'f', 2).arg(diffPeriod, 0, 'f', 3).arg(avgPeriod, 0, 'f', 3)
            .arg(haveSmoothed ? QString::number(smoothed,'f',1) : QStringLiteral("--"))
            .arg(mRateN ? QString::number(mRateSum/mRateN,'f',1) : QStringLiteral("--")));
    }

    QStringList warn;
    if (haveSmoothed && smoothed < kLateSlow)
        warn << QString("⚠ slow (late): %1 s/d").arg(smoothed, 0, 'f', 1);
    if (s.amplitudeValid && (s.amplitudeDeg < kAmpLo || s.amplitudeDeg > kAmpHi))
        warn << QString("⚠ amplitude out of 270~300°: %1°").arg(s.amplitudeDeg, 0, 'f', 0);
    if (warn.isEmpty()) { mAlert->setText(QString("Normal · session mean rate=%1 s/d  amp=%2°")
                              .arg(mRateN?QString::number(mRateSum/mRateN,'f',1):"--")
                              .arg(mAmpN?QString::number(mAmpSum/mAmpN,'f',0):"--"));
                          mAlert->setStyleSheet(QStringLiteral("color:#2ed573; font-weight:bold;")); }
    else { mAlert->setText(warn.join(QStringLiteral("    "))); mAlert->setStyleSheet(QStringLiteral("color:#ff4757; font-weight:bold;")); }

    if (isVisible()) { mRate->rescaleAxes();
                       redrawAnomalies(mRate, mRateAnomX, mRateAnomMarks);
                       mRate->replot(QCustomPlot::rpQueuedReplot);
                       mAmp->rescaleAxes();
                       // 밴드가 보이도록 y범위에 270~300° 포함.
                       QCPRange yr = mAmp->yAxis->range();
                       mAmp->yAxis->setRange(qMin(yr.lower, kAmpLo - 5.0), qMax(yr.upper, kAmpHi + 5.0));
                       redrawAnomalies(mAmp, mAmpAnomX, mAmpAnomMarks);
                       mAmp->replot(QCustomPlot::rpQueuedReplot); }
}

// 이상치 시각들을 배경 세로 음영(빨강 반투명)으로 표시. 풀 재사용. (LongTerm 과 동일 방식)
void TabTraceDisplay::redrawAnomalies(QCustomPlot *plot, QVector<double> &anomX, QVector<QCPItemRect*> &marks)
{
    if (!kShowAnomalyShade) {        // 음영 off — 혹시 남은 마킹 숨기고 종료(검출은 엔진 유지)
        for (QCPItemRect *m : marks) if (m) m->setVisible(false);
        return;
    }
    if (!plot) return;
    while (marks.size() < anomX.size() && marks.size() < kMaxAnomMarks) {
        auto *r = new QCPItemRect(plot);
        r->setPen(Qt::NoPen);
        r->setBrush(QColor(220, 30, 30, 45));
        r->setLayer(QStringLiteral("grid"));
        r->topLeft->setTypeX(QCPItemPosition::ptPlotCoords);
        r->topLeft->setTypeY(QCPItemPosition::ptAxisRectRatio);
        r->bottomRight->setTypeX(QCPItemPosition::ptPlotCoords);
        r->bottomRight->setTypeY(QCPItemPosition::ptAxisRectRatio);
        marks.push_back(r);
    }
    const double half = kAnomBandSec * 0.5;
    for (int i = 0; i < marks.size(); ++i) {
        const bool on = (i < anomX.size());
        marks[i]->setVisible(on);
        if (on) {
            marks[i]->topLeft->setCoords(anomX[i] - half, 0);
            marks[i]->bottomRight->setCoords(anomX[i] + half, 1);
        }
    }
}

void TabTraceDisplay::onShown()
{
    // 숨은 동안 쌓인 데이터(축 미조정)나 정지 상태에서 전환해도 제대로 보이도록 축을 맞춘다.
    //  (onMeasurement 의 rescale 은 isVisible() 가드라, 숨김+정지 조합에선 호출 안 됨.)
    if (mRate) { mRate->rescaleAxes(); redrawAnomalies(mRate, mRateAnomX, mRateAnomMarks); mRate->replot(); }
    if (mAmp)  {
        mAmp->rescaleAxes();
        QCPRange yr = mAmp->yAxis->range();
        mAmp->yAxis->setRange(qMin(yr.lower, kAmpLo - 5.0), qMax(yr.upper, kAmpHi + 5.0));
        redrawAnomalies(mAmp, mAmpAnomX, mAmpAnomMarks);
        mAmp->replot();
    }
}

void TabTraceDisplay::onResetSession()
{
    mHaveT0 = false; mRateWin.clear(); mRateSum=mAmpSum=0; mRateN=mAmpN=0;
    mDevWin.clear(); mDevSum=0; mDevN=0;
    mXtoSample.clear();
    // [이상치] 마킹 리셋
    mRatePrevOut = mAmpPrevOut = false; mRateAnomX.clear(); mAmpAnomX.clear();
    for (QCPItemRect *m : mRateAnomMarks) if (m) m->setVisible(false);
    for (QCPItemRect *m : mAmpAnomMarks)  if (m) m->setVisible(false);
    mAlert->setText(QStringLiteral("Waiting for signal…")); mAlert->setStyleSheet(QStringLiteral("color:#9e9e9e; font-weight:bold;"));
    if (mDerived) mDerived->setText(QStringLiteral("DiffTicTac=--   DiffPeriod(4s)=--   AvgPeriod=--"));
    // [③] 이전 세션의 seek 커서 제거(새 세션 = 시점 표시 리셋).
    TrendSeek::hideLollipop(mCurRate, mCurRateHead, mCurRateTip);
    TrendSeek::hideLollipop(mCurAmp,  mCurAmpHead,  mCurAmpTip);
    if (mRate) { PlotHelpers::clearAllGraphs(mRate); mRate->replot(); }
    if (mAmp)  { PlotHelpers::clearAllGraphs(mAmp);  mAmp->replot(); }
}
