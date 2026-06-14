#include "TabWaveformCompare.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QPushButton>
#include <QSlider>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabWaveformCompare::TabWaveformCompare(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Waveform Comparison</b> — Tg 스코프 스타일: 최근 비트들을 검정 배경 위 흰 파형(중심선 상·하 미러)으로 "
        "패널마다 비교. 왼쪽 녹색 10°/빨강 150·200·250·300° 도 그리드, 파랑=측정 진폭, x=ms(0=C). (FR-WCD)"), this));
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mInfo);

    // 컨트롤: 비트 선택(이전/다음 정렬) · 파랑 커서 이동 · Clear.
    auto *ctl = new QHBoxLayout();
    auto *prevBtn = new QPushButton(QStringLiteral("◀ 이전 비트"), this);
    auto *nextBtn = new QPushButton(QStringLiteral("다음 비트 ▶"), this);
    ctl->addWidget(prevBtn); ctl->addWidget(nextBtn);
    ctl->addSpacing(12);
    ctl->addWidget(new QLabel(QStringLiteral("커서(ms):"), this));
    mCursorSld = new QSlider(Qt::Horizontal, this);
    mCursorSld->setRange((int)(-kPreMs * 10), (int)(kPostMs * 10));   // 0.1ms 분해능
    mCursorSld->setValue((int)(mCursorMs * 10));
    ctl->addWidget(mCursorSld, 1);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    ctl->addWidget(clearBtn);
    lay->addLayout(ctl);

    connect(prevBtn, &QPushButton::clicked, this, [this]{ ++mBeatOffset; render(); });
    connect(nextBtn, &QPushButton::clicked, this, [this]{ if (mBeatOffset > 0) --mBeatOffset; render(); });
    connect(clearBtn, &QPushButton::clicked, this, &TabWaveformCompare::onResetSession);
    connect(mCursorSld, &QSlider::valueChanged, this, [this](int v){ mCursorMs = v / 10.0; render(); });

    // 본문: 좌측 세로 trace 스트립 + 우측(비교 패널 스택 + 하단 개요).
    auto *body = new QHBoxLayout();
    mStrip = makePanel();                                  // 좌측 trace 스트립(세로 = 시간)
    mStrip->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    mStrip->xAxis->setTickLabels(false);
    mStrip->xAxis->setLabel(QStringLiteral("trace"));
    mStrip->yAxis->setTickLabels(true);
    mStrip->setMinimumWidth(90); mStrip->setMaximumWidth(150);
    body->addWidget(mStrip, 0);

    auto *right = new QVBoxLayout();
    for (int i = 0; i < kPanels; ++i) { auto *p = makePanel(); mPanels.push_back(p); right->addWidget(p, 1); }
    mOverview = makePanel();                               // 하단 개요: 넓은 구간 + 모든 C 마커 + 파랑 선택
    mOverview->xAxis->setLabel(QStringLiteral("overview — 최근 비트들 (파랑 = 선택)"));
    mOverview->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    right->addWidget(mOverview, 1);
    body->addLayout(right, 1);
    lay->addLayout(body, 1);
}

