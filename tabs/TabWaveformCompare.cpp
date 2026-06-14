#include "TabWaveformCompare.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <algorithm>
#include <cmath>

TabWaveformCompare::TabWaveformCompare(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Waveform Comparison</b> — 최근 비트의 원신호를 C(lock) 피크 x=0 에 정렬해 레인 비교(레인별 정규화). "
        "녹색 곡선=엔벨로프, 파란 음영=A→C 구간(+t_AC ms), 녹색 10° 그리드/빨강 150·200·250·300°, "
        "파랑 굵은 선=측정 진폭(정상이면 파형 시작과 겹침). (FR-WCD)"), this));
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mInfo);

    mPlot = new QCustomPlot(this);
    // Tg 스타일: 검정 배경, 흰 파형, 흰 축.
    mPlot->setBackground(QBrush(Qt::black));
    for (QCPAxis *ax : {mPlot->xAxis, mPlot->yAxis}) {
        ax->setBasePen(QPen(Qt::white));
        ax->setTickPen(QPen(Qt::white));
        ax->setSubTickPen(QPen(Qt::white));
        ax->setTickLabelColor(Qt::white);
        ax->setLabelColor(Qt::white);
    }
    mPlot->yAxis->setTickLabels(false);
    mPlot->xAxis->setLabel(QStringLiteral("ms (0 = C peak)"));
    // 레인당 4그래프: raw(흰 채움) / 베이스라인(채움 기준) / ±엔벨로프(녹색).
    for (int k = 0; k < kLanes; ++k) {
        mPlot->addGraph();   // gRaw
        mPlot->graph(gRaw(k))->setPen(QPen(Qt::white, 1));
        mPlot->graph(gRaw(k))->setBrush(QBrush(QColor(255, 255, 255, 200)));
        mPlot->addGraph();   // gBase — 투명한 채움 기준선(레인 중앙)
        mPlot->graph(gBase(k))->setPen(Qt::NoPen);
        mPlot->graph(gRaw(k))->setChannelFillGraph(mPlot->graph(gBase(k)));
        mPlot->addGraph();   // gEnvU
        mPlot->graph(gEnvU(k))->setPen(QPen(QColor(0, 220, 90), 1));
        mPlot->addGraph();   // gEnvD
        mPlot->graph(gEnvD(k))->setPen(QPen(QColor(0, 220, 90), 1));
    }
    lay->addWidget(mPlot, 1);
}

void TabWaveformCompare::onMeasurement(const MeasurementSnapshot &s)
{
    if (mBar) mBar->update(s);
    if (s.liftAngle > 0) mLiftAngle = s.liftAngle;
    mAmpValid = s.amplitudeValid; mAmpDeg = s.amplitudeDeg;
}

void TabWaveformCompare::onShown() { render(); }

void TabWaveformCompare::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        mBuf.configure(w.sampleRateHz * 2);
        mRawBuf.configure(w.sampleRateHz * 2);
        mConfigured = true;
    }
    mBuf.push(w);                                       // 엔벨로프 + 이벤트(C 정렬 기준)
    if (w.raw && w.rawN > 0) {                          // 바이폴라 원신호
        WaveBlock rb; rb.env = w.raw; rb.n = w.rawN; rb.startSample = w.rawStart;
        rb.sampleRateHz = w.sampleRateHz; rb.bph = w.bph; rb.synced = w.synced;
        mRawBuf.push(rb);
    }
    if (isVisible()) render();
}

