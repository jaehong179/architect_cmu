#include "TabSyncSweepScope.h"
#include "ReadoutBar.h"
#include "qcustomplot.h"
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <algorithm>
#include <cmath>

// ── F0~F3 필터 뷰 (FR-SFM) — Project Plan §Scope Function with Multiple Filter Views ──
//  F0: 캡처된 신호 그대로(평균 기준 미러, 바이폴라) — 시스템이 가진 가장 원신호에 가까운 표현.
//  F1: F0 의 이동평균 — 엔벨로프 평활·배경잡음 감소(저진폭 성분은 덜 보일 수 있음).
//  F2: F1 기반 — 상승 기울기 강조 + 하강 기울기 감쇠(국소 상승 후 감쇠 함수) → T3·(T2) 부각.
//  F3: 평균 위 상단부만 + 상승 에지 강조 → T1·특히 T3 식별.
//  (수치 계수는 문서 미지정 → DISPLAY_TAB_IMPLEMENTATION_GUIDE [PROPOSAL] 채택:
//   F1 K≈0.7ms, F2 β=0.95, F3 γ=1.0)
//  추가 view-only 밴드패스(BP 2~10kHz, 2차 IIR Butterworth) — 검출 경로에는 절대 미사용.

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void bandpass(double fc, double Q, double fs)
    {
        if (fc > fs * 0.49) fc = fs * 0.49;            // Nyquist 보호
        const double w0 = 2.0 * M_PI * fc / fs;
        const double c = std::cos(w0), s = std::sin(w0);
        const double al = s / (2.0 * Q);
        const double a0 = 1.0 + al;
        b0 = al / a0; b1 = 0.0; b2 = -al / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
};
static void biquadRun(const Biquad &bq, const QVector<double> &in, QVector<double> &out)
{
    const int n = in.size();
    out.resize(n);
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < n; ++i) {
        const double x = in[i];
        const double y = bq.b0 * x + bq.b1 * x1 + bq.b2 * x2 - bq.a1 * y1 - bq.a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        out[i] = y;
    }
}

// F1: 이동평균(박스카, K 샘플) — Plan "moving-average filter to the F0 signal".
static void movingAverage(const QVector<double> &in, int K, QVector<double> &out)
{
    const int n = in.size();
    out.resize(n);
    if (K < 1) K = 1;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += in[i];
        if (i >= K) acc -= in[i - K];
        out[i] = acc / qMin(i + 1, K);
    }
}

// 평균 제거된 rawFull 에 필터(mode)를 적용 → filt. bipolar = 미러(양/음) 표시 여부.
static void applyFilter(const QVector<double> &rawFull, int mode, int K, int sr,
                        QVector<double> &filt, bool &bipolar)
{
    bipolar = false;
    switch (mode) {
    case 0:                                            // F0: 원신호(평균 기준 미러, 바이폴라)
        filt = rawFull; bipolar = true; break;
    case 1: {                                          // F1: |F0| 이동평균 → 평활 엔벨로프
        QVector<double> rect(rawFull.size());
        for (int i = 0; i < rawFull.size(); ++i) rect[i] = std::fabs(rawFull[i]);
        movingAverage(rect, K, filt); break;
    }
    case 2: {                                          // F2: F1 + 상승 기울기 강조·하강 감쇠
        QVector<double> rect(rawFull.size()), y1v;
        for (int i = 0; i < rawFull.size(); ++i) rect[i] = std::fabs(rawFull[i]);
        movingAverage(rect, K, y1v);
        const double beta = 0.95;
        filt.resize(y1v.size());
        double acc = 0.0, prev = 0.0;
        for (int i = 0; i < y1v.size(); ++i) {
            const double d = std::max(0.0, y1v[i] - prev);
            acc = d + beta * acc; prev = y1v[i]; filt[i] = acc;
        }
        break;
    }
    case 3: {                                          // F3: 평균 위 상단부 + 상승 에지 강조
        const double gamma = 1.0;
        filt.resize(rawFull.size());
        double prevU = 0.0;
        for (int i = 0; i < rawFull.size(); ++i) {
            const double u = std::max(0.0, rawFull[i]);
            filt[i] = u + gamma * std::max(0.0, u - prevU); prevU = u;
        }
        break;
    }
    default: {                                         // BP 2~10kHz (view-only)
        Biquad bq; bq.bandpass(4472.0, 0.56, sr);
        QVector<double> yb; biquadRun(bq, rawFull, yb);
        filt.resize(yb.size());
        for (int i = 0; i < yb.size(); ++i) filt[i] = std::fabs(yb[i]);
        break;
    }
    }
}

