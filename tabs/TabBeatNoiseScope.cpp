#include "TabBeatNoiseScope.h"
#include "ReadoutBar.h"
#include "ScopeRender.h"
#include <QComboBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <cmath>

static QCustomPlot *miniPlot(QWidget *parent, const QString &yLabel, int minH)
{
    auto *p = new QCustomPlot(parent);
    p->addGraph();
    p->graph(0)->setPen(QPen(QColor(120, 110, 0)));
    p->graph(0)->setBrush(QColor(235, 215, 0, 150));
    p->yAxis->setLabel(yLabel);
    p->setMinimumHeight(minH);
    return p;
}

TabBeatNoiseScope::TabBeatNoiseScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("Scope1 범위:"), this));
    mRange = new QComboBox(this);
    mRange->addItem(QStringLiteral("20 ms"), 20);
    mRange->addItem(QStringLiteral("200 ms"), 200);
    mRange->addItem(QStringLiteral("400 ms"), 400);
    ctl->addWidget(mRange);
    mAvg = new QCheckBox(QStringLiteral("Σ 평균"), this); mAvg->setChecked(true);
    ctl->addWidget(mAvg);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    lay->addWidget(new QLabel(QStringLiteral("<b>Scope 1</b> — 단일 비트 파형 (A 녹색 / C 빨강, lift angle 표시)"), this));
    mScope1 = new QCustomPlot(this);
    mScope1->addGraph();
    mScope1->graph(0)->setPen(QPen(QColor(120, 110, 0)));
    mScope1->graph(0)->setBrush(QColor(235, 215, 0, 150));
    mScope1->xAxis->setLabel(QStringLiteral("time (ms)"));
    lay->addWidget(mScope1, 3);

    // ② 듀얼-트레이스 평균(똑/딱 두 축) — 메인 바로 아래(사양 레이아웃 순서).
    // Plan: tic/tac 대응을 단정하지 않고 "두 평균 비트-노이즈 트레이스"로 표기.
    lay->addWidget(new QLabel(QStringLiteral("<b>Scope 2</b> — 두 수평축 평균 듀얼-트레이스 (고정 20 ms, Σ 사이클 = 50+50 간격)"), this));
    mCycle = new QLabel(QStringLiteral("Σ 0/50 · 0/50"), this);
    mCycle->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mCycle);
    mTr1 = miniPlot(this, QStringLiteral("평균 ① (똑/tic)"), 70);
    mTr2 = miniPlot(this, QStringLiteral("평균 ② (딱/toc)"), 70);
    lay->addWidget(mTr1, 1);
    lay->addWidget(mTr2, 1);

    // ③ 하단: 최근 비트 스트립(클릭 → 확대) — 화면 맨 아래(사양).
    lay->addWidget(new QLabel(QStringLiteral("최근 비트 스트립 (클릭 → Scope1 확대, 재클릭 → 라이브) →"), this));
    mStrips = miniPlot(this, QString(), 70);
    mStrips->xAxis->setTickLabels(false);
    connect(mStrips, &QCustomPlot::mousePress, this, &TabBeatNoiseScope::onStripClicked);
    lay->addWidget(mStrips, 1);

    connect(mRange, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ mRangeMs = mRange->currentData().toInt(); if (isVisible()) renderScope1(); });
    connect(mAvg, &QCheckBox::toggled, this, [this](bool){ if (isVisible()) renderScope2(); });
}

void TabBeatNoiseScope::onMeasurement(const MeasurementSnapshot &s)
{
    if (mBar) mBar->update(s);
    if (s.liftAngle > 0) mLiftAngle = s.liftAngle;
}

void TabBeatNoiseScope::onShown() { render(); }

void TabBeatNoiseScope::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        mBuf.configure((int)(w.sampleRateHz * 0.8));
        mWin = (int)(0.020 * w.sampleRateHz);     // 20ms 평균 윈도우
        mConfigured = true;
    }
    mBuf.push(w);
    processNewBeats();
    if (isVisible()) render();
}

