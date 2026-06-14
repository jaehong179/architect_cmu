#include "TabEscapementAnalyzer.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QSpinBox>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabEscapementAnalyzer::TabEscapementAnalyzer(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Escapement Analyzer</b> — 한 박자의 Tick(좌)·Tock(우) 버스트를 ms 축에 펼침. 각 버스트의 "
        "T1(첫 소리)·T3(셋째 소리)을 빨강 세로선 + 간격(ms)으로 표시. 녹색 세로선 = 이상적 Tock T3 위치, "
        "중앙 어두운 곡선 = Tick·Tock Beat Error. (FR-EAM)"), this));

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
    mPlot->xAxis->setLabel(QStringLiteral("time (ms, 0 = Tick T1)"));
    mPlot->yAxis->setLabel(QStringLiteral("envelope"));
    // 중앙 Beat Error 곡선(어두운 선) — 본축 위 중앙 밴드에 Tick·Tock 박자오차 이력을 표시.
    mPlot->addGraph();
    mPlot->graph(1)->setPen(QPen(QColor(40, 40, 40), 1.4));
    lay->addWidget(mPlot, 1);

    connect(mThresh, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ if (isVisible()) render(); });
}

void TabEscapementAnalyzer::onMeasurement(const MeasurementSnapshot &s)
{
    if (mBar) mBar->update(s);
    // beat error 이력 누적(중앙 곡선용) — 최근 200개 유지.
    mBeValid = s.beatErrorValid;
    if (s.beatErrorValid) {
        mBeHist.push_back(s.beatErrorMs);
        if (mBeHist.size() > 200) mBeHist.remove(0, mBeHist.size() - 200);
    }
}

void TabEscapementAnalyzer::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) { mBuf.configure(w.sampleRateHz); mConfigured = true; }
    mBuf.push(w);
    if (isVisible()) render();
}

static void vline(QCustomPlot *p, double xMs, double y0, double y1, const QColor &c, Qt::PenStyle st, double w = 1)
{
    auto *ln = new QCPItemLine(p);
    ln->start->setCoords(xMs, y0); ln->end->setCoords(xMs, y1);
    ln->setPen(QPen(c, w, st));
}

