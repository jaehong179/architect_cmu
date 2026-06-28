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
    mScope1->addGraph();   // graph(1): 선택 칸 확대 시 tock 평균(파랑) — tick 은 graph(0)
    mScope1->graph(1)->setPen(QPen(QColor(0, 110, 150)));
    mScope1->graph(1)->setBrush(QColor(0, 150, 190, 90));
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

    // ── 하단(Scope 1): 최근 개별 비트 띠 — 클릭 → 그 비트를 Scope 1 에 확대(명세). ──
    mStripBox = new QWidget(this);
    auto *stripLay = new QVBoxLayout(mStripBox); stripLay->setContentsMargins(0, 0, 0, 0);
    stripLay->addWidget(new QLabel(QStringLiteral(
        "Recent beats — the most recent tick/tock beat-noise as small strips; "
        "click one to enlarge it in Scope 1 (click again to return to live)"), mStripBox));
    mStrips = miniPlot(mStripBox, QString(), 90);
    mStrips->xAxis->setTickLabels(false);
    connect(mStrips, &QCustomPlot::mousePress, this, &TabBeatNoiseScope::onStripClicked);
    stripLay->addWidget(mStrips, 1);
    lay->addWidget(mStripBox, 1);

    // ── 하단(Scope 2): 누적평균 셀 8칸(개별 영역) — 한 칸 안에 tick(위)/tock(아래) 정상 적층.
    //  칸 클릭 → Scope 2(trace1=tick 위 / trace2=tock 아래)에 그 칸 평균을 표시. ──
    mCellsBox = new QWidget(this);
    auto *cellsOuter = new QVBoxLayout(mCellsBox); cellsOuter->setContentsMargins(0, 0, 0, 0);
    cellsOuter->addWidget(new QLabel(QStringLiteral(
        "8 cumulative-average cells (separate areas) — each cell = average of the latest 5·N beats "
        "(5,10,…,40); within a cell tick (top) / tock (bottom) are both upright. "
        "Click a cell to show it on Scope 2"), mCellsBox));
    auto *cellsRow = new QWidget(mCellsBox);
    auto *cellsLay = new QHBoxLayout(cellsRow);
    cellsLay->setContentsMargins(0, 0, 0, 0); cellsLay->setSpacing(3);
    mCells.resize(kCells);
    for (int k = 0; k < kCells; ++k) {
        auto *c = new QCustomPlot(cellsRow);
        c->addGraph();   // graph(0) = tick (위, 노랑) — 가운데(0) 바닥으로 채움
        c->graph(0)->setPen(QPen(QColor(120, 110, 0)));
        c->graph(0)->setBrush(QColor(235, 215, 0, 150));
        c->addGraph();   // graph(1) = tock (아래, 파랑) — 정상 방향(미러 없음)
        c->graph(1)->setPen(QPen(QColor(0, 110, 150)));
        c->graph(1)->setBrush(QColor(0, 150, 190, 110));
        c->addGraph();   // graph(2) = tock 바닥선(−H, 비표시) — graph(1) 채널 채움 기준
        c->graph(2)->setPen(Qt::NoPen);
        c->graph(1)->setChannelFillGraph(c->graph(2));
        c->xAxis->setVisible(false);
        c->yAxis->setVisible(false);
        c->axisRect()->setAutoMargins(QCP::msNone);
        c->axisRect()->setMargins(QMargins(0, 0, 0, 0));
        c->setMinimumHeight(120);
        connect(c, &QCustomPlot::mousePress, this, &TabBeatNoiseScope::onCellClicked);
        mCells[k] = c;
        cellsLay->addWidget(c, 1);
    }
    cellsOuter->addWidget(cellsRow, 1);
    lay->addWidget(mCellsBox, 1);
    mCellsBox->setVisible(false);   // 기본 = Scope 1(최근 비트 띠)

    connect(mScopeToggle, &QPushButton::clicked, this, [this]{ mShowScope2 = !mShowScope2; applyScopeView(); });
    connect(mRange, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ mRangeMs = mRange->currentData().toInt(); if (isVisible() && !mShowScope2) renderScope1(); });
    connect(mAvg, &QCheckBox::toggled, this, [this](bool){ if (isVisible() && mShowScope2) renderScope2(); });
}