// E8 (Equations_v0 Part IV): Amp = 3600·λ / (π·n·t_AC), t_AC = 같은 비트 패킷의 A→C 간격(s).
double TabBeatNoiseScope::beatAmplitudeDeg(uint64_t aSample) const
{
    const int sr = mBuf.sampleRate();
    const int bph = mBuf.bph();
    if (sr <= 0 || bph <= 0 || mWin <= 0) return -1.0;
    const QVector<WaveEvent> evs = mBuf.eventsInRange(aSample + 1, aSample + (uint64_t)mWin);
    for (const WaveEvent &e : evs) {
        if (e.type != 2) continue;                       // C 이벤트
        const double tAC = (double)(e.sample - aSample) / sr;
        if (tAC <= 0.0) return -1.0;
        const double amp = (3600.0 * mLiftAngle) / (M_PI * bph * tAC);
        return amp < 360.0 ? amp : -1.0;                 // 360° 이상은 노킹 → 기각
    }
    return -1.0;
}

void TabBeatNoiseScope::processNewBeats()
{
    if (mWin <= 0 || !mBuf.hasData()) return;
    const uint64_t latest = mBuf.latestAbs();
    if (latest <= (uint64_t)mWin) return;
    const uint64_t scanFrom = mHaveLastBeat ? mLastBeatA + 1 : mBuf.oldestAbs();
    const QVector<WaveEvent> evs = mBuf.eventsInRange(scanFrom, latest - (uint64_t)mWin + 1);
    for (const WaveEvent &e : evs) {
        if (e.type != 1) continue;                 // A 이벤트(비트 시작)
        QVector<double> beat; mBuf.copyRange(e.sample, mWin, beat);
        const bool even = (mBeatCount % 2 == 0);
        QVector<double> &sum  = even ? mTr1Sum  : mTr2Sum;
        QVector<double> &last = even ? mTr1Last : mTr2Last;
        long   &cnt    = even ? mTr1N      : mTr2N;
        double &ampSum = even ? mTr1AmpSum : mTr2AmpSum;
        long   &ampN   = even ? mTr1AmpN   : mTr2AmpN;
        last = beat;
        // Σ 사이클: 50 tic + 50 tac 간격 완료 시 누적 정지(축별 평균 진폭 확정 후 새 사이클).
        if (cnt < kCycleN) {
            if (sum.size() != mWin) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
                sum.assign(mWin, 0.0);
#else
                sum.fill(0.0, mWin);
#endif
                cnt = 0;
            }
            for (int i = 0; i < mWin; ++i) sum[i] += beat[i];
            ++cnt;
            const double amp = beatAmplitudeDeg(e.sample);
            if (amp > 0.0) { ampSum += amp; ++ampN; }
        }
        // 사이클 완료(50+50): 축별 평균 진폭 확정 후 다음 사이클을 새로 시작.
        if (mTr1N >= kCycleN && mTr2N >= kCycleN) {
            mLastCycleAmp1 = mTr1AmpN ? mTr1AmpSum / mTr1AmpN : 0.0;
            mLastCycleAmp2 = mTr2AmpN ? mTr2AmpSum / mTr2AmpN : 0.0;
            mHaveCycleResult = true;
            mTr1Sum.clear(); mTr2Sum.clear(); mTr1N = mTr2N = 0;
            mTr1AmpSum = mTr2AmpSum = 0; mTr1AmpN = mTr2AmpN = 0;
        }
        // 스트립 링
        mRecent.push_back(beat);
        while (mRecent.size() > kStrips) { mRecent.removeFirst(); if (mSelStrip >= 0) --mSelStrip; }
        if (mSelStrip < -1) mSelStrip = -1;
        mLastBeatA = e.sample; mHaveLastBeat = true; ++mBeatCount;
    }
}

