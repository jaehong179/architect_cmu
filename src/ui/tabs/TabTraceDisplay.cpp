#include "TabTraceDisplay.h"
#include "ReadoutBar.h"
#include "LegendBox.h"
#include "PlotHelpers.h"
#include "qcustomplot.h"

TabTraceDisplay::TabTraceDisplay(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this);
    lay->addWidget(mBar);
    // 그래프 읽는 법 설명(Project Plan §Trace: "short explanatory text or labels").
    lay->addWidget(makeLegendBox(QStringLiteral(
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td valign='top'><b>Top rate&nbsp;:</b></td><td>"
        "<font color='#143ca0'><b>bold line=smoothed</b></font> · flat=on-rate · rising=fast · falling=slow (s/d)</td></tr>"
        "<tr><td valign='top'><b>Bottom amplitude&nbsp;:</b></td><td>"
        "<font color='#00a000'>green band 270~300°</font> = normal range (°)</td></tr>"
        "</table>"), this));
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
    onResetSession();
}

void TabTraceDisplay::onMeasurement(const MeasurementSnapshot &s)
{
    mBar->update(s);
    if (!mHaveT0) { mT0 = s.timeMs; mHaveT0 = true; }
    const double x = (s.timeMs - mT0) / 1000.0;

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
                          mAlert->setStyleSheet(QStringLiteral("color:#080; font-weight:bold;")); }
    else { mAlert->setText(warn.join(QStringLiteral("    "))); mAlert->setStyleSheet(QStringLiteral("color:#c00; font-weight:bold;")); }

    if (isVisible()) { mRate->rescaleAxes(); mRate->replot(QCustomPlot::rpQueuedReplot);
                       mAmp->rescaleAxes();
                       // 밴드가 보이도록 y범위에 270~300° 포함.
                       QCPRange yr = mAmp->yAxis->range();
                       mAmp->yAxis->setRange(qMin(yr.lower, kAmpLo - 5.0), qMax(yr.upper, kAmpHi + 5.0));
                       mAmp->replot(QCustomPlot::rpQueuedReplot); }
}

void TabTraceDisplay::onShown()
{
    if (mRate) mRate->replot();
    if (mAmp)  mAmp->replot();
}

void TabTraceDisplay::onResetSession()
{
    mHaveT0 = false; mRateWin.clear(); mRateSum=mAmpSum=0; mRateN=mAmpN=0;
    mDevWin.clear(); mDevSum=0; mDevN=0;
    mAlert->setText(QStringLiteral("Waiting for signal…")); mAlert->setStyleSheet(QStringLiteral("color:#666; font-weight:bold;"));
    if (mDerived) mDerived->setText(QStringLiteral("DiffTicTac=--   DiffPeriod(4s)=--   AvgPeriod=--"));
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mRate) { PlotHelpers::clearAllGraphs(mRate); mRate->replot(); }
    if (mAmp)  { PlotHelpers::clearAllGraphs(mAmp);  mAmp->replot(); }
}