// Scope1/Scope2 표시 전환 — 하단 영역도 함께 바뀐다(Scope1=최근 비트 띠 / Scope2=누적평균 셀).
void TabBeatNoiseScope::applyScopeView()
{
    if (mScope1Box) mScope1Box->setVisible(!mShowScope2);
    if (mScope2Box) mScope2Box->setVisible(mShowScope2);
    if (mStripBox)  mStripBox->setVisible(!mShowScope2);   // Scope1 하단 = 최근 비트 띠
    if (mCellsBox)  mCellsBox->setVisible(mShowScope2);    // Scope2 하단 = 누적평균 셀
    if (mScopeToggle) mScopeToggle->setText(mShowScope2 ? QStringLiteral("● Scope 2 showing  ▶ click for Scope 1")
                                                        : QStringLiteral("● Scope 1 showing  ▶ click for Scope 2"));
    if (mRange) mRange->setEnabled(!mShowScope2);   // 범위는 Scope1 전용
    mStripsDirty = true;   // 전환 시 새로 보이는 하단 영역 재그리기
    if (isVisible()) render();
}

void TabBeatNoiseScope::onMeasurement(const MeasurementSnapshot &s)
{
    if (s.liftAngle > 0) mLiftAngle = s.liftAngle;
}

void TabBeatNoiseScope::onShown() { mStripsDirty = true; render(); }

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
    mSelBeat = mSelCell = -1;  // 선택 해제 → Scope1 이 mBuf 최신(=seek 비트)을 표시
    mShowScope2 = false;       // Scope1 모드로
    mStripsDirty = true;       // 선택 박스 제거 반영
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

// T3 위치용: 같은 비트 패킷의 A→C 간격(샘플). A 직후 첫 C 까지, 없으면 -1.
double TabBeatNoiseScope::beatAcSamples(uint64_t aSample) const
{
    if (mWin <= 0) return -1.0;
    const QVector<WaveEvent> evs = mBuf.eventsInRange(aSample + 1, aSample + (uint64_t)mWin);
    for (const WaveEvent &e : evs)
        if (e.type == 2) return (double)(e.sample - aSample);   // 첫 C
    return -1.0;
}

void TabBeatNoiseScope::processNewBeats()
{
    if (mWin <= 0 || !mBuf.hasData()) return;
    const uint64_t latest = mBuf.latestAbs();
    if (latest <= (uint64_t)mWin) return;
    const uint64_t scanFrom = mHaveLastBeat ? mLastBeatASample + 1 : mBuf.oldestAbs();
    const QVector<WaveEvent> evs = mBuf.eventsInRange(scanFrom, latest - (uint64_t)mWin + 1);
    bool added = false;                                    // 이번 호출에 새 비트가 누적됐는가
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
        // 누적평균 썸네일용 비트 이력(tick=짝/tock=홀). push 직후 증분 갱신하고 40개로 캡.
        QVector<QVector<double>> &hist = even ? mTickHist : mTockHist;
        QVector<double>          &acH  = even ? mTickAc   : mTockAc;
        hist.push_back(beat);
        acH.push_back(beatAcSamples(e.sample));    // 이 비트의 A→C(샘플, 없으면 -1) — Scope2 T3 위치용
        updateCellWindows(even, beat);             // 슬라이딩 윈도우 증분(새 1개 +, 오래된 1개 −)
        while (hist.size() > kCells * kAvgStep) hist.removeFirst();   // 캡(증분 갱신 후라 안전)
        while (acH.size()  > kCells * kAvgStep) acH.removeFirst();    // A→C 이력도 동일하게 캡
        // Scope 1 하단: 최근 개별 비트 8개(클릭 → 확대) + A 절대 샘플(선택→seek).
        mRecent.push_back(beat);
        mRecentSample.push_back(e.sample);
        while (mRecent.size() > kStrips) { mRecent.removeFirst(); mRecentSample.removeFirst();
                                           if (mSelBeat >= 0) --mSelBeat; }
        if (mSelBeat < -1) mSelBeat = -1;
        mLastBeatASample = e.sample; mHaveLastBeat = true; ++mBeatCount;
        added = true;
    }
    if (added) mStripsDirty = true;   // 새 비트 → 썸네일/선택 표시 갱신
}

