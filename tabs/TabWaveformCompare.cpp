#include "TabWaveformCompare.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QPushButton>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabWaveformCompare::TabWaveformCompare(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    // 접이식 범례(공간 확보): 버튼으로 펼침/접힘.
    auto *legendBtn = new QPushButton(QStringLiteral("▸ 범례 (펼치기)"), this);
    legendBtn->setCheckable(true); legendBtn->setChecked(false);
    legendBtn->setStyleSheet(QStringLiteral("QPushButton{ text-align:left; border:none; font-weight:bold; padding:2px; }"));
    auto *key = new QLabel(QStringLiteral(
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td valign='top'><b>Tic·Toc&nbsp;:</b></td><td>"
        "<font color='#1e78ff'><b>━ 파랑 굵은선</b></font>=측정 진폭 · <font color='#1e78ff'>┊ 파랑(0ms)</font>=C(T3) 시점 · "
        "<font color='#b43c3c'><b>┊ 빨강+숫자(50°)</b></font>·<font color='#149014'>┊ 녹색(10°)</font>=진폭 도(°) 눈금(상단축) · 하단축=시간(ms)</td></tr>"
        "<tr><td valign='top'><b>Period&nbsp;:</b></td><td>"
        "<font color='#00b400'>┊ 녹색</font>=A(T1, 임펄스 핀→팔레트 포크 타격) · "
        "<font color='#dc2828'>┊ 빨강</font>=C(T3, 이스케이프휠 잠김·포크→뱅킹핀) · "
        "<font color='#2840c8'>▮ 파랑 음영</font>=A(T1)→C(T3) 구간</td></tr>"
        "<tr><td valign='top'><b>Paperstrip&nbsp;:</b></td><td>"
        "<font color='#5080ff'>● 파랑</font>=tic · <font color='#dc4646'>● 빨강</font>=tac · 두 점열 간격=beat error · 기울기=rate · "
        "<font color='#00b400'>┊ 녹색</font>=wrap 경계</td></tr>"
        "</table>"), this);
    key->setWordWrap(true);
    key->setStyleSheet(QStringLiteral("QLabel{ background:#f6f6f6; border:1px solid #c4c4c4; border-radius:4px; padding:5px; }"));
    key->setVisible(false);                              // 기본 접힘
    connect(legendBtn, &QPushButton::toggled, this, [key, legendBtn](bool on){
        key->setVisible(on); legendBtn->setText(on ? QStringLiteral("▾ 범례 (접기)") : QStringLiteral("▸ 범례 (펼치기)"));
    });
    lay->addWidget(legendBtn);
    lay->addWidget(key);

    auto *ctl = new QHBoxLayout();
    ctl->addStretch(1);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    ctl->addWidget(clearBtn);
    lay->addLayout(ctl);
    connect(clearBtn, &QPushButton::clicked, this, &TabWaveformCompare::onResetSession);

    // ── 좌측 paperstrip (검정 배경, tic 흰점 / tac 청록점, 세로=시간) ──
    mPaper = new QCustomPlot(this);
    mPaper->setBackground(QBrush(Qt::black));
    const QFont paf(QStringLiteral("sans"), 7);
    for (QCPAxis *ax : {mPaper->xAxis, mPaper->yAxis}) {
        ax->setBasePen(QPen(Qt::white)); ax->setTickPen(QPen(Qt::white));
        ax->setSubTickPen(QPen(Qt::white)); ax->setTickLabelColor(Qt::white); ax->setLabelColor(Qt::white);
        ax->setLabelFont(paf); ax->setTickLabelFont(paf);
    }
    mPaper->axisRect()->setMargins(QMargins(2, 2, 2, 2));
    mPaper->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom);
    mPaper->xAxis->setTickLabels(false); mPaper->xAxis->setRange(0, 1);
    mPaper->xAxis->setLabel(QStringLiteral("Paperstrip"));   // 그래프 이름
    mPaper->yAxis->setLabel(QStringLiteral("시간(s) ↑최신"));   // 세로 = 시간
    mPaper->addGraph();   // graph0 = tic(파랑) — Rate/Scope·Beat Error 관례와 통일
    mPaper->graph(0)->setLineStyle(QCPGraph::lsNone);
    mPaper->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(80, 140, 255), 4.5));
    mPaper->graph(0)->setName(QStringLiteral("tic"));
    mPaper->addGraph();   // graph1 = tac(빨강)
    mPaper->graph(1)->setLineStyle(QCPGraph::lsNone);
    mPaper->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(235, 70, 70), 4.5));
    mPaper->graph(1)->setName(QStringLiteral("tac"));
    mPaper->legend->setVisible(true);
    mPaper->legend->setBrush(QBrush(QColor(0, 0, 0, 190)));
    mPaper->legend->setBorderPen(QPen(QColor(120, 120, 120)));
    mPaper->legend->setTextColor(Qt::white);
    mPaper->legend->setFont(QFont(QStringLiteral("sans"), 7));
    mPaper->legend->setIconSize(9, 9);
    mPaper->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);  // 우상단 모서리(데이터 안 가림)
    mPaper->setMinimumWidth(210); mPaper->setMaximumWidth(290);

    // ── 우측: tic / toc 평균파형 + period ──
    mTic = makeScope(); mToc = makeScope(); mPeriod = makeScope();
    mPeriod->xAxis->setLabel(QStringLiteral("ms (0=Tick T1)"));
    mPeriod->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));

    auto *body = new QHBoxLayout();
    body->addWidget(mPaper, 0);
    auto *right = new QVBoxLayout();
    right->addWidget(mTic, 2);
    right->addWidget(mToc, 2);
    right->addWidget(mPeriod, 2);
    body->addLayout(right, 1);
    lay->addLayout(body, 1);
}