// Tg 스코프 패널 1개: 검정 배경 + 흰 축, 미러 채움용 그래프 3개(베이스라인 / +mag / −mag).
QCustomPlot *TabWaveformCompare::makePanel()
{
    auto *p = new QCustomPlot(this);
    p->setBackground(QBrush(Qt::black));
    for (QCPAxis *ax : {p->xAxis, p->yAxis}) {
        ax->setBasePen(QPen(Qt::white));
        ax->setTickPen(QPen(Qt::white));
        ax->setSubTickPen(QPen(Qt::white));
        ax->setTickLabelColor(Qt::white);
        ax->setLabelColor(Qt::white);
    }
    p->yAxis->setTickLabels(false);
    p->yAxis->setRange(-1.15, 1.15);
    p->xAxis->setRange(-kPreMs, kPostMs);
    p->xAxis->setLabel(QStringLiteral("ms (0 = C peak)"));
    // 5ms 간격 눈금(레퍼런스 −20…+5ms).
    QSharedPointer<QCPAxisTickerFixed> ticker(new QCPAxisTickerFixed);
    ticker->setTickStep(5.0);
    ticker->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    p->xAxis->setTicker(ticker);
    // graph0 = 베이스라인(y=0, 투명) — 채움 기준.  graph1 = +|mag|, graph2 = −|mag| (흰 채움).
    p->addGraph(); p->graph(0)->setPen(Qt::NoPen);
    p->addGraph();
    p->graph(1)->setPen(QPen(Qt::white, 1));
    p->graph(1)->setBrush(QBrush(QColor(255, 255, 255, 230)));
    p->graph(1)->setChannelFillGraph(p->graph(0));
    p->addGraph();
    p->graph(2)->setPen(QPen(Qt::white, 1));
    p->graph(2)->setBrush(QBrush(QColor(255, 255, 255, 230)));
    p->graph(2)->setChannelFillGraph(p->graph(0));
    return p;
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

// C 피크 c 기준 한 비트를 패널 p 에 그림: 흰 미러 파형 + 도 그리드.
void TabWaveformCompare::renderPanel(QCustomPlot *p, quint64 c)
{
    const int sr   = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int pre  = (int)(kPreMs  / 1000.0 * sr);
    const int post = (int)(kPostMs / 1000.0 * sr);
    const int count = pre + post;

    QVector<double> raw; mRawBuf.copyRange(c - (quint64)pre, count, raw);
    double mean = 0; for (double v : raw) mean += v; mean /= count;
    QVector<double> mag(count);
    for (int i = 0; i < count; ++i) mag[i] = std::fabs(raw[i] - mean);
    // 레인 정규화: 95퍼센타일 |값| 으로 ±1 정규화 + 클램프(스파이크는 상하한에서 잘림).
    QVector<double> sorted = mag;
    std::nth_element(sorted.begin(), sorted.begin() + (int)(count * 0.95), sorted.end());
    double mx = sorted[(int)(count * 0.95)];
    if (mx < 1e-12) mx = 1e-12;

    QVector<double> x(count), yU(count), yL(count), base(count);
    for (int i = 0; i < count; ++i) {
        x[i] = 1000.0 * (i - pre) / sr;          // ms, 0 = C peak
        double m = mag[i] / mx; if (m > 1.0) m = 1.0;
        yU[i] = m; yL[i] = -m; base[i] = 0.0;     // 중심선 기준 상·하 대칭
    }
    p->graph(0)->setData(x, base, true);
    p->graph(1)->setData(x, yU, true);
    p->graph(2)->setData(x, yL, true);

    p->clearItems();
    // ── 도(°) 그리드 (E9, x = −t_AC(α)) ── 왼쪽(음수 ms)에 표시 ──
    const int bph = mBuf.bph();
    auto vline = [&](double xMs, const QColor &col, double w) {
        if (xMs < -kPreMs || xMs > kPostMs) return;
        auto *ln = new QCPItemLine(p);
        ln->start->setCoords(xMs, -1.15); ln->end->setCoords(xMs, 1.15);
        ln->setPen(QPen(col, w));
    };
    if (bph > 0) {
        for (int a = 10; a <= 360; a += 10)                               // 10° 녹색 세선
            vline(-1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * a), QColor(0, 150, 0), 1);
        // 상단 각도(°) 축: ms↔도 비선형 매핑을 텍스트 티커로 ms 좌표 위에 도수 눈금 배치.
        QSharedPointer<QCPAxisTickerText> top(new QCPAxisTickerText);
        for (int a : {150, 200, 300}) {                                   // 기준 도수(빨강 major) + 상단 축 눈금 (사양: 150/200/300)
            const double xm = -1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * a);
            vline(xm, QColor(200, 0, 0), 1);
            if (xm > -kPreMs && xm < kPostMs) top->addTick(xm, QString::number(a));
        }
        p->xAxis2->setVisible(true);
        p->xAxis2->setTickLabels(true);
        p->xAxis2->setLabel(QStringLiteral("angle (deg)"));
        p->xAxis2->setBasePen(QPen(Qt::white)); p->xAxis2->setTickPen(QPen(Qt::white));
        p->xAxis2->setSubTickPen(QPen(Qt::white)); p->xAxis2->setTickLabelColor(Qt::white);
        p->xAxis2->setLabelColor(Qt::white);
        p->xAxis2->setRange(p->xAxis->range());                           // 동일 좌표계(ms) 위에 도수 라벨
        p->xAxis2->setTicker(top);
        if (mAmpValid && mAmpDeg > 0.0)                                   // 측정 진폭 = 파랑 굵은 선
            vline(-1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * mAmpDeg), QColor(40, 110, 255), 3);
    }
    // 파랑 커서 라인(비트 정밀 비교용) — 모든 패널 공통 위치.
    {
        auto *cur = new QCPItemLine(p);
        cur->start->setCoords(mCursorMs, -1.15); cur->end->setCoords(mCursorMs, 1.15);
        cur->setPen(QPen(QColor(40, 110, 255), 1, Qt::DashLine));
    }
    p->replot(QCustomPlot::rpQueuedReplot);
}

