#include "TabEscapementAnalyzer.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabEscapementAnalyzer::TabEscapementAnalyzer(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Escapement Analyzer</b> — beat 동기 <b>고정 x축</b>에 Tick(좌)·Tock(우) <b>원신호(raw)</b> 버스트. "
        "빨강 세로선 = T1/T3 + 간격(ms), 녹색 = 이상적 Tock T3, 자홍/청록 점선 = onset/peak. "
        "<b>가운데 점열</b> = 최근 비트 타이밍 마커: Tic(파랑)·Tac(빨강), <b>두 점열 간격 = beat error</b>, <b>기울기 = rate</b>. (FR-EAM)"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("threshold %:"), this));
    mThresh = new QSpinBox(this); mThresh->setRange(1, 30); mThresh->setValue(4);   // 참조 그림 "threshold 4%"
    ctl->addWidget(mThresh);
    mOnsetPeak = new QCheckBox(QStringLiteral("onset↔peak 비교"), this);
    mOnsetPeak->setChecked(true);
    ctl->addWidget(mOnsetPeak);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    mPlot = new QCustomPlot(this);
    mPlot->addGraph();                                            // graph(0) = 원신호(raw bipolar)
    mPlot->graph(0)->setPen(QPen(QColor(110, 110, 110)));
    mPlot->addGraph();                                            // graph(1) = Tic 점(파랑)
    mPlot->graph(1)->setLineStyle(QCPGraph::lsNone);
    mPlot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(40, 80, 200), 3));
    mPlot->graph(1)->setName(QStringLiteral("Tic (똑)"));
    mPlot->addGraph();                                            // graph(2) = Tac 점(빨강)
    mPlot->graph(2)->setLineStyle(QCPGraph::lsNone);
    mPlot->graph(2)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(200, 40, 40), 3));
    mPlot->graph(2)->setName(QStringLiteral("Tac (딱)"));
    mPlot->xAxis->setLabel(QStringLiteral("time (ms, 0 = Tick T1)"));
    mPlot->yAxis->setLabel(QStringLiteral("raw signal"));
    lay->addWidget(mPlot, 1);

    connect(mThresh, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ if (isVisible()) render(); });
    connect(mOnsetPeak, &QCheckBox::toggled, this, [this](bool){ if (isVisible()) render(); });
}

void TabEscapementAnalyzer::onMeasurement(const MeasurementSnapshot &s)
{
    if (mBar) mBar->update(s);
    if (s.beatErrorValid) mLastBeatErr = s.beatErrorMs;
}

void TabEscapementAnalyzer::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        mBuf.configure(w.sampleRateHz);
        mRawBuf.configure(w.sampleRateHz);
        mConfigured = true;
    }
    mBuf.push(w);                                                // 엔벨로프 + 이벤트(마커/동기)
    if (w.raw && w.rawN > 0) {                                   // 원신호 → mRawBuf (표시용)
        WaveBlock rb;
        rb.env = w.raw; rb.n = w.rawN; rb.startSample = w.rawStart;
        rb.sampleRateHz = w.sampleRateHz; rb.bph = w.bph; rb.synced = w.synced;
        mRawBuf.push(rb);
    }
    accumBeats(w);                                              // 가운데 점열 누적
    if (isVisible()) render();
}

// 새 A 이벤트마다 비트 타이밍오차 점 누적(E1~E3). 짝수 비트=Tic, 홀수=Tac.
void TabEscapementAnalyzer::accumBeats(const WaveBlock &w)
{
    if (w.sampleRateHz <= 0 || !w.synced || w.bph <= 0) return;
    if (mAnchored && w.bph != mAncBph) { mAnchored = false; mEnVal.clear(); mEnN.clear(); }
    const double sr = (double)w.sampleRateHz;
    const double iTargetMs = 3600.0 / w.bph * 1000.0;            // E1: I_target = 3600/BPH (ms)
    for (int i = 0; i < w.numEvents; ++i) {
        const WaveEvent &e = w.events[i];
        if (e.type != 1) continue;                              // A 이벤트(T1)만 타이밍 기준
        if (mAnchored && e.sample <= mLastA) continue;
        if (!mAnchored) { mTstart = e.sample; mBeatN = 0; mLastA = e.sample; mAncBph = w.bph; mAnchored = true; continue; }
        const double dtMs = (double)(e.sample - mTstart) / sr * 1000.0;
        const long n = (long)std::llround(dtMs / iTargetMs);    // 비트 번호(누락 견딤)
        if (n <= mBeatN) { mLastA = e.sample; continue; }
        mBeatN = n; mLastA = e.sample;
        const double En = dtMs - (double)n * iTargetMs;         // E2: Eₙ = T측정 − (T시작 + n·I목표)
        mEnVal.push_back(-En); mEnN.push_back(n);               // 빠름=+ (랩 없이 누적, 표시 때 평균 기준 정렬)
        while (mEnVal.size() > kHist) { mEnVal.remove(0); mEnN.remove(0); }
    }
}