// 검정 배경 흰 미러 파형 패널: graph0=베이스라인, graph1=+|mag|, graph2=−|mag|.
QCustomPlot *TabWaveformCompare::makeScope()
{
    auto *p = new QCustomPlot(this);
    p->setBackground(QBrush(Qt::black));
    const QFont axf(QStringLiteral("sans"), 7);
    for (QCPAxis *ax : {p->xAxis, p->yAxis}) {
        ax->setBasePen(QPen(Qt::white)); ax->setTickPen(QPen(Qt::white));
        ax->setSubTickPen(QPen(Qt::white)); ax->setTickLabelColor(Qt::white); ax->setLabelColor(Qt::white);
        ax->setLabelFont(axf); ax->setTickLabelFont(axf);
    }
    p->axisRect()->setMargins(QMargins(2, 2, 2, 2));             // 플롯 영역 최대화
    p->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom | QCP::msTop);
    p->yAxis->setTickLabels(false); p->yAxis->setRange(-1.05, 1.05);
    p->yAxis->setLabel(QStringLiteral("|진폭|"));
    p->xAxis->setRange(-kPreMs, kPostMs); p->xAxis->setLabel(QStringLiteral("ms (0=C)"));
    QSharedPointer<QCPAxisTickerFixed> tk(new QCPAxisTickerFixed);
    tk->setTickStep(5.0); tk->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    p->xAxis->setTicker(tk);
    p->addGraph(); p->graph(0)->setPen(Qt::NoPen);
    p->addGraph(); p->graph(1)->setPen(QPen(Qt::white, 1)); p->graph(1)->setBrush(QColor(255,255,255,230)); p->graph(1)->setChannelFillGraph(p->graph(0));
    p->addGraph(); p->graph(2)->setPen(QPen(Qt::white, 1)); p->graph(2)->setBrush(QColor(255,255,255,230)); p->graph(2)->setChannelFillGraph(p->graph(0));
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
    mBuf.push(w);
    if (w.raw && w.rawN > 0) {
        WaveBlock rb; rb.env = w.raw; rb.n = w.rawN; rb.startSample = w.rawStart;
        rb.sampleRateHz = w.sampleRateHz; rb.bph = w.bph; rb.synced = w.synced;
        mRawBuf.push(rb);
    }
    accumulate(w);
    if (isVisible()) render();
}