void TabWaveformCompare::render()
{
    if (!mRawBuf.hasData()) return;
    const int sr   = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int pre  = (int)(kPreMs  / 1000.0 * sr);
    const int post = (int)(kPostMs / 1000.0 * sr);

    // 최근 C 이벤트들(각 패널의 비트 기준). 표시 창이 버퍼 안에 드는 것만.
    const QVector<WaveEvent> evs = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<quint64> cs;
    for (const WaveEvent &e : evs)
        if (e.type == 2 && e.sample > (quint64)pre && e.sample + (quint64)post <= mRawBuf.latestAbs())
            cs.push_back(e.sample);
    if (cs.isEmpty()) { mInfo->setText(QStringLiteral("C 이벤트 수집 중…")); return; }

    // 비트 선택: 최근에서 mBeatOffset 만큼 뒤로 이동(이전/다음 버튼). 범위 클램프.
    const int maxOffset = std::max(0, (int)cs.size() - 1);
    if (mBeatOffset > maxOffset) mBeatOffset = maxOffset;
    const int newest = (int)cs.size() - 1 - mBeatOffset;        // 위 패널(i=0)이 가리킬 인덱스
    const int n = std::min((int)kPanels, newest + 1);
    for (int i = 0; i < kPanels; ++i) {
        const int idx = newest - i;
        if (i < n && idx >= 0) {
            renderPanel(mPanels[i], cs[idx]);                  // 위(i=0) = 선택 비트, 아래로 갈수록 과거
        } else {
            for (int g = 0; g < mPanels[i]->graphCount(); ++g) mPanels[i]->graph(g)->data()->clear();
            mPanels[i]->clearItems(); mPanels[i]->replot();
        }
    }
    renderOverview(cs, std::max(0, newest - n + 1), newest);    // 선택 구간 강조
    renderStrip();                                              // 좌측 세로 trace 스트립
    mInfo->setText(QString("비트 %1/%2 (offset=%3)  lift=%4°  bph=%5 %6  cursor=%7 ms")
        .arg(newest + 1).arg(cs.size()).arg(mBeatOffset).arg(mLiftAngle)
        .arg(mBuf.bph()).arg(mBuf.synced() ? "[synced]" : "").arg(mCursorMs, 0, 'f', 1));
}

