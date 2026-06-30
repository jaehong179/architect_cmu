#include "TabEscapementAnalyzer.h"
#include "WaveLodHistory.h"   // [③] seek replay 시 과거 구간 복원
#include "qcustomplot.h"
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabEscapementAnalyzer::TabEscapementAnalyzer(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("threshold %:"), this));
    mThresh = new QSpinBox(this); mThresh->setRange(1, 30); mThresh->setValue(4);   // 참조 그림 "threshold 4%"
    ctl->addWidget(mThresh);
    mOnsetPeak = new QCheckBox(QStringLiteral("onset↔peak compare"), this);
    mOnsetPeak->setChecked(true);
    ctl->addWidget(mOnsetPeak);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("Waiting for signal…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    mPlot = new QCustomPlot(this);
    mPlot->addGraph();                                            // graph(0) = 원신호(raw bipolar)
    mPlot->graph(0)->setPen(QPen(QColor(110, 110, 110)));
    mPlot->addGraph();                                            // graph(1) = Tic 점(파랑)
    mPlot->graph(1)->setLineStyle(QCPGraph::lsNone);
    mPlot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(40, 80, 200), 3));
    mPlot->graph(1)->setName(QStringLiteral("Tic (tick)"));
    mPlot->addGraph();                                            // graph(2) = Tac 점(빨강)
    mPlot->graph(2)->setLineStyle(QCPGraph::lsNone);
    mPlot->graph(2)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(200, 40, 40), 3));
    mPlot->graph(2)->setName(QStringLiteral("Tac (tock)"));
    mPlot->addGraph();                                            // graph(3) = rate 이상치(주황) — RateScope 통일
    mPlot->graph(3)->setLineStyle(QCPGraph::lsNone);
    mPlot->graph(3)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(255, 140, 0), 5));
    mPlot->graph(3)->setName(QStringLiteral("outlier"));
    mPlot->xAxis->setLabel(QStringLiteral("time (ms, 0 = Tick T1)"));
    mPlot->yAxis->setLabel(QStringLiteral("amplitude (raw, bipolar)"));
    lay->addWidget(mPlot, 1);

    connect(mThresh, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){ if (isVisible()) render(); });
    connect(mOnsetPeak, &QCheckBox::toggled, this, [this](bool){ if (isVisible()) render(); });
}