// C 정렬 tic/toc 코히어런트 평균(EMA) + A onset paperstrip 누적.
void TabWaveformCompare::accumulate(const WaveBlock &)
{
    const int sr = mRawBuf.sampleRate(); if (sr <= 0) return;
    const int pre  = (int)(kPreMs  / 1000.0 * sr);
    const int post = (int)(kPostMs / 1000.0 * sr);
    if (mWin <= 0) mWin = pre + post;
    const uint64_t latest = mRawBuf.latestAbs();
    const QVector<WaveEvent> evs = mBuf.eventsInRange(mBuf.oldestAbs(), latest);
    for (const WaveEvent &e : evs) {
        if (e.type == 1) {                                       // A onset → paperstrip
            if (mAHist.isEmpty() || e.sample > mAHist.last()) {
                if (!mHaveAnchor) { mAnchor = e.sample; mHaveAnchor = true; }
                mAHist.push_back(e.sample);
                while (mAHist.size() > kPaperHist) mAHist.remove(0);
            }
        } else if (e.type == 2) {                                // C peak → tic/toc 평균
            if (mHaveLastC && e.sample <= mLastC) continue;
            if (e.sample < (uint64_t)pre) continue;
            if (e.sample + (uint64_t)post > latest) continue;    // 아직 미래 → 다음 블록 대기
            QVector<double> win; mRawBuf.copyRange(e.sample - (uint64_t)pre, mWin, win);
            const bool tic = (mCCount % 2 == 0);
            QVector<double> &avg = tic ? mTicAvg : mTocAvg;
            bool &init = tic ? mTicInit : mTocInit;
            if (avg.size() != mWin || !init) { avg = win; init = true; }
            else { const double a = 0.15; for (int i = 0; i < mWin; ++i) avg[i] = (1.0 - a) * avg[i] + a * win[i]; }
            mLastC = e.sample; mHaveLastC = true; ++mCCount;
        }
    }
}

void TabWaveformCompare::drawAvgPanel(QCustomPlot *p, const QVector<double> &avg, const QString &title)
{
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int pre = (int)(kPreMs / 1000.0 * sr);
    const int n = avg.size();
    if (n < 2) { for (int g = 0; g < 3; ++g) p->graph(g)->data()->clear(); p->clearItems(); p->replot(); return; }
    QVector<double> mg(n); for (int i = 0; i < n; ++i) mg[i] = std::fabs(avg[i]);
    QVector<double> s = mg; const int k = std::min(n - 1, (int)(n * 0.90));
    std::nth_element(s.begin(), s.begin() + k, s.end());
    const double mx = std::max(1e-9, s[k]);
    QVector<double> x(n), yU(n), yL(n), base(n);
    for (int i = 0; i < n; ++i) {
        x[i] = 1000.0 * (i - pre) / sr;                          // ms, 0 = C
        const double m = std::min(1.0, mg[i] / mx * 1.0);        // 90퍼센타일을 가득 채움(파형 크게)
        yU[i] = m; yL[i] = -m; base[i] = 0.0;
    }
    p->graph(0)->setData(x, base, true);
    p->graph(1)->setData(x, yU, true);
    p->graph(2)->setData(x, yL, true);

    p->clearItems();
    auto vline = [&](double xm, const QColor &c, double w){
        if (xm < -kPreMs || xm > kPostMs) return;
        auto *ln = new QCPItemLine(p); ln->start->setCoords(xm, -1.15); ln->end->setCoords(xm, 1.15);
        ln->setPen(QPen(c, w));
    };
    // tg 식 도(°) 격자(시간 격자가 아님): 10°마다 세로선(비선형). x = −t_AC(°) = −3600·lift/(π·bph·°).
    const int bph = mBuf.bph();
    auto tAC = [&](double deg){ return -1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * deg); };
    if (bph > 0) {
        QSharedPointer<QCPAxisTickerText> top(new QCPAxisTickerText);
        for (int a = 100; a <= 360; a += 10) {                   // tg: i=10..360 step 10
            const double xm = tAC(a);
            if (xm < -kPreMs || xm > kPostMs) continue;
            const bool major = (a % 50 == 0);                    // 50° = 빨강 major + 라벨, 그 외 10° = 녹색
            vline(xm, major ? QColor(150, 40, 40) : QColor(25, 90, 25), 1);
            if (major) top->addTick(xm, QString::number(a));
        }
        p->xAxis2->setVisible(true); p->xAxis2->setTickLabels(true);
        p->xAxis2->setBasePen(QPen(Qt::white)); p->xAxis2->setTickPen(QPen(Qt::white));
        p->xAxis2->setSubTickPen(QPen(Qt::white)); p->xAxis2->setTickLabelColor(Qt::white);
        p->xAxis2->setLabelColor(Qt::white); p->xAxis2->setLabel(QStringLiteral("진폭(°)"));
        p->xAxis2->setLabelFont(QFont(QStringLiteral("sans"), 7));
        p->xAxis2->setTickLabelFont(QFont(QStringLiteral("sans"), 7));
        p->xAxis2->setRange(p->xAxis->range()); p->xAxis2->setTicker(top);
        if (mAmpValid && mAmpDeg > 0.0) vline(tAC(mAmpDeg), QColor(40, 110, 255), 3);   // 측정 진폭
    }
    vline(0.0, QColor(60, 120, 255), 1.5);                       // C(pulse) = 파랑(0ms)
    auto *t = new QCPItemText(p);                                // 그래프 이름(좌상단)
    t->position->setType(QCPItemPosition::ptAxisRectRatio);
    t->position->setCoords(0.015, 0.02); t->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    t->setText(title); t->setColor(Qt::white);
    t->setFont(QFont(QStringLiteral("sans"), 9, QFont::Bold));
    t->setBrush(QColor(0, 0, 0, 150)); t->setPadding(QMargins(3, 1, 3, 1));
    p->replot(QCustomPlot::rpQueuedReplot);
}

