#include "TabTraceDisplay.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"

TabTraceDisplay::TabTraceDisplay(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this);
    lay->addWidget(mBar);
    // 그래프 읽는 법 설명(Project Plan §Trace: "short explanatory text or labels").
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Trace Display</b> — 상단 rate(s/d, 진한선=스무딩): 수평=정시, 상승=빠름, 하강=느림. "
        "하단 amplitude(°): 녹색 밴드 270~300°가 정상 범위. (FR-TD)"), this));
    mAlert = new QLabel(this);
    mAlert->setWordWrap(true);
    mAlert->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(mAlert);
    mDerived = new QLabel(this);
    mDerived->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mDerived);

    mRate = new QCustomPlot(this);
    mRate->addGraph(); mRate->graph(0)->setPen(QPen(QColor(150,150,60)));            // raw (yellow-ish)
    mRate->addGraph(); mRate->graph(1)->setPen(QPen(QColor(120,120,0), 2));          // smoothed
    mRate->yAxis->setLabel(QStringLiteral("rate s/d"));
    mRate->xAxis->setTickLabels(false);

    mAmp = new QCustomPlot(this);
    mAmp->addGraph(); mAmp->graph(0)->setPen(QPen(QColor(150,150,60)));
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
        warn << QString("⚠ 느림(late): %1 s/d").arg(smoothed, 0, 'f', 1);
    if (s.amplitudeValid && (s.amplitudeDeg < kAmpLo || s.amplitudeDeg > kAmpHi))
        warn << QString("⚠ amplitude 270~300° 이탈: %1°").arg(s.amplitudeDeg, 0, 'f', 0);
    if (warn.isEmpty()) { mAlert->setText(QString("정상 · 세션평균 rate=%1 s/d  amp=%2°")
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
    mAlert->setText(QStringLiteral("측정 대기 중…")); mAlert->setStyleSheet(QStringLiteral("color:#666; font-weight:bold;"));
    if (mDerived) mDerived->setText(QStringLiteral("DiffTicTac=--   DiffPeriod(4s)=--   AvgPeriod=--"));
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mRate) { mRate->graph(0)->data()->clear(); mRate->graph(1)->data()->clear(); mRate->replot(); }
    if (mAmp)  { mAmp->graph(0)->data()->clear(); mAmp->replot(); }
}