void TabEscapementAnalyzer::onMeasurement(const MeasurementSnapshot &s)
{
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

// [③] 정지 중 트렌드 클릭 → 그 시점 주변 파형을 이력에서 복원해 표시(파형부만; 가운데 누적 점열은 동결).
void TabEscapementAnalyzer::onSeek(double absSample)
{
    if (!mHistory || !mHistory->hasData()) return;
    const int sr = mHistory->sampleRate();
    if (sr <= 0) return;
    if (!mConfigured) { mBuf.configure(sr); mRawBuf.configure(sr); mConfigured = true; }
    const int win = sr / 2;                                     // ~0.5초(비트 여러 개)
    const uint64_t half = (uint64_t)(win / 2);
    const uint64_t from = ((uint64_t)absSample > half) ? (uint64_t)absSample - half : 0;
    WaveLodHistory::ReconBlock rb;
    mHistory->reconstruct(from, win, rb);
    mBuf.clear();
    mRawBuf.clear();
    mHaveShownTic = false;                                      // seek → 위상 앵커 재설정
    mBuf.push(rb.block);                                        // 엔벨로프 + 이벤트
    if (rb.block.raw && rb.block.rawN > 0) {                    // 원신호 → mRawBuf
        WaveBlock raw;
        raw.env = rb.block.raw; raw.n = rb.block.rawN; raw.startSample = rb.block.rawStart;
        raw.sampleRateHz = rb.block.sampleRateHz; raw.bph = rb.block.bph; raw.synced = rb.block.synced;
        mRawBuf.push(raw);
    }
    render();   // 파형부만 갱신(accumBeats 미호출 → 가운데 누적 점열 보존)
}

// 새 A 이벤트마다 비트 타이밍오차 점 누적(E1~E3). 짝수 비트=Tic, 홀수=Tac.
void TabEscapementAnalyzer::accumBeats(const WaveBlock &w)
{
    if (w.sampleRateHz <= 0 || !w.synced || w.bph <= 0) return;
    if (mAnchored && w.bph != mAnchorBph) { mAnchored = false; mBeatErrVals.clear(); mBeatErrNums.clear(); mBeatErrOut.clear(); }
    const double sr = (double)w.sampleRateHz;
    const double iTargetMs = 3600.0 / w.bph * 1000.0;            // E1: I_target = 3600/BPH (ms)
    for (int i = 0; i < w.numEvents; ++i) {
        const WaveEvent &e = w.events[i];
        if (e.type != 1) continue;                              // A 이벤트(T1)만 타이밍 기준
        if (mAnchored && e.sample <= mLastASample) continue;
        if (!mAnchored) { mAnchorStartSample = e.sample; mBeatNumber = 0; mLastASample = e.sample; mAnchorBph = w.bph; mAnchored = true; continue; }
        const double dtMs = (double)(e.sample - mAnchorStartSample) / sr * 1000.0;
        const long n = (long)std::llround(dtMs / iTargetMs);    // 비트 번호(누락 견딤)
        if (n <= mBeatNumber) { mLastASample = e.sample; continue; }
        mBeatNumber = n; mLastASample = e.sample;
        const double En = dtMs - (double)n * iTargetMs;         // E2: Eₙ = T측정 − (T시작 + n·I목표)
        mBeatErrVals.push_back(-En); mBeatErrNums.push_back(n); mBeatErrOut.push_back(e.outlier);   // 빠름=+ (랩 없이 누적, 표시 때 평균 기준 정렬)
        while (mBeatErrVals.size() > kHist) { mBeatErrVals.remove(0); mBeatErrNums.remove(0); mBeatErrOut.remove(0); }
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
        mPlot->graph(1)->data()->clear(); mPlot->graph(2)->data()->clear(); mPlot->graph(3)->data()->clear();
        double a = 0.001; for (double v : yy) a = std::max(a, std::abs(v));
        mPlot->xAxis->setRange(0, 40);
        mPlot->yAxis->setRange(-a * 1.1, a * 1.1);
        mInfo->setText(QStringLiteral("Waiting for A event (unsynced)…"));
        mPlot->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    const QVector<WaveEvent> allEv = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<uint64_t> aList;
    for (const WaveEvent &e : allEv) if (e.type == 1) aList.push_back(e.sample);

    // 표시 위상 고정: 후보 Tick(끝에서 둘째 A)은 매 틱 1비트씩 전진하지만, 앵커는 '2비트 이상' 벌어질
    //  때만 따라간다 → 항상 같은 위상(tic→toc)만 표시 → Tock·ideal Tock T3 가 beat error 만큼 매 틱
    //  좌우로 튀지 않고 rate 드리프트만 남는다. (역행=seek/reset 시엔 즉시 재앵커.)
    const uint64_t candTic = aList.size() >= 2 ? aList[aList.size() - 2] : lastA;
    if (!mHaveShownTic || candTic < mShownTicA || candTic >= mShownTicA + (uint64_t)(1.5 * beat)) {
        mShownTicA = candTic; mHaveShownTic = true;
    }
    const uint64_t ticA = mShownTicA;
    uint64_t tocA = ticA;
    for (uint64_t a : aList) if (a > ticA) { tocA = a; break; }   // ticA 바로 다음 A = Tock
    const bool haveTwo = (tocA > ticA);

    // ── 고정 x축: [-preMs, beatMs+tailMs] — BPH(nominal beat)만으로 결정 → 좌우 흔들림 없음. ──
    const double beatMs = 1000.0 * beat / sr;
    const double xLo = -preMs, xHi = beatMs + tailMs;
    const int span = (int)((xHi - xLo) / 1000.0 * sr);
    const uint64_t from = ticA > (uint64_t)pre ? ticA - pre : 0;

    // 창 끝(from+span)이 아직 최신을 넘는다면(=오른쪽이 미래·부분 데이터) 직전 완전 프레임을 유지한다.
    //  ticA 가 최신보다 ~1비트밖에 안 뒤처져 창이 다음 비트 구간까지 뻗는데, 그게 버퍼되기 전에 그리면
    //  오른쪽이 채워졌다 비었다 = 깜빡임. 완전히 버퍼된 뒤에만 갱신 → 비트당 1회 깔끔하게 그린다.
    if (mRawBuf.latestAbs() < from + (uint64_t)span)
        return;

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

    // 한 버스트: T1=A, T3=A 직후 첫 C. 빨강 세로선 + 각 선에 T1/T3 라벨 + 버스트 헤더(Tick/Tock).
    auto label = [&](double xm, double yy, const QString &s, const QColor &cc, bool bold = false){
        auto *t = new QCPItemText(mPlot);
        t->position->setCoords(xm, yy);
        t->setText(s); t->setColor(cc);
        if (bold) t->setFont(QFont(QStringLiteral("sans"), 9, QFont::Bold));
        t->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    };
    auto drawBurst = [&](uint64_t aSample, const QString &burst, double &t1Ms, double &t3Ms){
        t1Ms = 1000.0 * (double)((int64_t)aSample - (int64_t)ticA) / sr; t3Ms = -1.0;
        vline(mPlot, t1Ms, -A, A, QColor(220, 0, 0));
        if (showOP) markOnsetPeak(aSample, A * 0.45, A * 0.92);          // T1 onset/peak (상단)
        label(t1Ms, A * 1.12, burst, QColor(80, 80, 80), true);         // 버스트 이름(Tick/Tock)
        label(t1Ms, A * 1.04, QStringLiteral("T1"), QColor(190, 0, 0)); // 좌 빨강선 = T1
        const QVector<WaveEvent> cs = mBuf.eventsInRange(aSample + 1, aSample + (uint64_t)beat / 2);
        for (const WaveEvent &e : cs) {
            if (e.type != 2) continue;
            t3Ms = 1000.0 * (double)((int64_t)e.sample - (int64_t)ticA) / sr;
            vline(mPlot, t3Ms, -A, A, QColor(220, 0, 0));
            if (showOP) markOnsetPeak(e.sample, -A * 0.92, -A * 0.45);   // T3 onset/peak (하단)
            label(t3Ms, A * 1.04, QStringLiteral("T3"), QColor(190, 0, 0));   // 우 빨강선 = T3
            auto *gl = new QCPItemText(mPlot);
            gl->position->setCoords((t1Ms + t3Ms) / 2.0, A * 0.5);
            gl->setText(QString("T1–T3 %1 ms").arg(t3Ms - t1Ms, 0, 'f', 1));
            gl->setColor(Qt::black);
            gl->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            break;
        }
    };

    double ticT1 = -1, ticT3 = -1, tocT1 = -1, tocT3 = -1;
    drawBurst(ticA, QStringLiteral("Tick"), ticT1, ticT3);
    if (haveTwo) drawBurst(tocA, QStringLiteral("Tock"), tocT1, tocT3);

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
    const int m = mBeatErrVals.size();
    if (m >= 1) {
        double mean = 0; for (double v : mBeatErrVals) mean += v; mean /= m;   // 평균 기준 정렬(랩 대신)
        const double mid  = beatMs * 0.5;
        const double zoom = beatMs * 0.05;     // 타이밍오차 1ms → beat 폭의 5%
        const double band = beatMs * 0.16;     // 가운데 밴드 한계(버스트 침범 방지)
        QVector<double> tx, ty, kx, ky, ox, oy;
        for (int i = 0; i < m; ++i) {
            const double yp = (m == 1) ? 0.0 : (-A * 0.9 + 1.8 * A * i / (m - 1));
            double dx = (mBeatErrVals[i] - mean) * zoom;
            dx = std::max(-band, std::min(band, dx));               // 가로 폭 제한
            const double xp = mid + dx;
            if (i < mBeatErrOut.size() && mBeatErrOut[i]) { ox.push_back(xp); oy.push_back(yp); }  // [이상치] 주황
            else if (mBeatErrNums[i] % 2 == 0) { tx.push_back(xp); ty.push_back(yp); }   // 짝=Tic
            else                  { kx.push_back(xp); ky.push_back(yp); }   // 홀=Tac
        }
        mPlot->graph(1)->setData(tx, ty, false);
        mPlot->graph(2)->setData(kx, ky, false);
        mPlot->graph(3)->setData(ox, oy, false);
        auto *ml = new QCPItemText(mPlot);
        ml->position->setCoords(mid, A * 1.04);
        ml->setText(QStringLiteral("beat error (Tic·Tac) / rate"));
        ml->setColor(QColor(90, 90, 90));
        ml->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    } else {
        mPlot->graph(1)->data()->clear(); mPlot->graph(2)->data()->clear(); mPlot->graph(3)->data()->clear();
    }

    mPlot->xAxis->setRange(xLo, xHi);
    mPlot->yAxis->setRange(-A * 1.12, A * 1.12);
    const double ticGap = (ticT3 >= 0) ? ticT3 - ticT1 : -1.0;
    const double tocGap = (tocT3 >= 0) ? tocT3 - tocT1 : -1.0;
    const double drift  = (tocT3 >= 0 && idealTocMs >= 0) ? tocT3 - idealTocMs : 0.0;
    mInfo->setText(QString("Tick T1→T3=%1ms  Tock T1→T3=%2ms  beat err=%3ms  ΔTock(meas−ideal)=%4ms  bph=%5 %6")
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
    mAnchored = false; mAnchorStartSample = 0; mBeatNumber = 0; mLastASample = 0; mAnchorBph = 0;
    mHaveShownTic = false;
    mBeatErrVals.clear(); mBeatErrNums.clear(); mBeatErrOut.clear(); mLastBeatErr = 0.0;
    if (mInfo) mInfo->setText(QStringLiteral("Waiting for signal…"));
    if (mPlot) {
        mPlot->graph(0)->data()->clear();
        mPlot->graph(1)->data()->clear();
        mPlot->graph(2)->data()->clear();
        mPlot->graph(3)->data()->clear();
        mPlot->clearItems(); mPlot->replot();
    }
}
