#include "TabBeatNoiseScope.h"
#include "ScopeRender.h"
#include "WaveLodHistory.h"   // [③] seek 대상 replay
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <cmath>

static QCustomPlot *miniPlot(QWidget *parent, const QString &yLabel, int minH,
                             const QString &xLabel = QString())
{
    auto *p = new QCustomPlot(parent);
    p->addGraph();
    p->graph(0)->setPen(QPen(QColor(120, 110, 0)));
    p->graph(0)->setBrush(QColor(235, 215, 0, 150));
    p->yAxis->setLabel(yLabel);
    if (!xLabel.isEmpty()) p->xAxis->setLabel(xLabel);
    p->setMinimumHeight(minH);
    return p;
}

// y 스케일 안정화: 스무딩 피크(상승 즉시·하강 천천히) → 매 프레임 max 출렁임 억제.
//  상승은 즉시라 봉우리가 잘리지 않고, 하강만 서서히 줄어 스케일이 안정적이다.
static double smoothPeak(double &norm, double inst)
{
    if (inst > norm) norm = inst; else norm = 0.92 * norm + 0.08 * inst;
    if (norm < 1e-9) norm = 1e-9;
    return norm;
}

TabBeatNoiseScope::TabBeatNoiseScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);

    auto *ctl = new QHBoxLayout();
    mScopeToggle = new QPushButton(QStringLiteral("● Scope 1 showing  ▶ click for Scope 2"), this);   // 현재 모드 + 전환
    ctl->addWidget(mScopeToggle);
    ctl->addWidget(new QLabel(QStringLiteral("Scope1 range:"), this));
    mRange = new QComboBox(this);
    mRange->addItem(QStringLiteral("20 ms"), 20);
    mRange->addItem(QStringLiteral("200 ms"), 200);
    mRange->addItem(QStringLiteral("400 ms"), 400);
    ctl->addWidget(mRange);
    mAvg = new QCheckBox(QStringLiteral("Σ average"), this); mAvg->setChecked(true);
    ctl->addWidget(mAvg);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("Waiting for signal…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    // ── Scope 1 컨테이너(단일 비트 파형) ──
    mScope1Box = new QWidget(this);
    auto *s1lay = new QVBoxLayout(mScope1Box); s1lay->setContentsMargins(0, 0, 0, 0);
    s1lay->addWidget(new QLabel(QStringLiteral("<b>Scope 1</b> — single-beat waveform (A green / C red, lift angle shown)"), mScope1Box));
    mScope1 = new QCustomPlot(mScope1Box);
    mScope1->addGraph();
    mScope1->graph(0)->setPen(QPen(QColor(120, 110, 0)));
    mScope1->graph(0)->setBrush(QColor(235, 215, 0, 150));
    mScope1->xAxis->setLabel(QStringLiteral("time (ms)"));
    mScope1->yAxis->setLabel(QStringLiteral("amplitude (envelope)"));
    s1lay->addWidget(mScope1, 1);
    lay->addWidget(mScope1Box, 3);

    // ── Scope 2 컨테이너(두 수평축 평균 듀얼-트레이스) ──
    // Plan: tic/tac 대응을 단정하지 않고 "두 평균 비트-노이즈 트레이스"로 표기.
    mScope2Box = new QWidget(this);
    auto *s2lay = new QVBoxLayout(mScope2Box); s2lay->setContentsMargins(0, 0, 0, 0);
    s2lay->addWidget(new QLabel(QStringLiteral("<b>Scope 2</b> — average beat-noise of the two beat-groups (tic/tac) (fixed 20 ms · Σ cycle 50+50 → per-axis average amplitude). A healthy watch shows the two traces nearly overlapping; diagnose via the per-axis average-amplitude difference."), mScope2Box));
    mCycle = new QLabel(QStringLiteral("Σ 0/50 · 0/50"), mScope2Box);
    mCycle->setStyleSheet(QStringLiteral("font-family:monospace;"));
    s2lay->addWidget(mCycle);
    // Plan 명시: tic/tac 축 대응을 단정하지 말 것 → 중립적으로 "trace 1/2" 표기.
    mTr1 = miniPlot(mScope2Box, QStringLiteral("average beat-noise ① (trace 1)"), 70, QStringLiteral("time (ms)"));
    mTr2 = miniPlot(mScope2Box, QStringLiteral("average beat-noise ② (trace 2)"), 70, QStringLiteral("time (ms)"));
    s2lay->addWidget(mTr1, 1);
    s2lay->addWidget(mTr2, 1);
    lay->addWidget(mScope2Box, 3);
    mScope2Box->setVisible(false);   // 기본 = Scope 1

    // ③ 하단: 최근 비트 스트립 8개 — Scope1/Scope2 무관 항상 맨 아래.
    lay->addWidget(new QLabel(QStringLiteral("8 recent-beat thumbnails — click one to zoom in Scope 1 (click again to return to live)"), this));
    mStrips = miniPlot(this, QString(), 70);
    mStrips->xAxis->setTickLabels(false);
    connect(mStrips, &QCustomPlot::mousePress, this, &TabBeatNoiseScope::onStripClicked);
    lay->addWidget(mStrips, 1);

    connect(mScopeToggle, &QPushButton::clicked, this, [this]{ mShowScope2 = !mShowScope2; applyScopeView(); });
    connect(mRange, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ mRangeMs = mRange->currentData().toInt(); if (isVisible() && !mShowScope2) renderScope1(); });
    connect(mAvg, &QCheckBox::toggled, this, [this](bool){ if (isVisible() && mShowScope2) renderScope2(); });
}

