#include "TabSyncSweepScope.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

// ── Scope Mode with Synchronized Sweep (FR-SMS) — Project Plan §Scope Mode ──
//  프리러닝 스윕 창(틱 주기 배수)에 원신호(평균 기준 미러)를 그린다.
//  정시=패턴 정지, 빠름/느림=드리프트. 필터 비교는 'Multiple Filter Views' 탭.

TabSyncSweepScope::TabSyncSweepScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Synchronized Sweep (Scope Mode)</b> — 프리러닝 스윕(틱 주기 배수)에 원신호를 표시: "
        "정시=패턴 정지, 빠름/느림=드리프트. (FR-SMS)"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("sweep(beats):"), this));
    mBeats = new QSpinBox(this); mBeats->setRange(1, 4); mBeats->setValue(1);
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
    mPlot->graph(0)->setPen(QPen(QColor(150, 40, 130)));
    mPlot->xAxis->setLabel(QStringLiteral("sweep time (ms)"));
    mPlot->yAxis->setLabel(QStringLiteral("signal (평균 기준)"));
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

    // 원신호 sweep 창을 가져와 평균 제거(스코프 "평균 기준" 표시).
    QVector<double> raw; mRawBuf.copyRange(from, sweep, raw);
    double mean = 0; for (double v : raw) mean += v; mean /= std::max(1, (int)raw.size());

    QVector<double> y(sweep), x(sweep);
    for (int i = 0; i < sweep; ++i) { y[i] = (i < raw.size() ? raw[i] - mean : 0.0); x[i] = 1000.0 * i / sr; }
    mPlot->graph(0)->setData(x, y, true);
    mPlot->graph(0)->setPen(QPen(QColor(150, 40, 130)));

    mPlot->clearItems();
    double top = 0.0;
    for (double v : y) top = std::max(top, std::fabs(v));
    if (top <= 0.0) top = 1.0;
    const QVector<WaveEvent> evs = mBuf.eventsInRange(from, from + (uint64_t)sweep);
    for (const WaveEvent &e : evs) {                   // A(녹)·C(빨) 마커
        const double xm = 1000.0 * (double)(e.sample - from) / sr;
        auto *ln = new QCPItemLine(mPlot);
        ln->start->setCoords(xm, -top);
        ln->end->setCoords(xm, top);
        ln->setPen(QPen(e.type == 1 ? QColor(0,170,0) : QColor(220,0,0), 1, Qt::DashLine));
    }
    mPlot->xAxis->setRange(0, 1000.0 * sweep / sr);
    mPlot->yAxis->setRange(-top * 1.12, top * 1.12);
    mPlot->replot(QCustomPlot::rpQueuedReplot);

    mInfo->setText(QString("sweep=%1 beat≈%2ms  bph=%3 %4")
        .arg(mBeats->value()).arg(1000.0*sweep/sr,0,'f',1)
        .arg(mRawBuf.bph()).arg(mHaveAnchor && mRawBuf.synced() ? "[free-run]" : "[unsynced]"));
}

void TabSyncSweepScope::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mHaveAnchor = false; mSweepAnchor = 0;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) { mPlot->graph(0)->data()->clear(); mPlot->clearItems(); mPlot->replot(); }
}
