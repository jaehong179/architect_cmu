#include "TabSyncSweepScope.h"
#include "LegendBox.h"
#include "WaveLodHistory.h"   // [③] seek replay
#include "qcustomplot.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

// ── Scope Mode with Synchronized Sweep (FR-SMS) — scope_mode.c 구도 ──
//  프리러닝 스윕 창(틱 주기 × NBEATS)에 folded 신호 |raw-avg| 를 바닥에서 위로 채워(그래스)
//  그린다. 정시=패턴 정지, 빠름/느림=드리프트. 필터 비교는 'Multiple Filter Views' 탭.

static const QColor kScopeMagenta(199, 125, 191);     // scope_mode.c 마젠타(0.78,0.49,0.75)

TabSyncSweepScope::TabSyncSweepScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(makeLegendBox(QStringLiteral(
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td valign='top'><b>Display&nbsp;:</b></td><td>folded "
        "<font color='#c77dbf'>|signal−mean|</font>(rectified, positive·negative halves merged) grass filled from baseline · "
        "y-axis=amplitude(envelope, normalized)</td></tr>"
        "<tr><td valign='top'><b>Sweep&nbsp;:</b></td><td>window width = tick period × N beats(free-running)</td></tr>"
        "<tr><td valign='top'><b>Behavior&nbsp;:</b></td><td>on-rate=pattern frozen · fast/slow=horizontal drift</td></tr>"
        "</table>"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("sweep(beats):"), this));
    mBeats = new QSpinBox(this); mBeats->setRange(2, 8); mBeats->setValue(6);   // NBEATS 기본 6
    ctl->addWidget(mBeats);
    ctl->addStretch(1);   // (정지는 전역 Pause 버튼이 담당)
    mInfo = new QLabel(QStringLiteral("Waiting for signal…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    mPlot = new QCustomPlot(this);
    mPlot->addGraph();
    mPlot->graph(0)->setPen(QPen(kScopeMagenta, 1.0));
    mPlot->graph(0)->setBrush(QColor(199, 125, 191, 150));   // 바닥(0)까지 채움 → 그래스
    mPlot->graph(0)->setAdaptiveSampling(true);
    mPlot->xAxis->setLabel(QStringLiteral("sweep time (ms)"));
    mPlot->yAxis->setLabel(QStringLiteral("amplitude (envelope, normalized 0~1)"));
    lay->addWidget(mPlot, 1);

    connect(mBeats, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ if (isVisible()) render(); });
}

void TabSyncSweepScope::onMeasurement(const MeasurementSnapshot &s)
{
    Q_UNUSED(s);
}

void TabSyncSweepScope::onShown() { render(); }

void TabSyncSweepScope::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        // 표시 창은 '마지막 완전한 스윕'이라 현재보다 최대 2 스윕 뒤처진다 → 버퍼는 그 창이 고정돼 있는
        //  동안(추가 1 스윕) 꼬리에 안 잡아먹히게 ≥ 2×최대스윕 + 여유가 필요. 최대 8비트(≤18000BPH 기준
        //  ≈1.6s)·2배 = 3.2s → 4s 로 잡아 좌측 데이터 소실(깜빡임)을 막는다.
        const int cap = (int)(w.sampleRateHz * 4.0);
        mBuf.configure(cap);
        mRawBuf.configure(cap);
        mConfigured = true;
    }
    mBuf.push(w);                                      // 엔벨로프 + 이벤트(마커/동기)
    if (w.raw && w.rawN > 0) {                          // 원신호 → mRawBuf (표시)
        WaveBlock rawBlock;
        rawBlock.env = w.raw; rawBlock.n = w.rawN; rawBlock.startSample = w.rawStart;
        rawBlock.sampleRateHz = w.sampleRateHz; rawBlock.bph = w.bph; rawBlock.synced = w.synced;
        mRawBuf.push(rawBlock);
    }
    // 프리러닝 스윕 앵커: 동기 후 첫 A 이벤트에서 1회만 고정(이후 재정렬 금지 → 드리프트 가시화).
    if (!mHaveAnchor && w.synced) {
        for (int i = 0; i < w.numEvents; ++i)
            if (w.events[i].type == 1) { mSweepAnchor = w.events[i].sample; mHaveAnchor = true; break; }
    }
    if (isVisible()) render();   // (정지는 전역 Pause = 방송 중단이 담당)
}