// Scope1/Scope2 표시 전환(스트립은 항상 하단에 그대로 유지).
void TabBeatNoiseScope::applyScopeView()
{
    if (mScope1Box) mScope1Box->setVisible(!mShowScope2);
    if (mScope2Box) mScope2Box->setVisible(mShowScope2);
    if (mScopeToggle) mScopeToggle->setText(mShowScope2 ? QStringLiteral("● Scope 2 showing  ▶ click for Scope 1")
                                                        : QStringLiteral("● Scope 1 showing  ▶ click for Scope 2"));
    if (mRange) mRange->setEnabled(!mShowScope2);   // 범위는 Scope1 전용
    if (isVisible()) render();
}

void TabBeatNoiseScope::onMeasurement(const MeasurementSnapshot &s)
{
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
    // (정지는 전역 Pause = TabManager 방송 중단이 담당 → 정지 중엔 onWave 자체가 안 옴.
    //  그래서 정지 화면의 스트립·선택이 그대로 유지되고, 그 비트를 골라 확대할 수 있다.)
    processNewBeats();
    if (isVisible()) render();
}

// [③] 다른 탭에서 선택한 시점 → 그 비트를 이력에서 복원해 Scope1에 표시(스트립 선택 해제, 누적 동결).
void TabBeatNoiseScope::onSeek(double absSample)
{
    if (!mHistory || !mHistory->hasData()) return;
    const int sr = mHistory->sampleRate();
    if (sr <= 0) return;
    if (!mConfigured) { mBuf.configure((int)(sr * 0.8)); mWin = (int)(0.020 * sr); mConfigured = true; }
    mBuf.clear();
    WaveLodHistory::replayInto(*mHistory, mBuf, nullptr, absSample, (int)(sr * 0.8));
    mSelectedStrip = -1;       // 스트립 선택 해제 → Scope1 이 mBuf 최신(=seek 비트)을 표시
    mShowScope2 = false;       // Scope1 모드로
    applyScopeView();
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
    const uint64_t scanFrom = mHaveLastBeat ? mLastBeatASample + 1 : mBuf.oldestAbs();
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
        mRecentSample.push_back(e.sample);     // [③] 이 비트의 A 절대 샘플(선택→seek)
        while (mRecent.size() > kStrips) { mRecent.removeFirst(); if (!mRecentSample.isEmpty()) mRecentSample.removeFirst();
                                           if (mSelectedStrip >= 0) --mSelectedStrip; }
        if (mSelectedStrip < -1) mSelectedStrip = -1;
        mLastBeatASample = e.sample; mHaveLastBeat = true; ++mBeatCount;
    }
}

