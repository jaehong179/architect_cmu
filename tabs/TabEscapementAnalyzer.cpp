#include "TabEscapementAnalyzer.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QSpinBox>
#include <QHBoxLayout>

TabEscapementAnalyzer::TabEscapementAnalyzer(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Escapement Analyzer</b> — 최근 한 비트의 파형, A(녹색)·C 마커 + ms 라벨. "
        "C 는 onset(점선 자홍)과 peak(실선 빨강)을 함께 표시 → 어느 기준이 더 반복적인지 비교. (FR-EAM)"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("threshold %:"), this));
    mThresh = new QSpinBox(this); mThresh->setRange(1, 30); mThresh->setValue(4);   // 참조 그림 "threshold 4%"
    ctl->addWidget(mThresh);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    mPlot = new QCustomPlot(this);
    mPlot->addGraph();
    mPlot->graph(0)->setPen(QPen(QColor(120, 110, 0)));
    mPlot->graph(0)->setBrush(QColor(235, 215, 0, 150));            // 노란 채움(area)
    mPlot->xAxis->setLabel(QStringLiteral("time (ms, 비트 시작 기준)"));
    mPlot->yAxis->setLabel(QStringLiteral("envelope"));
    lay->addWidget(mPlot, 1);

    connect(mThresh, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ if (isVisible()) render(); });
}

void TabEscapementAnalyzer::onMeasurement(const MeasurementSnapshot &s) { if (mBar) mBar->update(s); }

void TabEscapementAnalyzer::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) { mBuf.configure(w.sampleRateHz); mConfigured = true; }
    mBuf.push(w);
    if (isVisible()) render();
}

static void vline(QCustomPlot *p, double xMs, double yTop, const QColor &c, Qt::PenStyle st)
{
    auto *ln = new QCPItemLine(p);
    ln->start->setCoords(xMs, 0); ln->end->setCoords(xMs, yTop);
    ln->setPen(QPen(c, 1, st));
}

void TabEscapementAnalyzer::render()
{
    if (!mBuf.hasData()) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    uint64_t lastA;
    if (!mBuf.latestEvent(1, lastA)) {
        const int n = (int)(0.040 * sr);
        const uint64_t from = mBuf.latestAbs() > (uint64_t)n ? mBuf.latestAbs() - n : 0;
        QVector<double> y; mBuf.copyRange(from, n, y);
        QVector<double> x(n); for (int i = 0; i < n; ++i) x[i] = 1000.0 * i / sr;
        mPlot->clearItems();
        mPlot->graph(0)->setData(x, y, true);
        mPlot->rescaleAxes();
        mInfo->setText(QStringLiteral("A 이벤트 대기(미동기)…"));
        mPlot->replot(QCustomPlot::rpQueuedReplot);
        return;
    }
    const int beat = mBuf.samplesPerBeat();
    const int pre  = (int)(0.002 * sr);
    const int span = beat > 0 ? (int)(beat * 0.6) + pre : (int)(0.040 * sr);
    const uint64_t from = lastA > (uint64_t)pre ? lastA - pre : 0;

    QVector<double> y; mBuf.copyRange(from, span, y);
    QVector<double> x(span);
    for (int i = 0; i < span; ++i) x[i] = 1000.0 * i / sr;
    mPlot->graph(0)->setData(x, y, true);

    double ymax = 0.0; for (double v : y) if (v > ymax) ymax = v;
    if (ymax <= 0.0) ymax = 1.0;
    const double thrFrac = mThresh->value() / 100.0;
    const double thr = ymax * thrFrac;                  // X_norm 기준 임계(창 최대 대비 %)

    mPlot->clearItems();
    // 임계선(수평) + 라벨 — 참조 그림의 "threshold 4%".
    {
        auto *hl = new QCPItemLine(mPlot);
        hl->start->setCoords(0, thr); hl->end->setCoords(1000.0 * span / sr, thr);
        hl->setPen(QPen(QColor(0, 160, 120), 1, Qt::DotLine));
        auto *t = new QCPItemText(mPlot);
        t->position->setCoords(0.5, thr);
        t->setText(QString("threshold %1%").arg(mThresh->value()));
        t->setColor(QColor(0, 130, 100));
        t->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    }

    // A 마커(녹색 실선).
    const double aMs = 1000.0 * (double)(lastA - from) / sr;
    vline(mPlot, aMs, ymax, QColor(0, 170, 0), Qt::SolidLine);

    // C 이벤트: peak(검출 이벤트 부근 국소 최대) vs onset(피크에서 역방향 임계 교차) 비교.
    const QVector<WaveEvent> evs = mBuf.eventsInRange(lastA + 1, from + (uint64_t)span);
    double acPeakMs = -1.0, acOnsetMs = -1.0;
    for (const WaveEvent &e : evs) {
        if (e.type != 2) continue;
        // 국소 피크 탐색(이벤트 ±2ms): 검출기의 C 배치(onset/peak 설정)와 무관하게 피크 기준 확보.
        const int evIdx = (int)(e.sample - from);
        const int w2 = (int)(0.002 * sr);
        int pk = evIdx;
        for (int i = qMax(0, evIdx - w2); i < qMin(span, evIdx + w2 + 1); ++i)
            if (y[i] > y[pk]) pk = i;
        // onset: 피크에서 역방향으로 임계 아래로 떨어지는 첫 지점의 직후.
        int on = pk;
        while (on > 0 && y[on - 1] >= thr) --on;
        acPeakMs  = 1000.0 * (pk - (int)(lastA - from)) / sr;
        acOnsetMs = 1000.0 * (on - (int)(lastA - from)) / sr;
        // 마커: peak = 빨강 실선, onset = 자홍 점선.
        vline(mPlot, 1000.0 * pk / sr, ymax, QColor(220, 0, 0), Qt::SolidLine);
        vline(mPlot, 1000.0 * on / sr, ymax, QColor(200, 0, 180), Qt::DashLine);
        // ms 라벨(두 기준 비교).
        auto *t = new QCPItemText(mPlot);
        t->position->setCoords(1000.0 * pk / sr, ymax * 0.92);
        t->setText(QString(" peak %1 ms / onset %2 ms").arg(acPeakMs, 0, 'f', 2).arg(acOnsetMs, 0, 'f', 2));
        t->setColor(Qt::black);
        t->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        break;   // 같은 비트 패킷의 첫 C 만(E8 규칙: 같은 패킷의 A·C)
    }

    mPlot->xAxis->setRange(0, 1000.0 * span / sr);
    mPlot->yAxis->setRange(0, ymax * 1.12);
    mInfo->setText(acPeakMs >= 0.0
        ? QString("A→C(peak)=%1 ms  A→C(onset)=%2 ms  Δ=%3 ms  bph=%4 %5")
              .arg(acPeakMs,0,'f',2).arg(acOnsetMs,0,'f',2).arg(acPeakMs-acOnsetMs,0,'f',2)
              .arg(mBuf.bph()).arg(mBuf.synced()?"[synced]":"")
        : QStringLiteral("A 표시(다음 C 대기)"));
    mPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabEscapementAnalyzer::onShown() { render(); }

void TabEscapementAnalyzer::onResetSession()
{
    mBuf.clear(); mConfigured = false;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) { mPlot->graph(0)->data()->clear(); mPlot->clearItems(); mPlot->replot(); }
}