static const char *kFilterShort[5] = {"F0 원신호", "F1 이동평균", "F2 상승강조", "F3 상단+에지", "BP 2~10kHz"};

TabSyncSweepScope::TabSyncSweepScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Synchronized Sweep / Filter Views</b> — 프리러닝 스윕(틱 주기 배수): 정시=패턴 정지, "
        "빠름/느림=드리프트. F0 원신호(미러)·F1 이동평균·F2 상승강조(T3)·F3 상단+에지(T1·T3). (FR-SMS/FR-SFM)"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("필터:"), this));
    mFilter = new QComboBox(this);
    mFilter->addItems({QStringLiteral("F0  원신호(미러)"), QStringLiteral("F1  이동평균"),
                       QStringLiteral("F2  상승강조"), QStringLiteral("F3  상단+에지"),
                       QStringLiteral("BP  2~10kHz (view)")});
    ctl->addWidget(mFilter);
    ctl->addWidget(new QLabel(QStringLiteral("  sweep(beats):"), this));
    mBeats = new QSpinBox(this); mBeats->setRange(1, 4); mBeats->setValue(1);
    ctl->addWidget(mBeats);
    mQuadMode = new QCheckBox(QStringLiteral("F0~F3 4-패널"), this);   // FR-SFM: 동시 비교
    ctl->addWidget(mQuadMode);
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
    mPlot->yAxis->setLabel(QStringLiteral("filtered signal"));
    lay->addWidget(mPlot, 1);

    // 4-패널 컨테이너(F0~F3 가로 1×4) — 같은 sweep 창을 네 필터로 동시 표시(T1~T3 식별 비교).
    mQuadBox = new QWidget(this);
    auto *grid = new QGridLayout(mQuadBox);
    grid->setContentsMargins(0, 0, 0, 0);
    for (int k = 0; k < 4; ++k) {
        mQuad[k] = new QCustomPlot(mQuadBox);
        mQuad[k]->addGraph();
        mQuad[k]->xAxis->setLabel(QStringLiteral("sweep time (ms)"));
        mQuad[k]->yAxis->setLabel(QStringLiteral("signal"));
        grid->addWidget(mQuad[k], 0, k);           // 가로 1×4 배치(사양: F0|F1|F2|F3 나란히)
    }
    mQuadBox->setVisible(false);
    lay->addWidget(mQuadBox, 1);

    auto onMode = [this]{
        const bool quad = mQuadMode->isChecked();
        mQuadBox->setVisible(quad); mPlot->setVisible(!quad);
        mFilter->setEnabled(!quad);                  // 4-패널에선 개별 필터 선택 비활성
        if (isVisible()) render();
    };
    connect(mFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ if (isVisible()) render(); });
    connect(mBeats,  QOverload<int>::of(&QSpinBox::valueChanged),         this, [this](int){ if (isVisible()) render(); });
    connect(mQuadMode, &QCheckBox::toggled, this, onMode);
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
    if (w.raw && w.rawN > 0) {                          // 원신호 → mRawBuf (필터 입력)
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

    // 필터 워밍업: 창 앞쪽 여유 샘플 포함 처리 후 잘라낸다(시작 트랜지언트 제거).
    const int warm = (int)std::min<uint64_t>(1024, from);   // (int)from 은 장시간 세션에서 오버플로
    const uint64_t fetchFrom = from - (uint64_t)warm;
    QVector<double> rawFull; mRawBuf.copyRange(fetchFrom, warm + sweep, rawFull);
    double mean = 0; for (double v : rawFull) mean += v; mean /= std::max(1, (int)rawFull.size());
    for (double &v : rawFull) v -= mean;               // 평균 제거(F0 의 "평균 기준" 정의)

    if (mQuadMode && mQuadMode->isChecked()) {         // FR-SFM: F0~F3 4-패널 동시 비교
        for (int k = 0; k < 4; ++k) drawPanel(mQuad[k], k, rawFull, warm, sweep, sr, from, true);
    } else {
        drawPanel(mPlot, mFilter->currentIndex(), rawFull, warm, sweep, sr, from, false);
    }
    mInfo->setText(QString("%1  sweep=%2 beat≈%3ms  bph=%4 %5")
        .arg(mQuadMode && mQuadMode->isChecked() ? QStringLiteral("F0~F3 비교") : mFilter->currentText())
        .arg(mBeats->value()).arg(1000.0*sweep/sr,0,'f',1)
        .arg(mRawBuf.bph()).arg(mHaveAnchor && mRawBuf.synced() ? "[free-run]" : "[unsynced]"));
}