void TabBeatNoiseScope::renderScope1()
{
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    // 스트립이 선택되어 있으면 그 비트를 확대 표시(Plan: "select one of these prior beats for enlarged viewing").
    if (mSelectedStrip >= 0 && mSelectedStrip < mRecent.size()) {
        const QVector<double> &beat = mRecent[mSelectedStrip];
        QVector<double> x(beat.size());
        for (int i = 0; i < beat.size(); ++i) x[i] = 1000.0 * i / sr;
        double inst = 0.0; for (double v : beat) if (v > inst) inst = v;
        const double ymax = smoothPeak(mPeakScope1, inst);   // 스무딩 피크(출렁임 억제)
        mScope1->clearItems();
        mScope1->graph(0)->setData(x, beat, true);
        // A 는 비트 시작(x=0) → 좌측 축에 가려지지 않도록 약간의 좌측 여백을 둔다.
        const double span = beat.isEmpty() ? 1.0 : x.last();
        mScope1->xAxis->setRange(-0.04 * span, span);
        mScope1->yAxis->setRange(0, ymax * 1.1);
        // A(녹색)=비트 시작(index 0), C(빨강)=엔벨로프 피크 + C 시각 라벨 (라이브 뷰와 동일 마커).
        int pk = 0; for (int i = 1; i < beat.size(); ++i) if (beat[i] > beat[pk]) pk = i;
        auto *aln = new QCPItemLine(mScope1);
        aln->start->setCoords(0, 0); aln->end->setCoords(0, ymax);
        aln->setPen(QPen(QColor(0, 170, 0), 2, Qt::DashLine));
        auto *at = new QCPItemText(mScope1);   // A 라벨(녹색) — C 와 대칭으로 명시.
        at->position->setCoords(0, ymax * 0.98);
        at->setText(QStringLiteral("A 0.0 ms"));
        at->setColor(QColor(0, 140, 0));
        at->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
        const double cMs = 1000.0 * pk / sr;
        auto *cln = new QCPItemLine(mScope1);
        cln->start->setCoords(cMs, 0); cln->end->setCoords(cMs, ymax);
        cln->setPen(QPen(QColor(220, 0, 0), 1, Qt::DashLine));
        auto *ct = new QCPItemText(mScope1);
        ct->position->setCoords(cMs, ymax * 0.9);
        ct->setText(QString("C %1 ms").arg(cMs, 0, 'f', 1));
        ct->setColor(Qt::black);
        ct->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        mInfo->setText(QString("[selected beat #%1]  A→C=%2ms  lift=%3°  bph=%4")
            .arg(mSelectedStrip + 1).arg(cMs, 0, 'f', 1).arg(mLiftAngle).arg(mBuf.bph()));
        mScope1->replot(QCustomPlot::rpQueuedReplot);
        return;
    }
    if (!mBuf.hasData()) return;
    const int count = (int)(mRangeMs / 1000.0 * sr);
    const int pre   = (int)(0.002 * sr);                 // 2ms preroll
    const uint64_t latest = mBuf.latestAbs();
    const uint64_t need   = (uint64_t)(count > pre ? count - pre : count);  // A 이후 필요한 샘플 수
    // "가장 최근 A"가 아니라 '전체 윈도우가 버퍼에 다 들어온 마지막 완성 비트'를 고른다.
    // (진행 중 비트를 잡으면 A 뒤 데이터가 비어 노이즈 플로어로 폴백 → 비트마다 깜빡임 발생.)
    uint64_t lastA = 0; bool haveA = false;
    if (latest > need) {
        const QVector<WaveEvent> evs = mBuf.eventsInRange(mBuf.oldestAbs(), latest - need);
        for (int i = evs.size() - 1; i >= 0; --i)
            if (evs[i].type == 1) { lastA = evs[i].sample; haveA = true; break; }
    }
    uint64_t from;
    if (haveA) from = lastA > (uint64_t)pre ? lastA - (uint64_t)pre : 0;
    else       from = latest > (uint64_t)count ? latest - count : 0;   // 아직 완성 비트 없음 → 최근 구간
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
    const double top = smoothPeak(mPeakStrips, ymax > 0 ? ymax : 1.0);   // 스무딩 피크(출렁임 억제)
    mStrips->graph(0)->setData(x, y, true);
    // 선택 스트립 강조 박스.
    mStrips->clearItems();
    if (mSelectedStrip >= 0 && mSelectedStrip < mRecent.size()) {
        auto *rect = new QCPItemRect(mStrips);
        rect->topLeft->setCoords(mSelectedStrip * (winMs + 2.0), top * 1.05);
        rect->bottomRight->setCoords(mSelectedStrip * (winMs + 2.0) + winMs, 0);
        rect->setPen(QPen(QColor(0, 80, 220), 2));
        rect->setBrush(Qt::NoBrush);
    }
    mStrips->xAxis->setRange(0, mRecent.size() * (winMs + 2.0));
    mStrips->yAxis->setRange(0, top * 1.1);
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
    mSelectedStrip = (mSelectedStrip == idx) ? -1 : idx;   // 재클릭 → 라이브 복귀
    if (mSelectedStrip >= 0 && mSelectedStrip < mRecentSample.size())
        emit seekRequested((double)mRecentSample[mSelectedStrip]);   // [③] 선택 비트 시점 전파
    // 선택 비트 확대는 Scope1 에서 — Scope2 보기였다면 Scope1 으로 전환.
    mShowScope2 = false;
    applyScopeView();          // render(Scope1) + renderStrips() 수행
}