// 하단 period: 최신 한 박자(Tick·Tock 두 버스트) 원신호 + A/C 마커 + 검출창 음영.
void TabWaveformCompare::drawPeriod()
{
    if (!mPeriod || !mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int beat = mBuf.samplesPerBeat();
    const QVector<WaveEvent> evs = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<uint64_t> as; for (const WaveEvent &e : evs) if (e.type == 1) as.push_back(e.sample);
    if (as.size() < 2 || beat <= 0) { for (int g = 0; g < 3; ++g) mPeriod->graph(g)->data()->clear(); mPeriod->clearItems(); mPeriod->replot(); return; }
    const uint64_t ticA = as[as.size() - 2], tocA = as.last();
    const int pre = (int)(0.005 * sr);
    const uint64_t from = ticA > (uint64_t)pre ? ticA - pre : 0;
    const int count = (int)(tocA - from) + (int)(0.012 * sr);
    if (count <= 1) return;
    QVector<double> raw; mRawBuf.copyRange(from, count, raw);
    QVector<double> mg(count); for (int i = 0; i < count; ++i) mg[i] = std::fabs(raw[i]);
    QVector<double> s = mg; const int k = std::min(count - 1, (int)(count * 0.98));
    std::nth_element(s.begin(), s.begin() + k, s.end());
    const double mx = std::max(1e-9, s[k]);
    const int step = std::max(1, count / 2000);
    QVector<double> x, yU, yL, base;
    for (int i = 0; i < count; i += step) {
        x.push_back(1000.0 * ((double)from + i - (double)ticA) / sr);
        const double m = std::min(1.0, mg[i] / mx * 0.96);
        yU.push_back(m); yL.push_back(-m); base.push_back(0.0);
    }
    mPeriod->graph(0)->setData(x, base, true);
    mPeriod->graph(1)->setData(x, yU, true);
    mPeriod->graph(2)->setData(x, yL, true);
    mPeriod->clearItems();
    auto shadeAC = [&](uint64_t a){
        for (const WaveEvent &e : evs) if (e.type == 2 && e.sample > a && e.sample < a + (uint64_t)beat / 2) {
            const double ax = 1000.0 * ((double)a - (double)ticA) / sr, cx = 1000.0 * ((double)e.sample - (double)ticA) / sr;
            auto *r = new QCPItemRect(mPeriod); r->topLeft->setCoords(ax, 1.15); r->bottomRight->setCoords(cx, -1.15);
            r->setPen(Qt::NoPen); r->setBrush(QColor(40, 60, 200, 70));
            break;
        }
    };
    shadeAC(ticA); shadeAC(tocA);
    auto vl = [&](uint64_t smp, const QColor &c){
        const double xm = 1000.0 * ((double)smp - (double)ticA) / sr;
        auto *ln = new QCPItemLine(mPeriod); ln->start->setCoords(xm, -1.15); ln->end->setCoords(xm, 1.15);
        ln->setPen(QPen(c, 1.2));
    };
    vl(ticA, QColor(0, 200, 0)); vl(tocA, QColor(0, 200, 0));
    for (const WaveEvent &e : evs) if (e.type == 2 && e.sample >= ticA && e.sample <= tocA + (uint64_t)beat / 2) vl(e.sample, QColor(220, 40, 40));
    auto *nm = new QCPItemText(mPeriod);                         // 그래프 이름
    nm->position->setType(QCPItemPosition::ptAxisRectRatio);
    nm->position->setCoords(0.015, 0.02); nm->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    nm->setText(QStringLiteral("Period")); nm->setColor(Qt::white);
    nm->setFont(QFont(QStringLiteral("sans"), 9, QFont::Bold));
    nm->setBrush(QColor(0, 0, 0, 150)); nm->setPadding(QMargins(3, 1, 3, 1));
    mPeriod->xAxis->setRange(x.isEmpty() ? -5 : x.first(), x.isEmpty() ? 5 : x.last());
    mPeriod->yAxis->setRange(-1.05, 1.05);
    mPeriod->replot(QCustomPlot::rpQueuedReplot);
}

// 좌측 paperstrip: tic/tac onset 을 세로(시간)·가로(2박자 폴딩 위상)로 점 표시.
void TabWaveformCompare::drawPaperstrip()
{
    if (!mPaper) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int beat = mBuf.samplesPerBeat();
    if (beat <= 0 || mAHist.isEmpty() || !mHaveAnchor) {
        mPaper->graph(0)->data()->clear(); mPaper->graph(1)->data()->clear(); mPaper->clearItems(); mPaper->replot(); return;
    }
    // tg paperstrip: 폴딩창 W = 비트주기/zoom. 고정 앵커(점프 없음) + 고정 0.5 오프셋(초기 가운데).
    //  → tic·tac 이 거의 같은 x 로 접히되 'beat error' 만큼 벌어지고, rate 드리프트는 기울기/ wrap 으로 보임.
    const double W   = (double)beat / kPaperZoom;
    const double Wms = 1000.0 * W / sr;
    const double latestT = (double)mRawBuf.latestAbs() / sr;
    // 처음부터 1분 폭 고정: 경과<60s 면 [0,60], 그 이후엔 [현재−60s, 현재]로 스크롤.
    const double yLo = (latestT <= kPaperSecs) ? 0.0 : (latestT - kPaperSecs);
    const double yHi = yLo + kPaperSecs;
    QVector<double> tx, ty, kx, ky;
    for (uint64_t a : mAHist) {
        const double tSec = (double)a / sr;
        if (tSec < yLo) continue;
        const long nbeat = (long)llround((double)((int64_t)a - (int64_t)mAnchor) / (double)beat);
        double rp = std::fmod((double)((int64_t)a - (int64_t)mAnchor), W);
        if (rp < 0) rp += W;
        double ph = rp / W + 0.5;                                // 고정 앵커·오프셋 → x 안 흔들림
        ph -= std::floor(ph);                                    // [0,1) wrap
        if (nbeat % 2 == 0) { tx.push_back(ph); ty.push_back(tSec); }
        else                { kx.push_back(ph); ky.push_back(tSec); }
    }
    mPaper->graph(0)->setData(tx, ty, false);
    mPaper->graph(1)->setData(kx, ky, false);
    mPaper->clearItems();
    auto vl = [&](double xx, const QColor &c){
        auto *ln = new QCPItemLine(mPaper); ln->start->setCoords(xx, yLo); ln->end->setCoords(xx, latestT);
        ln->setPen(QPen(c, 1));
    };
    vl(0.08, QColor(0, 120, 0)); vl(0.92, QColor(0, 120, 0));   // tg 녹색 마진(wrap 경계)
    auto *wl = new QCPItemText(mPaper);                          // 폭 라벨 — 좌상단 모서리(데이터 안 가림)
    wl->position->setType(QCPItemPosition::ptAxisRectRatio);
    wl->position->setCoords(0.02, 0.012); wl->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    wl->setText(QString("W=%1ms").arg(Wms, 0, 'f', 1));
    wl->setColor(Qt::white); wl->setBrush(QColor(0, 0, 0, 170)); wl->setPadding(QMargins(3, 1, 3, 1));
    mPaper->xAxis->setRange(0, 1);                               // x 고정(autoscale 아님)
    mPaper->yAxis->setRange(yLo, yHi);                           // 항상 60초 폭 고정
    mPaper->replot(QCustomPlot::rpQueuedReplot);
}

void TabWaveformCompare::render()
{
    if (!mRawBuf.hasData()) return;
    drawAvgPanel(mTic, mTicAvg, QStringLiteral("Tic"));
    drawAvgPanel(mToc, mTocAvg, QStringLiteral("Toc"));
    drawPeriod();
    drawPaperstrip();
}

void TabWaveformCompare::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mTicAvg.clear(); mTocAvg.clear(); mWin = 0; mTicInit = mTocInit = false;
    mLastC = 0; mHaveLastC = false; mCCount = 0;
    mAHist.clear(); mAnchor = 0; mHaveAnchor = false;
    if (mBar) mBar->update(MeasurementSnapshot{});
    for (QCustomPlot *p : {mPaper, mTic, mToc, mPeriod}) {
        if (!p) continue;
        for (int g = 0; g < p->graphCount(); ++g) p->graph(g)->data()->clear();
        p->clearItems(); p->replot();
    }
}
