#include "TabSyncSweepScope.h"
#include "ReadoutBar.h"
#include "LegendBox.h"
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
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(makeLegendBox(QStringLiteral(
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td valign='top'><b>표시&nbsp;:</b></td><td>folded "
        "<font color='#c77dbf'>|신호−평균|</font>(정류, 양·음 절반 합침)을 바닥부터 채운 그래스 · "
        "y축=진폭(envelope, 정규화)</td></tr>"
        "<tr><td valign='top'><b>스윕&nbsp;:</b></td><td>창 폭 = 틱 주기 × N비트(프리러닝)</td></tr>"
        "<tr><td valign='top'><b>동작&nbsp;:</b></td><td>정시=패턴 정지 · 빠름/느림=좌우 드리프트</td></tr>"
        "</table>"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("sweep(beats):"), this));
    mBeats = new QSpinBox(this); mBeats->setRange(2, 8); mBeats->setValue(6);   // NBEATS 기본 6
    ctl->addWidget(mBeats);
    mPause = new QCheckBox(QStringLiteral("⏸ Pause"), this);          // 화면 정지(스코프 모드)
    ctl->addWidget(mPause);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    mPlot = new QCustomPlot(this);
    mPlot->addGraph();
    mPlot->graph(0)->setPen(QPen(kScopeMagenta, 1.0));
    mPlot->graph(0)->setBrush(QColor(199, 125, 191, 150));   // 바닥(0)까지 채움 → 그래스
    mPlot->graph(0)->setAdaptiveSampling(true);
    mPlot->xAxis->setLabel(QStringLiteral("sweep time (ms)"));
    mPlot->yAxis->setLabel(QStringLiteral("진폭 (envelope, 정규화 0~1)"));
    lay->addWidget(mPlot, 1);

    connect(mBeats, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ if (isVisible()) render(); });
}

void TabSyncSweepScope::onMeasurement(const MeasurementSnapshot &s) { if (mBar) mBar->update(s); }

void TabSyncSweepScope::onShown() { render(); }

void TabSyncSweepScope::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        mBuf.configure((int)(w.sampleRateHz * 1.6));
        mRawBuf.configure((int)(w.sampleRateHz * 1.6));
        mConfigured = true;
    }
    mBuf.push(w);                                      // 엔벨로프 + 이벤트(마커/동기)
    if (w.raw && w.rawN > 0) {                          // 원신호 → mRawBuf (표시)
        WaveBlock rb;
        rb.env = w.raw; rb.n = w.rawN; rb.startSample = w.rawStart;
        rb.sampleRateHz = w.sampleRateHz; rb.bph = w.bph; rb.synced = w.synced;
        mRawBuf.push(rb);
    }
    // 프리러닝 스윕 앵커: 동기 후 첫 A 이벤트에서 1회만 고정(이후 재정렬 금지 → 드리프트 가시화).
    if (!mHaveAnchor && w.synced) {
        for (int i = 0; i < w.numEvents; ++i)
            if (w.events[i].type == 1) { mSweepAnchor = w.events[i].sample; mHaveAnchor = true; break; }
    }
    if (isVisible() && !(mPause && mPause->isChecked())) render();   // Pause 시 화면 정지
}

void TabSyncSweepScope::render()
{
    if (!mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    int beat = mRawBuf.samplesPerBeat();
    if (beat <= 0) beat = (int)(0.040 * sr);
    const int sweep = mBeats->value() * beat;

    // 프리러닝 스윕: 창 시작 = T_anchor + k·sweep (마지막 완전한 창).
    //  정시 시계 → 매 창에서 비트가 같은 위치 = 정지. 빠름/느림 → 창마다 위치 이동 = 드리프트.
    const uint64_t latest = mRawBuf.latestAbs();
    uint64_t from;
    if (mHaveAnchor && latest > mSweepAnchor + (uint64_t)sweep) {
        const uint64_t k = (latest - mSweepAnchor) / (uint64_t)sweep;
        from = mSweepAnchor + (k - 1) * (uint64_t)sweep;     // 마지막 완전한 스윕 창
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
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) { mPlot->graph(0)->data()->clear(); mPlot->replot(); }
}