void TabEscapementAnalyzer::render()
{
    if (!mBuf.hasData()) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;
    uint64_t lastA;
    if (!mBuf.latestEvent(1, lastA)) {                  // 미동기: A 대기 — 최근 40ms 만 표시.
        const int n = (int)(0.040 * sr);
        const uint64_t from = mBuf.latestAbs() > (uint64_t)n ? mBuf.latestAbs() - n : 0;
        QVector<double> y; mBuf.copyRange(from, n, y);
        QVector<double> x(n); for (int i = 0; i < n; ++i) x[i] = 1000.0 * i / sr;
        mPlot->clearItems();
        mPlot->graph(0)->setData(x, y, true);
        mPlot->graph(1)->data()->clear();
        mPlot->rescaleAxes();
        mInfo->setText(QStringLiteral("A 이벤트 대기(미동기)…"));
        mPlot->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    const int beat = mBuf.samplesPerBeat();
    const int pre  = (int)(0.002 * sr);
    const int wpk  = (int)(0.002 * sr);                 // 국소 피크 탐색 ±2ms
    const int tail = beat > 0 ? (int)(beat * 0.6) : (int)(0.030 * sr);

    // 두 연속 onset = Tick(이전 A) + Tock(최신 A). 한 박자 안의 escapement 소리 비교.
    const QVector<WaveEvent> allEv = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<uint64_t> aList;
    for (const WaveEvent &e : allEv) if (e.type == 1) aList.push_back(e.sample);
    const bool haveTwo = aList.size() >= 2;
    const uint64_t ticA = haveTwo ? aList[aList.size() - 2] : lastA;
    const uint64_t tocA = lastA;

    // 표시 창: Tick 시작(−pre) ~ Tock 버스트 끝(+tail).
    const uint64_t from = ticA > (uint64_t)pre ? ticA - pre : 0;
    int span = (int)(tocA - from) + tail;
    if (span < (int)(0.040 * sr)) span = (int)(0.040 * sr);

    const int ticIdx = (int)(ticA - from);          // Tick T1 의 창 내 인덱스 → 이 지점을 0 ms 로.
    QVector<double> y; mBuf.copyRange(from, span, y);
    QVector<double> x(span);
    for (int i = 0; i < span; ++i) x[i] = 1000.0 * (i - ticIdx) / sr;   // 0 = Tick T1 (사양 −축 포함)
    mPlot->graph(0)->setData(x, y, true);
    const double xLo = x.first(), xHi = x.last();

    double ymax = 0.0; for (double v : y) if (v > ymax) ymax = v;
    if (ymax <= 0.0) ymax = 1.0;

    // Threshold: 검출기 onset_threshold(절대 엔벨로프 레벨) 우선, 없으면 스핀박스(창 최대 대비 %).
    const float det = mBuf.onsetThreshold();
    const bool  fromDet = det > 0.0f && (double)det < ymax * 1.5;
    const double thr = fromDet ? (double)det : ymax * (mThresh->value() / 100.0);
    const double thrPct = ymax > 0.0 ? thr / ymax * 100.0 : 0.0;

    mPlot->clearItems();
    // Threshold 녹색 수평선 + 라벨("threshold N%").
    {
        auto *hl = new QCPItemLine(mPlot);
        hl->start->setCoords(xLo, thr); hl->end->setCoords(xHi, thr);
        hl->setPen(QPen(QColor(0, 160, 90), 1, Qt::DashLine));
        auto *t = new QCPItemText(mPlot);
        t->position->setCoords(xLo, thr);
        t->setText(fromDet ? QString("threshold %1% (det)").arg(thrPct, 0, 'f', 0)
                           : QString("threshold %1%").arg(mThresh->value()));
        t->setColor(QColor(0, 130, 70));
        t->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    }

    // 한 버스트(T1=A, T3=C peak) 빨강 세로선 + 간격 라벨. 반환 = (t1Ms, t3Ms), 0 = Tick T1.
    auto drawBurst = [&](uint64_t aSample, const QString &name, double &t1Ms, double &t3Ms){
        t1Ms = 1000.0 * (double)(aSample - ticA) / sr; t3Ms = -1.0;
        vline(mPlot, t1Ms, 0, ymax, QColor(220, 0, 0), Qt::SolidLine, 1.4);   // T1 빨강
        auto *nl = new QCPItemText(mPlot);                                    // 버스트 이름(Tick/Tock)
        nl->position->setCoords(t1Ms, ymax * 1.05);
        nl->setText(name); nl->setColor(QColor(150, 0, 0));
        nl->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
        const QVector<WaveEvent> cs = mBuf.eventsInRange(aSample + 1, aSample + (uint64_t)tail);
        for (const WaveEvent &e : cs) {
            if (e.type != 2) continue;
            const int evIdx = (int)(e.sample - from);
            int pk = evIdx;
            for (int i = qMax(0, evIdx - wpk); i < qMin(span, evIdx + wpk + 1); ++i)
                if (y[i] > y[pk]) pk = i;
            t3Ms = 1000.0 * (pk - ticIdx) / sr;
            vline(mPlot, t3Ms, 0, ymax, QColor(220, 0, 0), Qt::SolidLine, 1.4);   // T3 빨강
            auto *gl = new QCPItemText(mPlot);                                    // T1→T3 간격(ms)
            gl->position->setCoords((t1Ms + t3Ms) / 2.0, ymax * 0.30);
            gl->setText(QString("%1 ms").arg(t3Ms - t1Ms, 0, 'f', 1));
            gl->setColor(Qt::black);
            gl->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            break;
        }
    };

    double ticT1 = -1, ticT3 = -1, tocT1 = -1, tocT3 = -1;
    drawBurst(ticA, QStringLiteral("Tick T1/T3"), ticT1, ticT3);
    if (haveTwo) drawBurst(tocA, QStringLiteral("Tock T1/T3"), tocT1, tocT3);

    // 이상적 Tock T3(녹색 수직선): Tick T3 + 한 박자(BPH 로부터). 실제 Tock T3 와의 차 = 박자오차.
    double idealTocMs = -1.0;
    if (beat > 0 && ticT3 >= 0) {
        idealTocMs = ticT3 + 1000.0 * (double)beat / sr;
        if (idealTocMs <= xHi + 2.0) {
            vline(mPlot, idealTocMs, 0, ymax, QColor(0, 150, 0), Qt::SolidLine, 1.6);
            auto *gt = new QCPItemText(mPlot);
            gt->position->setCoords(idealTocMs, ymax * 0.80);
            gt->setText(QStringLiteral(" ideal Tock T3"));
            gt->setColor(QColor(0, 120, 0));
            gt->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    // 중앙 Beat Error 곡선(어두운 선): Tick·Tock 박자오차 이력을 본축 중앙 밴드에 매핑.
    if (mBeHist.size() >= 2) {
        const int m = mBeHist.size();
        double bmn = mBeHist[0], bmx = mBeHist[0];
        for (double v : mBeHist) { bmn = std::min(bmn, v); bmx = std::max(bmx, v); }
        const double bmid = 0.5 * (bmn + bmx), bspan = std::max(bmx - bmn, 0.2);
        const double cen = ymax * 0.55, half = ymax * 0.16;   // 중앙 밴드(envelope 본축)
        QVector<double> bx(m), by(m);
        for (int i = 0; i < m; ++i) {
            bx[i] = xLo + (xHi - xLo) * i / (m - 1);
            by[i] = cen + (mBeHist[i] - bmid) / bspan * half;
        }
        mPlot->graph(1)->setData(bx, by, true);
        auto *bl = new QCPItemText(mPlot);                    // 곡선 라벨
        bl->position->setCoords((xLo + xHi) / 2.0, cen + half * 1.3);
        bl->setText(QStringLiteral("beat error (Tick·Tock)"));
        bl->setColor(QColor(60, 60, 60));
        bl->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    } else {
        mPlot->graph(1)->data()->clear();
    }

    mPlot->xAxis->setRange(xLo, xHi);
    mPlot->yAxis->setRange(0, ymax * 1.18);
    const double ticGap = (ticT3 >= 0) ? ticT3 - ticT1 : -1.0;
    const double tocGap = (tocT3 >= 0) ? tocT3 - tocT1 : -1.0;
    const double drift  = (tocT3 >= 0 && idealTocMs >= 0) ? tocT3 - idealTocMs : 0.0;
    mInfo->setText(QString("Tick T1→T3=%1ms  Tock T1→T3=%2ms  ΔTock(실−이상)=%3ms  bph=%4 %5")
        .arg(ticGap >= 0 ? QString::number(ticGap,'f',1) : "--")
        .arg(tocGap >= 0 ? QString::number(tocGap,'f',1) : "--")
        .arg(drift, 0, 'f', 2).arg(mBuf.bph()).arg(mBuf.synced() ? "[synced]" : ""));
    mPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabEscapementAnalyzer::onShown() { render(); }

void TabEscapementAnalyzer::onResetSession()
{
    mBuf.clear(); mConfigured = false;
    mBeHist.clear(); mBeValid = false;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) { mPlot->graph(0)->data()->clear(); mPlot->graph(1)->data()->clear(); mPlot->clearItems(); mPlot->replot(); }
}