// 하단 개요: 최근 다수 비트의 엔벨로프를 한 줄에 펼치고 모든 C 마커 + 선택 구간을 파랑 강조.
void TabWaveformCompare::renderOverview(const QVector<quint64> &cs, int selFrom, int selTo)
{
    if (!mOverview || cs.isEmpty()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const quint64 first = cs.first(), last = cs.last();
    const quint64 pre = (quint64)(kPreMs / 1000.0 * sr);
    const quint64 from = first > pre ? first - pre : 0;
    const int count = (int)(last - from) + (int)(kPostMs / 1000.0 * sr);
    if (count <= 1) return;
    // 다운샘플(가독성): 최대 ~2000 포인트.
    QVector<double> raw; mRawBuf.copyRange(from, count, raw);
    const int step = std::max(1, count / 2000);
    QVector<double> x, yU, yL, base;
    double mx = 1e-12;
    for (int i = 0; i < count; i += step) mx = std::max(mx, std::fabs(raw[i]));
    for (int i = 0; i < count; i += step) {
        x.push_back(1000.0 * (from + (quint64)i) / sr / 1000.0);   // 초 단위(넓은 축)
        const double m = std::min(1.0, std::fabs(raw[i]) / mx);
        yU.push_back(m); yL.push_back(-m); base.push_back(0.0);
    }
    mOverview->graph(0)->setData(x, base, true);
    mOverview->graph(1)->setData(x, yU, true);
    mOverview->graph(2)->setData(x, yL, true);
    mOverview->clearItems();
    mOverview->xAxis2->setVisible(false);
    for (int i = 0; i < cs.size(); ++i) {
        const double cx = (double)cs[i] / sr;                      // 초
        const bool sel = (i >= selFrom && i <= selTo);
        auto *ln = new QCPItemLine(mOverview);
        ln->start->setCoords(cx, -1.15); ln->end->setCoords(cx, 1.15);
        ln->setPen(QPen(sel ? QColor(40, 110, 255) : QColor(180, 60, 60), sel ? 2.5 : 1));
    }
    mOverview->xAxis->setLabel(QStringLiteral("overview — time (s), 파랑 = 선택 비트"));
    mOverview->xAxis->rescale();
    mOverview->yAxis->setRange(-1.15, 1.15);
    mOverview->replot(QCustomPlot::rpQueuedReplot);
}

// 좌측 세로 trace 스트립: 최근 0.5초 엔벨로프를 세로축=시간으로 스크롤, C 이벤트 녹색 수평선.
void TabWaveformCompare::renderStrip()
{
    if (!mStrip || !mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int win = (int)(0.5 * sr);
    const quint64 latest = mRawBuf.latestAbs();
    const quint64 from = latest > (quint64)win ? latest - win : 0;
    const int count = (int)(latest - from);
    if (count <= 1) return;
    QVector<double> raw; mRawBuf.copyRange(from, count, raw);
    const int step = std::max(1, count / 1200);
    double mx = 1e-12;
    for (int i = 0; i < count; i += step) mx = std::max(mx, std::fabs(raw[i]));
    QVector<double> tkey, tval;                                // key = time(세로), value = |amp|(가로)
    for (int i = 0; i < count; i += step) {
        tkey.push_back((double)(from + (quint64)i) / sr);
        tval.push_back(std::min(1.0, std::fabs(raw[i]) / mx));
    }
    mStrip->graph(0)->data()->clear();
    mStrip->graph(2)->data()->clear();
    mStrip->graph(1)->setChannelFillGraph(nullptr);
    mStrip->graph(1)->setKeyAxis(mStrip->yAxis);               // 시간축을 세로로
    mStrip->graph(1)->setValueAxis(mStrip->xAxis);
    mStrip->graph(1)->setData(tkey, tval, true);
    mStrip->clearItems();
    const QVector<WaveEvent> evs = mBuf.eventsInRange(from, latest);
    for (const WaveEvent &e : evs) if (e.type == 2) {          // C 이벤트 = 녹색 수평선
        const double tt = (double)e.sample / sr;
        auto *ln = new QCPItemLine(mStrip);
        ln->start->setCoords(0, tt); ln->end->setCoords(1, tt);
        ln->setPen(QPen(QColor(0, 160, 0), 1));
    }
    mStrip->xAxis->setRange(0, 1.05);
    mStrip->yAxis->setRange((double)from / sr, (double)latest / sr);
    mStrip->replot(QCustomPlot::rpQueuedReplot);
}

void TabWaveformCompare::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false; mBeatOffset = 0;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    QVector<QCustomPlot*> all = mPanels; if (mOverview) all.push_back(mOverview); if (mStrip) all.push_back(mStrip);
    for (QCustomPlot *p : all) {
        for (int g = 0; g < p->graphCount(); ++g) p->graph(g)->data()->clear();
        p->clearItems(); p->replot();
    }
}