static void drawTrace(QCustomPlot *p, const QVector<double> &sum, long n,
                      const QVector<double> &last, int sr, bool avgOn,
                      double ampDeg, bool ampValid, double &norm)
{
    // Σ ON: 정렬 코히어런트 평균(잡음 √N 감소). Σ OFF: 최신 단일 비트.
    const QVector<double> &src = avgOn ? sum : last;
    const double div = (avgOn && n > 0) ? (double)n : 1.0;
    p->clearItems();
    if (src.isEmpty()) { p->graph(0)->data()->clear(); p->replot(); return; }
    const int w = src.size();
    QVector<double> x(w), y(w);
    double ymax = 0.0;
    for (int i = 0; i < w; ++i) { x[i] = 1000.0 * i / sr; y[i] = src[i] / div; if (y[i] > ymax) ymax = y[i]; }
    p->graph(0)->setData(x, y, true);
    const double top = smoothPeak(norm, ymax > 0 ? ymax : 1.0);   // 스무딩 피크(출렁임 억제)
    p->xAxis->setRange(0, 1000.0 * w / sr);
    p->yAxis->setRange(0, top * 1.1);
    // Plan: "display the average amplitude on each horizontal axis" — 축별 평균 진폭 라벨.
    if (ampValid) {
        auto *t = new QCPItemText(p);
        t->position->setType(QCPItemPosition::ptAxisRectRatio);
        t->position->setCoords(0.985, 0.07);
        t->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
        t->setText(QString("average amplitude ≈ %1°").arg(ampDeg, 0, 'f', 0));
        t->setColor(QColor(170, 70, 0));
        t->setBrush(QColor(255, 255, 255, 200));
        t->setPen(QPen(QColor(170, 70, 0, 120)));
        t->setPadding(QMargins(4, 1, 4, 1));
        t->setFont(QFont(QStringLiteral("monospace"), 8));
    }
    p->replot(QCustomPlot::rpQueuedReplot);
}

void TabBeatNoiseScope::renderScope2()
{
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const bool avgOn = mAvg->isChecked();
    // 축별 평균 진폭: 진행 중이면 러닝 평균, 사이클 완료분이 있으면 그 값으로 폴백.
    const double amp1 = mTr1AmpN > 0 ? mTr1AmpSum / mTr1AmpN : mLastCycleAmp1;
    const double amp2 = mTr2AmpN > 0 ? mTr2AmpSum / mTr2AmpN : mLastCycleAmp2;
    const bool a1 = (mTr1AmpN > 0) || mHaveCycleResult;
    const bool a2 = (mTr2AmpN > 0) || mHaveCycleResult;
    drawTrace(mTr1, mTr1Sum, mTr1N, mTr1Last, sr, avgOn, amp1, a1, mPeakTr1);
    drawTrace(mTr2, mTr2Sum, mTr2N, mTr2Last, sr, avgOn, amp2, a2, mPeakTr2);
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
        t += QString("    [cycle complete] trace1 X̄=%1°  trace2 X̄=%2°")
                 .arg(mLastCycleAmp1, 0, 'f', 0).arg(mLastCycleAmp2, 0, 'f', 0);
    mCycle->setText(t);
}

void TabBeatNoiseScope::onResetSession()
{
    mBuf.clear(); mConfigured = false;
    mLastBeatASample = 0; mHaveLastBeat = false; mBeatCount = 0; mWin = 0;
    mTr1Sum.clear(); mTr2Sum.clear(); mTr1N = mTr2N = 0;
    mTr1Last.clear(); mTr2Last.clear();
    mTr1AmpSum = mTr2AmpSum = 0; mTr1AmpN = mTr2AmpN = 0;
    mHaveCycleResult = false; mLastCycleAmp1 = mLastCycleAmp2 = 0;
    mRecent.clear(); mRecentSample.clear(); mSelectedStrip = -1;
    mPeakScope1 = mPeakStrips = mPeakTr1 = mPeakTr2 = 0;
    if (mInfo) mInfo->setText(QStringLiteral("Waiting for signal…"));
    if (mCycle) mCycle->setText(QString("Σ 0/%1 · 0/%1").arg(kCycleN));
    for (QCustomPlot *p : {mScope1, mStrips, mTr1, mTr2})
        if (p) { p->graph(0)->data()->clear(); p->clearItems(); p->replot(); }
}