// [③] 정지 중 트렌드 클릭 → 그 시점 주변 구간을 이력에서 복원해 표시(스윕 앵커 등 누적은 동결).
void TabSyncSweepScope::onSeek(double absSample)
{
    if (!mHistory || !mHistory->hasData()) return;
    const int sr = mHistory->sampleRate();
    if (sr <= 0) return;
    if (!mConfigured) { const int cap = (int)(sr * 4.0); mBuf.configure(cap); mRawBuf.configure(cap); mConfigured = true; }
    mBuf.clear();
    mRawBuf.clear();
    WaveLodHistory::replayInto(*mHistory, mBuf, &mRawBuf, absSample, (int)(sr * 1.6));
    render();
}

void TabSyncSweepScope::render()
{
    if (!mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    int beat = mRawBuf.samplesPerBeat();
    if (beat <= 0) beat = (int)(0.040 * sr);
    const int sweep = mBeats->value() * beat;

    // 프리러닝 스윕: 창 시작 = T_anchor + sweepCount·sweep (마지막 완전한 창).
    //  정시 시계 → 매 창에서 비트가 같은 위치 = 정지. 빠름/느림 → 창마다 위치 이동 = 드리프트.
    const uint64_t latest = mRawBuf.latestAbs();
    uint64_t from;
    if (mHaveAnchor && latest > mSweepAnchor + (uint64_t)sweep) {
        const uint64_t sweepCount = (latest - mSweepAnchor) / (uint64_t)sweep;
        from = mSweepAnchor + (sweepCount - 1) * (uint64_t)sweep;     // 마지막 완전한 스윕 창
    } else {
        from = latest > (uint64_t)sweep ? latest - sweep : 0;   // 미동기: 최근 구간
    }

    // 원신호 sweep 창을 가져와 folded(정류) = |raw - avg| 로 접는다.
    QVector<double> raw; mRawBuf.copyRange(from, sweep, raw);
    double mean = 0; for (double v : raw) mean += v; mean /= std::max(1, (int)raw.size());

    QVector<double> folded(sweep);
    double curPeak = 1e-9;
    for (int i = 0; i < sweep; ++i) {
        folded[i] = (i < raw.size() ? std::fabs(raw[i] - mean) : 0.0);
        if (folded[i] > curPeak) curPeak = folded[i];
    }
    // 축 고정: 스무딩 피크로 정규화(상승 즉시·하강 천천히) → 매 프레임 y범위가 흔들리지 않음.
    if (curPeak > mNorm) mNorm = curPeak; else mNorm = 0.92 * mNorm + 0.08 * curPeak;
    if (mNorm < 1e-9) mNorm = 1e-9;

    QVector<double> y(sweep), x(sweep);
    for (int i = 0; i < sweep; ++i) {
        y[i] = std::min(1.0, folded[i] / mNorm);       // 상단 클립(scope_mode.c)
        x[i] = 1000.0 * i / sr;
    }
    mPlot->graph(0)->setData(x, y, true);

    mPlot->xAxis->setRange(0, 1000.0 * sweep / sr);
    mPlot->yAxis->setRange(0, 1.08);                   // 바닥(0)에서 위로
    mPlot->replot(QCustomPlot::rpQueuedReplot);

    mInfo->setText(QString("sweep=%1 beats≈%2ms  bph=%3 %4")
        .arg(mBeats->value()).arg(1000.0*sweep/sr,0,'f',0)
        .arg(mRawBuf.bph()).arg(mHaveAnchor && mRawBuf.synced() ? "[free-run]" : "[unsynced]"));
}

void TabSyncSweepScope::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mHaveAnchor = false; mSweepAnchor = 0; mNorm = 0.0;
    if (mInfo) mInfo->setText(QStringLiteral("Waiting for signal…"));
    if (mPlot) { mPlot->graph(0)->data()->clear(); mPlot->replot(); }
}