void TabWaveformCompare::render()
{
    if (!mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int pre  = (int)(kPreMs  / 1000.0 * sr);
    const int post = (int)(kPostMs / 1000.0 * sr);
    const int count = pre + post;

    // 최근 C 이벤트들(레인 정렬 기준). 같은 비트 패킷의 C 피크가 x=0.
    const QVector<WaveEvent> evs = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<uint64_t> cs;
    for (const WaveEvent &e : evs)
        if (e.type == 2 && e.sample > (uint64_t)pre && e.sample + (uint64_t)post <= mRawBuf.latestAbs())
            cs.push_back(e.sample);
    if (cs.isEmpty()) { mInfo->setText(QStringLiteral("C 이벤트 수집 중…")); return; }

    const int lanes = std::min((int)kLanes, (int)cs.size());
    const double half = 1.0, pitch = 2.3;               // 레인별 정규화(±1) + 간격
    const double yLo = -half * 1.15;
    const double yHi = pitch * (lanes - 1) + half * 1.15;

    QVector<double> x(count);
    for (int i = 0; i < count; ++i) x[i] = 1000.0 * (i - pre) / sr;

    mPlot->clearItems();
    QVector<double> acMsPerLane(lanes, -1.0);
    for (int k = 0; k < kLanes; ++k) {
        if (k >= lanes) {
            for (int g : {gRaw(k), gBase(k), gEnvU(k), gEnvD(k)}) mPlot->graph(g)->data()->clear();
            continue;
        }
        const uint64_t c = cs[cs.size() - lanes + k];
        const double off = pitch * (lanes - 1 - k);      // 위 = 최근 비트
        // 원신호: 레인별 95퍼센타일 |값| 으로 정규화 + ±half 클램프 — 버스트 본체가 레인을
        //  꽉 채우고 C 스파이크는 상하한에서 잘린다(Tg 의 시각 특성과 동일).
        QVector<double> raw; mRawBuf.copyRange(c - (uint64_t)pre, count, raw);
        double mean = 0; for (double v : raw) mean += v; mean /= count;
        QVector<double> mag(count);
        for (int i = 0; i < count; ++i) mag[i] = std::fabs(raw[i] - mean);
        QVector<double> sorted = mag;
        std::nth_element(sorted.begin(), sorted.begin() + (int)(count * 0.95), sorted.end());
        double mx = sorted[(int)(count * 0.95)];
        if (mx < 1e-12) mx = 1e-12;
        QVector<double> y(count), base(count);
        for (int i = 0; i < count; ++i) {
            double v = (raw[i] - mean) / mx * half * 0.9;
            if (v > half) v = half; else if (v < -half) v = -half;
            y[i] = v + off; base[i] = off;
        }
        mPlot->graph(gRaw(k))->setData(x, y, true);
        mPlot->graph(gBase(k))->setData(x, base, true);
        // 엔벨로프 오버레이(신호 분해) — raw 와 동일 스케일로 ±표시.
        QVector<double> env; mBuf.copyRange(c - (uint64_t)pre, count, env);
        QVector<double> eu(count), ed(count);
        for (int i = 0; i < count; ++i) {
            double e = env[i] / mx * half * 0.9;
            if (e > half) e = half;
            eu[i] = off + e; ed[i] = off - e;
        }
        mPlot->graph(gEnvU(k))->setData(x, eu, true);
        mPlot->graph(gEnvD(k))->setData(x, ed, true);
        // A→C 구간 파란 하이라이트 + t_AC 라벨(타이밍 랜드마크, Tg 좌측 패널의 파란 음영).
        const QVector<WaveEvent> laneEvs = mBuf.eventsInRange(c - (uint64_t)pre, c);
        for (int i = laneEvs.size() - 1; i >= 0; --i) {
            if (laneEvs[i].type != 1) continue;          // 마지막 A(같은 비트 패킷)
            const double aMs = -1000.0 * (double)(c - laneEvs[i].sample) / sr;
            acMsPerLane[k] = -aMs;
            auto *rect = new QCPItemRect(mPlot);
            rect->topLeft->setCoords(aMs, off + half);
            rect->bottomRight->setCoords(0.0, off - half);
            rect->setPen(Qt::NoPen);
            rect->setBrush(QBrush(QColor(40, 90, 255, 60)));
            auto *t = new QCPItemText(mPlot);
            t->position->setCoords(aMs, off + half);
            t->setText(QString("%1ms").arg(-aMs, 0, 'f', 1));
            t->setColor(QColor(120, 170, 255));
            t->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
            break;
        }
    }
    mPlot->xAxis->setRange(-kPreMs, kPostMs);
    mPlot->yAxis->setRange(yLo, yHi);

    // ── 도(°) 그리드 (E9, x = −t_AC(α)) — Tg 와 동일하게 C 기준 음수 ms 쪽에 표시 ──
    const int bph = mBuf.bph();
    auto vline = [&](double xMs, const QColor &c, double w) {
        if (xMs < -kPreMs || xMs > kPostMs) return;
        auto *ln = new QCPItemLine(mPlot);
        ln->start->setCoords(xMs, yLo); ln->end->setCoords(xMs, yHi);
        ln->setPen(QPen(c, w));
    };
    if (bph > 0) {
        for (int a = 100; a <= 360; a += 10)                              // 10° 녹색 세선
            vline(-1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * a), QColor(0, 150, 0), 1);
        for (int a : {150, 200, 250, 300}) {                              // 기준 도수: 빨강 + 라벨
            const double xm = -1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * a);
            vline(xm, QColor(200, 0, 0), 1);
            if (xm > -kPreMs && xm < kPostMs) {
                auto *t = new QCPItemText(mPlot);
                t->position->setCoords(xm, yHi);
                t->setText(QString::number(a));
                t->setColor(Qt::white);
                t->setPositionAlignment(Qt::AlignHCenter | Qt::AlignTop);
            }
        }
        // "deg" 라벨(우상단).
        auto *dt = new QCPItemText(mPlot);
        dt->position->setCoords(kPostMs - 0.5, yHi);
        dt->setText(QStringLiteral("deg"));
        dt->setColor(Qt::white);
        dt->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
        // 측정 진폭 위치(파란 굵은 선) — 정상이면 각 레인의 A onset 과 겹친다.
        if (mAmpValid && mAmpDeg > 0.0)
            vline(-1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * mAmpDeg), QColor(40, 110, 255), 3);
    }
    QString acTxt;
    for (int k = 0; k < lanes; ++k)
        if (acMsPerLane[k] > 0) acTxt += QString(" %1").arg(acMsPerLane[k], 0, 'f', 1);
    mInfo->setText(QString("레인=%1 (위=최근)  t_AC[ms]:%2  lift=%3°  bph=%4 %5")
        .arg(lanes).arg(acTxt.isEmpty() ? QStringLiteral(" --") : acTxt)
        .arg(mLiftAngle).arg(bph).arg(mBuf.synced()?"[synced]":""));
    mPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabWaveformCompare::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) { for (int i = 0; i < mPlot->graphCount(); ++i) mPlot->graph(i)->data()->clear(); mPlot->clearItems(); mPlot->replot(); }
}