void TabBeatNoiseScope::renderScope1()
{
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    if (mScope1->graphCount() > 1) mScope1->graph(1)->data()->clear();   // Scope1 은 단일 트레이스
    // 비트 띠에서 비트가 선택되어 있으면 그 비트를 확대 표시(명세: 이전 비트 선택 확대).
    if (mSelBeat >= 0 && mSelBeat < mRecent.size()) {
        const QVector<double> &beat = mRecent[mSelBeat];
        QVector<double> x(beat.size());
        for (int i = 0; i < beat.size(); ++i) x[i] = 1000.0 * i / sr;
        double inst = 0.0; for (double v : beat) if (v > inst) inst = v;
        const double ymax = smoothPeak(mPeakScope1, inst);
        mScope1->clearItems();
        mScope1->graph(0)->setData(x, beat, true);
        const double span = beat.isEmpty() ? 1.0 : x.last();
        mScope1->xAxis->setRange(-0.04 * span, span);   // A 가 좌측 축에 가리지 않게 여백
        mScope1->yAxis->setRange(0, ymax * 1.1);
        // A(녹색)=비트 시작(x=0), C(빨강)=엔벨로프 피크 + C 시각 라벨 (명세: A/C 식별 + C 시각 표시).
        int pk = 0; for (int i = 1; i < beat.size(); ++i) if (beat[i] > beat[pk]) pk = i;
        auto *aln = new QCPItemLine(mScope1);
        aln->start->setCoords(0, 0); aln->end->setCoords(0, ymax);
        aln->setPen(QPen(QColor(0, 170, 0), 2, Qt::DashLine));
        auto *at = new QCPItemText(mScope1);
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
            .arg(mSelBeat + 1).arg(cMs, 0, 'f', 1).arg(mLiftAngle).arg(mBuf.bph()));
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

// 슬라이딩 윈도우 증분 갱신: 칸 k = 최근 kAvgStep·(k+1)개 평균.
//  방금 hist 끝에 push 된 새 비트(beat)를 윈도우 합에 더하고, 윈도우 밖으로 밀린 1개를 빼서
//  합을 갱신한다(매번 N개를 다시 합산하지 않음). 평균 = 합/N. tick/tock 각자 자기 칸만 갱신.
//  ※ hist 캡(40개)은 호출자가 이 함수 뒤에 수행 → 여기선 hist[M-1-N] 가 항상 유효.
void TabBeatNoiseScope::updateCellWindows(bool tick, const QVector<double> &beat)
{
    if (mWin <= 0) return;
    QVector<QVector<double>> &hist   = tick ? mTickHist   : mTockHist;
    QVector<QVector<double>> &winSum = tick ? mTickWinSum : mTockWinSum;
    QVector<QVector<double>> &cell   = tick ? mCellTick   : mCellTock;
    QVector<int>             &cellN  = tick ? mCellTickN  : mCellTockN;
    if (winSum.size() != kCells) winSum.resize(kCells);
    if (cell.size()   != kCells) cell.resize(kCells);
    if (cellN.size()  != kCells) cellN.resize(kCells);
    const int M = hist.size();                     // push 직후의 보관 비트 수
    for (int k = 0; k < kCells; ++k) {
        const int N = kAvgStep * (k + 1);          // 5,10,15,…,40
        if (winSum[k].size() != mWin) winSum[k].fill(0.0, mWin);
        for (int i = 0; i < mWin && i < beat.size(); ++i) winSum[k][i] += beat[i];   // 새 1개 +
        if (M > N) {                               // 윈도우 밖으로 1개 밀림 → 그 1개 −
            const QVector<double> &rem = hist[M - 1 - N];
            for (int i = 0; i < mWin && i < rem.size(); ++i) winSum[k][i] -= rem[i];
        }
        const int n = qMin(N, M);
        cellN[k] = n;
        if (cell[k].size() != mWin) cell[k].fill(0.0, mWin);
        const double inv = n > 0 ? 1.0 / n : 0.0;
        for (int i = 0; i < mWin; ++i) cell[k][i] = winSum[k][i] * inv;   // 평균 = 합/N
    }
}

// 한 칸(개별 플롯)에 tick=위·tock=아래를 둘 다 정상 방향(미러 없음)으로 적층한다.
//  tick: [0, H] 가운데(0) 바닥으로 채움. tock: [−H, 0] 아래(−H) 바닥으로 채움(graph(2) 채널 채움).
//  selected 면 파란 테두리로 강조. (칸 안의 #N·×N 카운트 라벨은 표시하지 않음.)
static void drawCell(QCustomPlot *p, const QVector<double> &tick, const QVector<double> &tock,
                     int sr, double H, bool selected)
{
    QVector<double> xt(tick.size()), yt(tick.size());                    // tick 위(정상): 바닥 0
    QVector<double> xk(tock.size()), yk(tock.size()), yb(tock.size());   // tock 아래(정상): 바닥 −H
    for (int i = 0; i < tick.size(); ++i) { xt[i] = 1000.0 * i / sr; yt[i] = tick[i]; }
    for (int i = 0; i < tock.size(); ++i) { xk[i] = 1000.0 * i / sr; yk[i] = -H + tock[i]; yb[i] = -H; }
    p->graph(0)->setData(xt, yt, true);    // tick (위)
    p->graph(1)->setData(xk, yk, true);    // tock (아래, 정상 방향)
    p->graph(2)->setData(xk, yb, true);    // tock 바닥선(−H) — 채널 채움 기준
    const int w = qMax(tick.size(), tock.size());
    p->xAxis->setRange(0, w > 1 ? 1000.0 * (w - 1) / sr : 1.0);
    p->yAxis->setRange(-H * 1.14, H * 1.14);   // 0 중심: 위=tick, 아래=tock
    p->clearItems();
    auto *mid = new QCPItemStraightLine(p);     // tick/tock 경계(0) 점선
    mid->point1->setCoords(0, 0); mid->point2->setCoords(1, 0);
    mid->setPen(QPen(QColor(150, 150, 150, 120), 1, Qt::DashLine));
    if (selected) {
        auto *r = new QCPItemRect(p);               // 선택 강조 테두리
        r->topLeft->setType(QCPItemPosition::ptAxisRectRatio);
        r->bottomRight->setType(QCPItemPosition::ptAxisRectRatio);
        r->topLeft->setCoords(0, 0); r->bottomRight->setCoords(1, 1);
        r->setPen(QPen(QColor(0, 80, 220), 2));
        r->setBrush(Qt::NoBrush);
    }
    p->replot(QCustomPlot::rpQueuedReplot);
}

// Scope1 하단: 최근 개별 비트 kStrips 개를 2ms 간격으로 가로 배치. 선택 비트는 파란 박스 강조.
void TabBeatNoiseScope::renderStrips()
{
    if (mRecent.isEmpty() || mWin <= 0) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const double winMs = 1000.0 * mWin / sr;
    const double pitch = winMs + 2.0;                       // 비트별 2ms 간격
    QVector<double> x, y;
    double ymax = 0.0;
    for (int b = 0; b < mRecent.size(); ++b)
        for (int i = 0; i < mRecent[b].size(); ++i) {
            x.push_back(b * pitch + 1000.0 * i / sr);
            const double v = mRecent[b][i]; y.push_back(v);
            if (v > ymax) ymax = v;
        }
    const double top = smoothPeak(mPeakBeats, ymax > 0 ? ymax : 1.0);   // 스무딩 피크(출렁임 억제)
    mStrips->graph(0)->setData(x, y, true);
    mStrips->clearItems();
    if (mSelBeat >= 0 && mSelBeat < mRecent.size()) {       // 선택 비트 강조 박스
        auto *rect = new QCPItemRect(mStrips);
        rect->topLeft->setCoords(mSelBeat * pitch, top * 1.05);
        rect->bottomRight->setCoords(mSelBeat * pitch + winMs, 0);
        rect->setPen(QPen(QColor(0, 80, 220), 2));
        rect->setBrush(Qt::NoBrush);
    }
    mStrips->xAxis->setRange(0, mRecent.size() * pitch);
    mStrips->yAxis->setRange(0, top * 1.1);
    mStrips->replot(QCustomPlot::rpQueuedReplot);
}

// Scope2 하단: 누적평균 셀 8칸. tick/tock 공통 피크로 같은 스케일 비교, 선택 셀 강조.
void TabBeatNoiseScope::renderCells()
{
    if (mWin <= 0 || mCells.isEmpty() || mCellTick.isEmpty()) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    // 모든 칸의 tick/tock 공통 피크 → 같은 스케일로 칸·축 진폭 차이를 직접 비교.
    double inst = 0.0;
    for (const QVector<double> &c : mCellTick) for (double v : c) if (v > inst) inst = v;
    for (const QVector<double> &c : mCellTock) for (double v : c) if (v > inst) inst = v;
    const double top = smoothPeak(mPeakStrips, inst > 0 ? inst : 1.0);   // 스무딩 피크(출렁임 억제)
    for (int k = 0; k < mCells.size(); ++k)
        drawCell(mCells[k], mCellTick.value(k), mCellTock.value(k), sr, top, mSelCell == k);
}

// Scope1 비트 띠 클릭 → 그 비트를 Scope1 에 확대(현재 Scope1 보기 유지). 선택 비트 시점은 seek 전파.
void TabBeatNoiseScope::onStripClicked(QMouseEvent *ev)
{
    if (mRecent.isEmpty() || mWin <= 0) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const double pitch = 1000.0 * mWin / sr + 2.0;
    const double xc = mStrips->xAxis->pixelToCoord(ev->pos().x());
    const int idx = (int)(xc / pitch);
    if (idx < 0 || idx >= mRecent.size()) return;
    mSelBeat = (mSelBeat == idx) ? -1 : idx;               // 재클릭 → 라이브 복귀
    if (mSelBeat >= 0 && mSelBeat < mRecentSample.size())
        emit seekRequested((double)mRecentSample[mSelBeat]);   // 선택 비트 시점 전파(다른 탭 동기화)
    mStripsDirty = true;
    if (isVisible()) render();
}

// Scope2 누적평균 셀 클릭 → 그 칸을 Scope2(위 tick/아래 tock)에 표시(현재 Scope2 보기 유지).
void TabBeatNoiseScope::onCellClicked(QMouseEvent *ev)
{
    Q_UNUSED(ev);
    if (mWin <= 0 || mCellTick.isEmpty()) return;
    auto *plot = qobject_cast<QCustomPlot *>(sender());   // 클릭된 칸 플롯
    const int idx = plot ? mCells.indexOf(plot) : -1;
    if (idx < 0 || idx >= kCells) return;
    if (mCellTick.value(idx).isEmpty() && mCellTock.value(idx).isEmpty()) return;
    mSelCell = (mSelCell == idx) ? -1 : idx;               // 재클릭 → Σ 복귀
    mStripsDirty = true;       // 선택 강조 갱신
    if (isVisible()) render();
}

static void drawTrace(QCustomPlot *p, const QVector<double> &sum, long n,
                      const QVector<double> &last, int sr, bool avgOn,
                      double ampDeg, bool ampValid, double &norm, double t3Ms = -1.0)
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
    // T1(A, x=0)·T3(C, 평균 A→C) 검출 위치 — 빨강 세로선(Escapement 의 T1/T3 표기와 동일).
    auto vmark = [&](double xm, const QString &lab){
        auto *ln = new QCPItemLine(p);
        ln->start->setCoords(xm, 0); ln->end->setCoords(xm, top * 1.1);
        ln->setPen(QPen(QColor(220, 0, 0), 1, Qt::DashLine));
        auto *tl = new QCPItemText(p);
        tl->position->setCoords(xm, top * 1.06);
        tl->setText(lab); tl->setColor(QColor(190, 0, 0));
        tl->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
        tl->setFont(QFont(QStringLiteral("monospace"), 8));
    };
    vmark(0.0, QStringLiteral("T1"));
    if (t3Ms > 0.0 && t3Ms < 1000.0 * w / sr) vmark(t3Ms, QStringLiteral("T3"));
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

// 최근 n 개 A→C 간격(샘플)의 평균(유효 값만). 없으면 -1. → Scope2 T3 위치(평균).
static double acMeanLastN(const QVector<double> &acs, int n)
{
    const int M = acs.size();
    const int start = qMax(0, M - n);
    double s = 0; int cnt = 0;
    for (int i = start; i < M; ++i) if (acs[i] > 0.0) { s += acs[i]; ++cnt; }
    return cnt ? s / cnt : -1.0;
}

void TabBeatNoiseScope::renderScope2()
{
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    const bool avgOn = mAvg->isChecked();
    // 칸이 선택되어 있으면 그 칸의 tick(trace1=위)/tock(trace2=아래) 평균을 Scope 2 영역에 표시.
    if (mSelCell >= 0 && mSelCell < mCellTick.size() &&
        !(mCellTick[mSelCell].isEmpty() && mCellTock[mSelCell].isEmpty())) {
        const int N = kAvgStep * (mSelCell + 1);          // 선택 칸 윈도우 크기
        const double t3a = acMeanLastN(mTickAc, N), t3b = acMeanLastN(mTockAc, N);   // 평균 A→C(샘플)
        drawTrace(mTr1, mCellTick[mSelCell], 1, mCellTick[mSelCell], sr, false, 0, false, mPeakTr1, t3a > 0 ? 1000.0 * t3a / sr : -1.0);
        drawTrace(mTr2, mCellTock[mSelCell], 1, mCellTock[mSelCell], sr, false, 0, false, mPeakTr2, t3b > 0 ? 1000.0 * t3b / sr : -1.0);
        mCycle->setText(QString("[selected cell #%1]  trace1 = tick avg ×%2 (top) · trace2 = tock avg ×%3 (bottom)  —  click the cell again for live Σ")
            .arg(mSelCell + 1).arg(mCellTickN.value(mSelCell)).arg(mCellTockN.value(mSelCell)));
        return;
    }
    // 축별 평균 진폭: 진행 중이면 러닝 평균, 사이클 완료분이 있으면 그 값으로 폴백.
    const double amp1 = mTr1AmpN > 0 ? mTr1AmpSum / mTr1AmpN : mLastCycleAmp1;
    const double amp2 = mTr2AmpN > 0 ? mTr2AmpSum / mTr2AmpN : mLastCycleAmp2;
    const bool a1 = (mTr1AmpN > 0) || mHaveCycleResult;
    const bool a2 = (mTr2AmpN > 0) || mHaveCycleResult;
    // Σ 트레이스 T3: 최근 tick/tock 비트들의 평균 A→C 위치.
    const double t3s1 = acMeanLastN(mTickAc, mTickAc.size());
    const double t3s2 = acMeanLastN(mTockAc, mTockAc.size());
    drawTrace(mTr1, mTr1Sum, mTr1N, mTr1Last, sr, avgOn, amp1, a1, mPeakTr1, t3s1 > 0 ? 1000.0 * t3s1 / sr : -1.0);
    drawTrace(mTr2, mTr2Sum, mTr2N, mTr2Last, sr, avgOn, amp2, a2, mPeakTr2, t3s2 > 0 ? 1000.0 * t3s2 / sr : -1.0);
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
    mTickHist.clear(); mTockHist.clear();
    mTickWinSum.clear(); mTockWinSum.clear();
    mTickAc.clear(); mTockAc.clear();
    mCellTick.clear(); mCellTock.clear(); mCellTickN.clear(); mCellTockN.clear();
    mRecent.clear(); mRecentSample.clear();
    mSelBeat = mSelCell = -1; mStripsDirty = true;
    mPeakScope1 = mPeakStrips = mPeakBeats = mPeakTr1 = mPeakTr2 = 0;
    if (mInfo) mInfo->setText(QStringLiteral("Waiting for signal…"));
    if (mCycle) mCycle->setText(QString("Σ 0/%1 · 0/%1").arg(kCycleN));
    if (mScope1 && mScope1->graphCount() > 1) mScope1->graph(1)->data()->clear();
    for (QCustomPlot *p : {mScope1, mStrips, mTr1, mTr2})
        if (p) { p->graph(0)->data()->clear(); p->clearItems(); p->replot(); }
    for (QCustomPlot *c : mCells)
        if (c) { c->graph(0)->data()->clear(); c->graph(1)->data()->clear(); c->graph(2)->data()->clear(); c->clearItems(); c->replot(); }
}