void TabBeatNoiseScope::renderScope1()
{
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    // 스트립이 선택되어 있으면 그 비트를 확대 표시(Plan: "select one of these prior beats for enlarged viewing").
    if (mSelStrip >= 0 && mSelStrip < mRecent.size()) {
        const QVector<double> &beat = mRecent[mSelStrip];
        QVector<double> x(beat.size());
        for (int i = 0; i < beat.size(); ++i) x[i] = 1000.0 * i / sr;
        double ymax = 0.0; for (double v : beat) if (v > ymax) ymax = v;
        if (ymax <= 0.0) ymax = 1.0;
        mScope1->clearItems();
        mScope1->graph(0)->setData(x, beat, true);
        mScope1->xAxis->setRange(0, beat.isEmpty() ? 1.0 : x.last());
        mScope1->yAxis->setRange(0, ymax * 1.1);
        // A(녹색)=비트 시작(index 0), C(빨강)=엔벨로프 피크 + C 시각 라벨 (라이브 뷰와 동일 마커).
        int pk = 0; for (int i = 1; i < beat.size(); ++i) if (beat[i] > beat[pk]) pk = i;
        auto *aln = new QCPItemLine(mScope1);
        aln->start->setCoords(0, 0); aln->end->setCoords(0, ymax);
        aln->setPen(QPen(QColor(0, 170, 0), 1, Qt::DashLine));
        const double cMs = 1000.0 * pk / sr;
        auto *cln = new QCPItemLine(mScope1);
        cln->start->setCoords(cMs, 0); cln->end->setCoords(cMs, ymax);
        cln->setPen(QPen(QColor(220, 0, 0), 1, Qt::DashLine));
        auto *ct = new QCPItemText(mScope1);
        ct->position->setCoords(cMs, ymax * 0.9);
        ct->setText(QString("C %1 ms").arg(cMs, 0, 'f', 1));
        ct->setColor(Qt::black);
        ct->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        mInfo->setText(QString("[선택 비트 #%1]  A→C=%2ms  lift=%3°  bph=%4")
            .arg(mSelStrip + 1).arg(cMs, 0, 'f', 1).arg(mLiftAngle).arg(mBuf.bph()));
        mScope1->replot(QCustomPlot::rpQueuedReplot);
        return;
    }
    if (!mBuf.hasData()) return;
    const int count = (int)(mRangeMs / 1000.0 * sr);
    uint64_t lastA, from;
    if (mBuf.latestEvent(1, lastA)) {
        const int pre = (int)(0.002 * sr);
        from = lastA > (uint64_t)pre ? lastA - pre : 0;
        if (from + (uint64_t)count > mBuf.latestAbs() && mBuf.latestAbs() > (uint64_t)count)
            from = mBuf.latestAbs() - count;
    } else from = mBuf.latestAbs() > (uint64_t)count ? mBuf.latestAbs() - count : 0;
    scopePlotWindow(mScope1, mBuf, from, count);
    // Plan §Scope 1: "shall present the lift angle associated with the displayed beat pattern".
    mInfo->setText(QString("range=%1ms  lift=%2°  beats=%3  bph=%4 %5")
        .arg(mRangeMs).arg(mLiftAngle).arg(mBeatCount).arg(mBuf.bph()).arg(mBuf.synced()?"[synced]":""));
    mScope1->replot(QCustomPlot::rpQueuedReplot);
}

void TabBeatNoiseScope::renderStrips()
{
    if (mRecent.isEmpty() || mWin <= 0) { return; }
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const double winMs = 1000.0 * mWin / sr;
    QVector<double> x, y;
    double ymax = 0.0;
    for (int b = 0; b < mRecent.size(); ++b) {
        for (int i = 0; i < mRecent[b].size(); ++i) {
            x.push_back(b * (winMs + 2.0) + 1000.0 * i / sr);   // 비트별로 2ms 간격 띄움
            const double v = mRecent[b][i]; y.push_back(v);
            if (v > ymax) ymax = v;
        }
    }
    mStrips->graph(0)->setData(x, y, true);
    // 선택 스트립 강조 박스.
    mStrips->clearItems();
    if (mSelStrip >= 0 && mSelStrip < mRecent.size()) {
        auto *rect = new QCPItemRect(mStrips);
        rect->topLeft->setCoords(mSelStrip * (winMs + 2.0), (ymax > 0 ? ymax : 1.0) * 1.05);
        rect->bottomRight->setCoords(mSelStrip * (winMs + 2.0) + winMs, 0);
        rect->setPen(QPen(QColor(0, 80, 220), 2));
        rect->setBrush(Qt::NoBrush);
    }
    mStrips->xAxis->setRange(0, mRecent.size() * (winMs + 2.0));
    mStrips->yAxis->setRange(0, (ymax > 0 ? ymax : 1.0) * 1.1);
    mStrips->replot(QCustomPlot::rpQueuedReplot);
}