// 한 패널에 지정 필터로 sweep 창 + A/C 마커를 그린다(단일 보기/4-패널 공용).
void TabSyncSweepScope::drawPanel(QCustomPlot *p, int mode, const QVector<double> &rawFull,
                                  int warm, int sweep, int sr, uint64_t from, bool quadLabel)
{
    if (!p) return;
    const int K = std::max(1, (int)(0.0007 * sr));     // F1 이동평균 창 ≈ 0.7 ms
    bool bipolar = false;
    QVector<double> filt;
    applyFilter(rawFull, mode, K, sr, filt, bipolar);

    // 워밍업 구간 제거 → 표시 윈도우.
    QVector<double> y(sweep), x(sweep);
    for (int i = 0; i < sweep; ++i) { y[i] = (warm + i < filt.size() ? filt[warm + i] : 0.0); x[i] = 1000.0 * i / sr; }
    p->graph(0)->setData(x, y, true);

    if (bipolar) { p->graph(0)->setPen(QPen(QColor(150, 40, 130))); p->graph(0)->setBrush(Qt::NoBrush); }
    else         { p->graph(0)->setPen(QPen(QColor(120, 110, 0)));  p->graph(0)->setBrush(QColor(235, 215, 0, 150)); }

    p->clearItems();
    double ymax = 0.0, ymin = 0.0;
    for (double v : y) { if (v > ymax) ymax = v; if (v < ymin) ymin = v; }
    if (ymax <= 0.0 && ymin >= 0.0) ymax = 1.0;
    const double top = std::max(std::fabs(ymax), std::fabs(ymin));
    const QVector<WaveEvent> evs = mBuf.eventsInRange(from, from + (uint64_t)sweep);
    for (const WaveEvent &e : evs) {
        const double xm = 1000.0 * (double)(e.sample - from) / sr;
        auto *ln = new QCPItemLine(p);
        ln->start->setCoords(xm, bipolar ? -top : 0);
        ln->end->setCoords(xm, bipolar ? top : ymax);
        ln->setPen(QPen(e.type == 1 ? QColor(0,170,0) : QColor(220,0,0), 1, Qt::DashLine));
    }
    if (quadLabel) {                                   // 4-패널 식별 라벨(F0~F3)
        auto *t = new QCPItemText(p);
        t->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
        t->position->setType(QCPItemPosition::ptAxisRectRatio);
        t->position->setCoords(0.02, 0.02);
        t->setText(QString::fromUtf8(kFilterShort[mode < 0 || mode > 4 ? 0 : mode]));
        t->setColor(QColor(60, 60, 60));
    }
    p->xAxis->setRange(0, 1000.0 * sweep / sr);
    if (bipolar) p->yAxis->setRange(-top * 1.12, top * 1.12);
    else         p->yAxis->setRange(0, (ymax > 0 ? ymax : 1.0) * 1.12);
    p->replot(QCustomPlot::rpQueuedReplot);
}

void TabSyncSweepScope::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mHaveAnchor = false; mSweepAnchor = 0;
    if (mBar) mBar->update(MeasurementSnapshot{});
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
    if (mPlot) { mPlot->graph(0)->data()->clear(); mPlot->clearItems(); mPlot->replot(); }
    for (QCustomPlot *p : mQuad) if (p) { p->graph(0)->data()->clear(); p->clearItems(); p->replot(); }
}