static void vline(QCustomPlot *p, double xMs, double y0, double y1, const QColor &c, double w = 1.4)
{
    auto *ln = new QCPItemLine(p);
    ln->start->setCoords(xMs, y0); ln->end->setCoords(xMs, y1);
    ln->setPen(QPen(c, w));
}

void TabEscapementAnalyzer::render()
{
    if (!mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const double preMs = 5.0, tailMs = 25.0;
    const int pre = (int)(preMs / 1000.0 * sr);
    const int beat = mBuf.samplesPerBeat();

    uint64_t lastA;
    if (!mBuf.latestEvent(1, lastA) || beat <= 0) {              // 미동기: 최근 40ms 원신호만(마커 없음).
        const int n = (int)(0.040 * sr);
        const uint64_t from = mRawBuf.latestAbs() > (uint64_t)n ? mRawBuf.latestAbs() - n : 0;
        QVector<double> yy; mRawBuf.copyRange(from, n, yy);
        QVector<double> xx(n); for (int i = 0; i < n; ++i) xx[i] = 1000.0 * i / sr;
        mPlot->clearItems();
        mPlot->graph(0)->setData(xx, yy, true);
        mPlot->graph(1)->data()->clear(); mPlot->graph(2)->data()->clear();
        double a = 0.001; for (double v : yy) a = std::max(a, std::abs(v));
        mPlot->xAxis->setRange(0, 40);
        mPlot->yAxis->setRange(-a * 1.1, a * 1.1);
        mInfo->setText(QStringLiteral("A 이벤트 대기(미동기)…"));
        mPlot->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    // 두 연속 onset = Tick(이전 A) + Tock(최신 A).
    const QVector<WaveEvent> allEv = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<uint64_t> aList;
    for (const WaveEvent &e : allEv) if (e.type == 1) aList.push_back(e.sample);
    const bool haveTwo = aList.size() >= 2;
    const uint64_t ticA = haveTwo ? aList[aList.size() - 2] : lastA;
    const uint64_t tocA = lastA;

    // ── 고정 x축: [-preMs, beatMs+tailMs] — BPH(nominal beat)만으로 결정 → 좌우 흔들림 없음. ──
    const double beatMs = 1000.0 * beat / sr;
    const double xLo = -preMs, xHi = beatMs + tailMs;
    const int span = (int)((xHi - xLo) / 1000.0 * sr);
    const uint64_t from = ticA > (uint64_t)pre ? ticA - pre : 0;

    QVector<double> y; mRawBuf.copyRange(from, span, y);
    QVector<double> x(span);
    for (int i = 0; i < span; ++i) x[i] = 1000.0 * (i - pre) / sr;     // 0 = Tick T1
    mPlot->graph(0)->setData(x, y, true);
    QVector<double> env; mBuf.copyRange(from, span, env);             // onset/peak 검출용 엔벨로프
    double envMax = 0.001; for (double v : env) envMax = std::max(envMax, v);

    // ── y 스케일: raw 최댓값(T1/T3 의 날카로운 스파이크)이 아니라 98퍼센타일로 잡아
    //  버스트 본체가 화면을 채우게 한다(스파이크는 살짝 클리핑). 초기 관찰 후 고정. ──
    QVector<double> av; av.reserve(span);
    for (double v : y) av.push_back(std::abs(v));
    double p98 = 0.001;
    if (!av.isEmpty()) {
        const int k = std::min((int)av.size() - 1, (int)(av.size() * 0.98));
        std::nth_element(av.begin(), av.begin() + k, av.end());
        p98 = std::max(0.001, av[k]);
    }
    if (!mScaleLocked) {
        mAmpScale = std::max(mAmpScale, p98);
        if (++mScaleFrames >= kScaleWarm) mScaleLocked = true;
    } else if (p98 > mAmpScale * 1.6 || p98 < mAmpScale * 0.4) {
        mAmpScale = p98; mScaleFrames = 0; mScaleLocked = false;       // 진폭 크게 변함 → 재보정
    }
    const double A = mAmpScale * 1.25;   // 본체가 화면의 ~80% 차지

    // threshold(±): 검출기 onset 레벨 우선, 없으면 % (창 최대 대비). raw 는 바이폴라라 ± 양쪽.
    const float det = mBuf.onsetThreshold();
    const bool  fromDet = det > 0.0f && (double)det < A * 1.5;
    const double thr = fromDet ? (double)det : A * (mThresh->value() / 100.0);
    const double envThr = fromDet ? (double)det : envMax * (mThresh->value() / 100.0);
    const bool showOP = mOnsetPeak && mOnsetPeak->isChecked();

    mPlot->clearItems();
    for (double s : {1.0, -1.0}) {
        auto *hl = new QCPItemLine(mPlot);
        hl->start->setCoords(xLo, s * thr); hl->end->setCoords(xHi, s * thr);
        hl->setPen(QPen(QColor(0, 160, 90), 1, Qt::DashLine));
    }
    {
        auto *t = new QCPItemText(mPlot);
        t->position->setCoords(xLo, thr);
        t->setText(fromDet ? QStringLiteral("threshold (det)") : QString("threshold %1%").arg(mThresh->value()));
        t->setColor(QColor(0, 130, 70));
        t->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    }

    // onset↔peak 비교: 이벤트 부근 엔벨로프의 onset(임계 상향교차)·peak(국소최대)를 자홍/청록 점선.
    auto markOnsetPeak = [&](uint64_t evSample, double yLo, double yHi){
        const int c = (int)((int64_t)evSample - (int64_t)from);
        if (c < 0 || c >= span) return;
        const int wn = (int)(0.0025 * sr);
        const int lo = std::max(0, c - wn), hi = std::min(span - 1, c + wn);
        int pk = lo; for (int i = lo; i <= hi; ++i) if (env[i] > env[pk]) pk = i;
        int on = pk; while (on > 0 && env[on] > envThr) --on;
        const double onMs = 1000.0 * (on - pre) / sr, pkMs = 1000.0 * (pk - pre) / sr;
        auto dline = [&](double xm, const QColor &cc){
            auto *ln = new QCPItemLine(mPlot);
            ln->start->setCoords(xm, yLo); ln->end->setCoords(xm, yHi);
            ln->setPen(QPen(cc, 1.2, Qt::DashLine));
        };
        dline(onMs, QColor(200, 0, 200));                        // onset (자홍)
        dline(pkMs, QColor(0, 150, 150));                        // peak  (청록)
        auto *lb = new QCPItemText(mPlot);
        lb->position->setCoords(pkMs, yHi);
        lb->setText(QString("on→pk %1ms").arg(pkMs - onMs, 0, 'f', 2));
        lb->setColor(QColor(140, 0, 140));
        lb->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    };

    // 한 버스트: T1=A, T3=A 직후 첫 C. 빨강 세로선 + 간격(ms).
    auto drawBurst = [&](uint64_t aSample, const QString &name, double &t1Ms, double &t3Ms){
        t1Ms = 1000.0 * (double)((int64_t)aSample - (int64_t)ticA) / sr; t3Ms = -1.0;
        vline(mPlot, t1Ms, -A, A, QColor(220, 0, 0));
        if (showOP) markOnsetPeak(aSample, A * 0.45, A * 0.92);          // T1 onset/peak (상단)
        auto *nl = new QCPItemText(mPlot);
        nl->position->setCoords(t1Ms, A * 1.04);
        nl->setText(name); nl->setColor(QColor(150, 0, 0));
        nl->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
        const QVector<WaveEvent> cs = mBuf.eventsInRange(aSample + 1, aSample + (uint64_t)beat / 2);
        for (const WaveEvent &e : cs) {
            if (e.type != 2) continue;
            t3Ms = 1000.0 * (double)((int64_t)e.sample - (int64_t)ticA) / sr;
            vline(mPlot, t3Ms, -A, A, QColor(220, 0, 0));
            if (showOP) markOnsetPeak(e.sample, -A * 0.92, -A * 0.45);   // T3 onset/peak (하단)
            auto *gl = new QCPItemText(mPlot);
            gl->position->setCoords((t1Ms + t3Ms) / 2.0, A * 0.5);
            gl->setText(QString("%1 ms").arg(t3Ms - t1Ms, 0, 'f', 1));
            gl->setColor(Qt::black);
            gl->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            break;
        }
    };

    double ticT1 = -1, ticT3 = -1, tocT1 = -1, tocT3 = -1;
    drawBurst(ticA, QStringLiteral("Tick T1/T3"), ticT1, ticT3);
    if (haveTwo) drawBurst(tocA, QStringLiteral("Tock T1/T3"), tocT1, tocT3);

    // 이상적 Tock T3(녹색) = Tick T3 + 한 박자(nominal).
    double idealTocMs = -1.0;
    if (ticT3 >= 0) {
        idealTocMs = ticT3 + beatMs;
        vline(mPlot, idealTocMs, -A, A, QColor(0, 150, 0), 1.8);
        auto *gt = new QCPItemText(mPlot);
        gt->position->setCoords(idealTocMs, A * 0.82);
        gt->setText(QStringLiteral(" ideal Tock T3"));
        gt->setColor(QColor(0, 120, 0));
        gt->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    // ── 가운데 점열: 최근 비트 타이밍오차를 점으로 누적. Tic(짝, 파랑)·Tac(홀, 빨강). ──
    //  x = mid + Eₙ·zoom (±kWrapMs 창을 가운데 밴드폭으로 확대), y = 오래된(아래)→최신(위).
    //  두 점열 간격 = beat error, 기울기 = rate.
    const int m = mEnVal.size();
    if (m >= 1) {
        double mean = 0; for (double v : mEnVal) mean += v; mean /= m;   // 평균 기준 정렬(랩 대신)
        const double mid  = beatMs * 0.5;
        const double zoom = beatMs * 0.05;     // 타이밍오차 1ms → beat 폭의 5%
        const double band = beatMs * 0.16;     // 가운데 밴드 한계(버스트 침범 방지)
        QVector<double> tx, ty, kx, ky;
        for (int i = 0; i < m; ++i) {
            const double yp = (m == 1) ? 0.0 : (-A * 0.9 + 1.8 * A * i / (m - 1));
            double dx = (mEnVal[i] - mean) * zoom;
            dx = std::max(-band, std::min(band, dx));               // 가로 폭 제한
            const double xp = mid + dx;
            if (mEnN[i] % 2 == 0) { tx.push_back(xp); ty.push_back(yp); }   // 짝=Tic
            else                  { kx.push_back(xp); ky.push_back(yp); }   // 홀=Tac
        }
        mPlot->graph(1)->setData(tx, ty, false);
        mPlot->graph(2)->setData(kx, ky, false);
        auto *ml = new QCPItemText(mPlot);
        ml->position->setCoords(mid, A * 1.04);
        ml->setText(QStringLiteral("beat error (Tic·Tac) / rate"));
        ml->setColor(QColor(90, 90, 90));
        ml->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    } else {
        mPlot->graph(1)->data()->clear(); mPlot->graph(2)->data()->clear();
    }

    mPlot->xAxis->setRange(xLo, xHi);
    mPlot->yAxis->setRange(-A * 1.12, A * 1.12);
    const double ticGap = (ticT3 >= 0) ? ticT3 - ticT1 : -1.0;
    const double tocGap = (tocT3 >= 0) ? tocT3 - tocT1 : -1.0;
    const double drift  = (tocT3 >= 0 && idealTocMs >= 0) ? tocT3 - idealTocMs : 0.0;
    mInfo->setText(QString("Tick T1→T3=%1ms  Tock T1→T3=%2ms  beat err=%3ms  ΔTock(실−이상)=%4ms  bph=%5 %6")
        .arg(ticGap >= 0 ? QString::number(ticGap, 'f', 1) : "--")
        .arg(tocGap >= 0 ? QString::number(tocGap, 'f', 1) : "--")
        .arg(mLastBeatErr, 0, 'f', 2)
        .arg(drift, 0, 'f', 2).arg(mBuf.bph()).arg(mBuf.synced() ? "[synced]" : ""));
    mPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabEscapementAnalyzer::onShown() { render(); }

void TabEscapementAnalyzer::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mAmpScale = 0.0; mScaleFrames = 0; mScaleLocked = false;
    mAnchored = false; mTstart = 0; mBeatN = 0; mLastA = 0; mAncBph = 0;
    mEnVal.clear(); mEnN.clear(); mLastBeatErr = 0.0;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) {
        mPlot->graph(0)->data()->clear();
        mPlot->graph(1)->data()->clear();
        mPlot->graph(2)->data()->clear();
        mPlot->clearItems(); mPlot->replot();
    }
}