void TabBeatNoiseScope::onStripClicked(QMouseEvent *ev)
{
    if (mRecent.isEmpty() || mWin <= 0) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const double winMs = 1000.0 * mWin / sr;
    const double xc = mStrips->xAxis->pixelToCoord(ev->pos().x());
    const int idx = (int)(xc / (winMs + 2.0));
    if (idx < 0 || idx >= mRecent.size()) return;
    mSelStrip = (mSelStrip == idx) ? -1 : idx;   // 재클릭 → 라이브 복귀
    renderScope1();
    renderStrips();
}

static void drawTrace(QCustomPlot *p, const QVector<double> &sum, long n,
                      const QVector<double> &last, int sr, bool avgOn)
{
    // Σ ON: 정렬 코히어런트 평균(잡음 √N 감소). Σ OFF: 최신 단일 비트.
    const QVector<double> &src = avgOn ? sum : last;
    const double div = (avgOn && n > 0) ? (double)n : 1.0;
    if (src.isEmpty()) { p->graph(0)->data()->clear(); p->replot(); return; }
    const int w = src.size();
    QVector<double> x(w), y(w);
    double ymax = 0.0;
    for (int i = 0; i < w; ++i) { x[i] = 1000.0 * i / sr; y[i] = src[i] / div; if (y[i] > ymax) ymax = y[i]; }
    p->graph(0)->setData(x, y, true);
    p->xAxis->setRange(0, 1000.0 * w / sr);
    p->yAxis->setRange(0, (ymax > 0 ? ymax : 1.0) * 1.1);
    p->replot(QCustomPlot::rpQueuedReplot);
}

void TabBeatNoiseScope::renderScope2()
{
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const bool avgOn = mAvg->isChecked();
    drawTrace(mTr1, mTr1Sum, mTr1N, mTr1Last, sr, avgOn);
    drawTrace(mTr2, mTr2Sum, mTr2N, mTr2Last, sr, avgOn);
    // 사이클 진행(중간 결과 10·20 간격 포함) / 완료 시 축별 평균 진폭(Witschi "50  X 289°" 표기).
    QString t = QString("Σ %1/%2 · %3/%4").arg(mTr1N).arg(kCycleN).arg(mTr2N).arg(kCycleN);
    auto interim = [&](long n, double ampSum, long ampN) -> QString {
        if (n >= 10 && n < kCycleN && ampN > 0)
            return QString("  (~%1° @%2)").arg(ampSum / ampN, 0, 'f', 0).arg(n >= 20 ? 20 : 10);
        return QString();
    };
    t += interim(mTr1N, mTr1AmpSum, mTr1AmpN);
    t += interim(mTr2N, mTr2AmpSum, mTr2AmpN);
    if (mHaveCycleResult)
        t += QString("    [사이클 완료] trace1 X̄=%1°  trace2 X̄=%2°")
                 .arg(mLastCycleAmp1, 0, 'f', 0).arg(mLastCycleAmp2, 0, 'f', 0);
    mCycle->setText(t);
}

void TabBeatNoiseScope::onResetSession()
{
    mBuf.clear(); mConfigured = false;
    mLastBeatA = 0; mHaveLastBeat = false; mBeatCount = 0; mWin = 0;
    mTr1Sum.clear(); mTr2Sum.clear(); mTr1N = mTr2N = 0;
    mTr1Last.clear(); mTr2Last.clear();
    mTr1AmpSum = mTr2AmpSum = 0; mTr1AmpN = mTr2AmpN = 0;
    mHaveCycleResult = false; mLastCycleAmp1 = mLastCycleAmp2 = 0;
    mRecent.clear(); mSelStrip = -1;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mCycle) mCycle->setText(QString("Σ 0/%1 · 0/%1").arg(kCycleN));
    for (QCustomPlot *p : {mScope1, mStrips, mTr1, mTr2})
        if (p) { p->graph(0)->data()->clear(); p->clearItems(); p->replot(); }
}
