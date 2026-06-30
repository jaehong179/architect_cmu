#include "TabFilterViews.h"
#include "ScopeFilters.h"
#include "WaveLodHistory.h"   // [③] seek replay
#include "qcustomplot.h"
#include "PlotHelpers.h"      // [PERF] applyFastPaint
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <algorithm>
#include <cmath>

// ── Scope Function with Multiple Filter Views (FR-SFM) — Project Plan §Scope Function ──
//  박자(T1) 정렬 고정창에서 F0~F3 네 필터를 테두리 칸(1×4)으로 동시 표시(T1~T3 식별 비교).
//  F0·F1·F2 미러(±), F3 upper(정류). 필터 수식은 ScopeFilters(공용 DSP). 원본 파형은 Scope Mode 탭.

static constexpr int kRenderIntervalMs = 33;   // ~30FPS 스로틀(오디오 블록마다 4패널 replot 방지)

// 필터별 색상(참고파일: F0 crimson, F1 darkgreen; F2·F3 는 신규 식별색).
static QColor filterColor(int mode)
{
    switch (mode) {
    case 0:  return QColor(220, 20, 60);     // F0 crimson
    case 1:  return QColor(0, 110, 0);       // F1 darkgreen
    case 2:  return QColor(225, 120, 0);     // F2 darkorange
    default: return QColor(90, 60, 200);     // F3 indigo
    }
}

TabFilterViews::TabFilterViews(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);

    auto *ctl = new QHBoxLayout();                       // 상태(정지는 전역 버튼이 담당)
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("Waiting for signal…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    // 각 필터 = 테두리 '칸'(QFrame) { 상단 이름 + 그래프 }. 4칸 균등.
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    for (int k = 0; k < 4; ++k) {
        const QColor c = filterColor(k);
        QFrame *cell = new QFrame(this);                 // 시각적 칸 경계
        cell->setFrameShape(QFrame::StyledPanel);
        cell->setFrameShadow(QFrame::Raised);
        auto *cl = new QVBoxLayout(cell);
        cl->setContentsMargins(4, 4, 4, 4); cl->setSpacing(3);

        // 상단: 그래프 이름(필터색 볼드, 중앙).
        QLabel *name = new QLabel(QString::fromUtf8(kScopeFilterShort[k]), cell);
        name->setAlignment(Qt::AlignHCenter);
        name->setStyleSheet(QString("font-weight:bold; font-size:13px; color:%1;").arg(c.darker(150).name()));
        cl->addWidget(name, 0);

        QCustomPlot *p = new QCustomPlot(cell);
        p->addGraph();                                  // graph(0) = +출력
        p->addGraph();                                  // graph(1) = −출력(미러 패널만 사용)
        const QColor fillC(c.red(), c.green(), c.blue(), 200);
        for (int g = 0; g < 2; ++g) {
            p->graph(g)->setPen(QPen(c, 1.0));
            p->graph(g)->setBrush(fillC);
            p->graph(g)->setAdaptiveSampling(true);     // 화면폭 초과 표본 자동 솎음(드로잉 부하↓)
        }
        p->graph(1)->setVisible(kScopeMirror[k]);       // upper 패널(F2·F3)은 하단 그래프 숨김
        p->setNoAntialiasingOnDrag(true);
        p->xAxis->setLabel(QStringLiteral("time (ms, 0=T1)"));
        p->yAxis->setLabel(kScopeMirror[k] ? QStringLiteral("amplitude(normalized) ±") : QStringLiteral("amplitude(normalized) +"));
        PlotHelpers::applyFastPaint(p);   // [PERF] 정적 표시도 AA off(채움/선) — 패널 4개라 효과 큼
        mQuad[k] = p;
        cl->addWidget(p, 1);

        row->addWidget(cell, 1);                        // 4칸 균등 폭
    }
    lay->addLayout(row, 1);
}

void TabFilterViews::onMeasurement(const MeasurementSnapshot &s)
{
    Q_UNUSED(s);
}

void TabFilterViews::onShown() { mThrottle.invalidate(); render(); }

void TabFilterViews::onWave(const WaveBlock &w)
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
    if (!isVisible()) return;   // (정지는 전역 Pause = TabManager 방송 중단이 담당)
    // ~30FPS 스로틀: 직전 렌더로부터 충분히 지났을 때만 그린다(오디오 콜백 빈도와 무관).
    if (mThrottle.isValid() && mThrottle.elapsed() < kRenderIntervalMs) return;
    mThrottle.restart();
    render();
}

// [③] 정지 중 트렌드 클릭 → 그 시점 주변 구간을 이력에서 복원해 표시(F0~F3 raw 필터 입력 포함).
void TabFilterViews::onSeek(double absSample)
{
    if (!mHistory || !mHistory->hasData()) return;
    const int sr = mHistory->sampleRate();
    if (sr <= 0) return;
    if (!mConfigured) { mBuf.configure((int)(sr * 1.6)); mRawBuf.configure((int)(sr * 1.6)); mConfigured = true; }
    mBuf.clear();
    mRawBuf.clear();
    WaveLodHistory::replayInto(*mHistory, mBuf, &mRawBuf, absSample, (int)(sr * 1.6));
    render();
}

void TabFilterViews::render()
{
    if (!mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;

    // 박자(T1) 기준 짧은 고정 줌 창 — 한 버스트가 패널을 채우도록.
    const int preS  = std::max(1, (int)(kPreMs * sr / 1000.0));
    const int postS = std::max(1, (int)(kPostMs * sr / 1000.0));
    const int sweep = preS + postS;

    // 가장 최근의 '완전히 버퍼링된' T1(A) 이벤트에 정렬(뒤쪽 postS 샘플까지 확보된 것).
    //  기준 공간은 mRawBuf(=원신호/입력 절대샘플). 이벤트 .sample 도 같은 입력 공간이므로
    //  mBuf.eventsInRange 에 원신호 공간 경계를 그대로 넘긴다(버퍼 베이스가 달라도 정합).
    int beat = mRawBuf.samplesPerBeat(); if (beat <= 0) beat = (int)(0.166 * sr);
    const uint64_t latest = mRawBuf.latestAbs();
    const uint64_t scanSpan = (uint64_t)std::max(sweep * 3, beat * 2);
    const uint64_t scanFrom = latest > scanSpan ? latest - scanSpan : 0;
    const QVector<WaveEvent> recent = mBuf.eventsInRange(scanFrom, latest);
    uint64_t anchorA = 0; bool haveA = false;
    for (int i = recent.size() - 1; i >= 0; --i)
        if (recent[i].type == 1 && recent[i].sample + (uint64_t)postS <= latest) {
            anchorA = recent[i].sample; haveA = true; break;
        }
    uint64_t from;
    if (haveA && anchorA >= (uint64_t)preS) {
        from = anchorA - (uint64_t)preS;               // T1 을 x=0 으로
    } else {                                           // 미검출: 최근 구간(폴백)
        from = latest > (uint64_t)sweep ? latest - sweep : 0;
        anchorA = from + (uint64_t)preS;
    }

    // 필터 워밍업: 창 앞쪽 여유 샘플 포함 처리 후 잘라낸다(이동평균/감쇠 트랜지언트 제거).
    const int warm = (int)std::min<uint64_t>(1024, from);   // (int)from 은 장시간 세션에서 오버플로
    const uint64_t fetchFrom = from - (uint64_t)warm;
    QVector<double> rawFull; mRawBuf.copyRange(fetchFrom, warm + sweep, rawFull);

    // F0~F3 출력 4개 + T1/T2/T3 검출(평균 제거는 computeScopeFilters 내부 avg 처리).
    QVector<double> out[4]; int pulses[3] = { -1, -1, -1 };
    computeScopeFilters(rawFull, sr, out, pulses);

    for (int k = 0; k < 4; ++k) drawPanel(k, out[k], warm, sweep, sr, preS, pulses);
    mInfo->setText(QString("F0~F3  window=%1ms(0=T1)  bph=%2 %3")
        .arg(1000.0*sweep/sr,0,'f',0)
        .arg(mRawBuf.bph()).arg(haveA && mRawBuf.synced() ? "[beat-locked]" : "[unsynced]"));
}

// 한 패널을 미러(F0·F1: ±|out|)/upper(F2·F3: +out)로 그리고 T1/T2/T3 마커를 표시. x: 0=T1.
void TabFilterViews::drawPanel(int k, const QVector<double> &out, int warm, int sweep, int sr, int preS, const int pulses[3])
{
    QCustomPlot *p = mQuad[k];
    if (!p) return;
    const bool mirror = kScopeMirror[k];               // F0·F1 미러(±), F2·F3 upper(+만)

    // 워밍업 구간 제거 → 표시 윈도우. 미러: ±out, upper: +out. x=(i-preS) → T1=0ms.
    //  sweep(수만 샘플)은 화면 폭 대비 과하므로, 실제 플롯 픽셀 폭에 맞춰(픽셀당 ~2점) 버킷별 '최대'
    //  (엔벨로프 피크 보존)로 데시메이션 → setData/replot 부하↓(패널 4개라 효과 큼). 마커는 원샘플 기준.
    //  축 고정을 위해 '스무딩된 피크(mNorm)'로 정규화(AGC식) → y범위 흔들림 억제.
    const int pxW = p->axisRect() ? std::max(1, p->axisRect()->width()) : 1200;
    const int targetPoints = std::min(sweep, std::max(400, pxW * 2));
    const int step = std::max(1, sweep / targetPoints);
    const int nOut = (sweep + step - 1) / step;
    QVector<double> up(nOut), lo(nOut), x(nOut), pooled(nOut);
    double curPeak = 1e-9;
    for (int b = 0; b < nOut; ++b) {
        const int i0 = b * step, i1 = std::min(sweep, i0 + step);
        double m = 0.0;
        for (int i = i0; i < i1; ++i) {
            const double v = (warm + i < out.size() ? out[warm + i] : 0.0);   // out 은 비음수 엔벨로프
            if (v > m) m = v;
        }
        pooled[b] = m;
        if (m > curPeak) curPeak = m;
        x[b] = 1000.0 * ((i0 + (i1 - i0) * 0.5) - preS) / sr;   // 버킷 중심, T1=0ms
    }
    double &nrm = mNorm[k];                             // 상승은 즉시, 하강은 천천히(흔들림 억제)
    if (curPeak > nrm) nrm = curPeak; else nrm = 0.92 * nrm + 0.08 * curPeak;
    if (nrm < 1e-9) nrm = 1e-9;
    for (int b = 0; b < nOut; ++b) {
        const double m = pooled[b] / nrm;
        up[b] = m; lo[b] = mirror ? -m : 0.0;
    }
    p->graph(0)->setData(x, up, true);
    if (mirror) p->graph(1)->setData(x, lo, true);
    const double top = 1.0;                             // 정규화 후 고정 스케일

    // T1/T2/T3 마커 — F2(전부)·F3(T1·T3)만. 선+라벨을 풀에서 재사용.
    static const QColor tcol[3] = { QColor(0,160,0), QColor(230,140,0), QColor(210,0,0) };
    static const char  *tname[3] = { "T1", "T2", "T3" };
    QVector<QCPItemLine *> &lpool = mMarks[k];
    QVector<QCPItemText *> &tpool = mTLabels[k];
    int used = 0;
    const double yLo = mirror ? -top * 1.12 : 0.0, yHi = top * 1.12;
    for (int t = 0; t < 3; ++t) {
        const bool show = (k == 2) || (k == 3 && t != 1);      // F2:T1T2T3, F3:T1T3
        const int idx = pulses[t];
        if (!show || idx < 0) continue;
        const int di = idx - warm;
        if (di < 0 || di >= sweep) continue;
        const double xm = 1000.0 * (di - preS) / sr;
        QCPItemLine *ln;
        if (used < lpool.size()) ln = lpool[used];
        else { ln = new QCPItemLine(p); lpool.append(ln); }
        ln->setVisible(true);
        ln->start->setCoords(xm, yLo); ln->end->setCoords(xm, yHi);
        ln->setPen(QPen(tcol[t], 1, Qt::DashLine));
        QCPItemText *tx;
        if (used < tpool.size()) tx = tpool[used];
        else { tx = new QCPItemText(p); tpool.append(tx); }
        tx->setVisible(true);
        tx->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
        tx->position->setType(QCPItemPosition::ptPlotCoords);
        tx->position->setCoords(xm, yHi);
        tx->setText(QString::fromLatin1(tname[t]));
        tx->setColor(tcol[t]);
        ++used;
    }
    for (int i = used; i < lpool.size(); ++i) lpool[i]->setVisible(false);
    for (int i = used; i < tpool.size(); ++i) tpool[i]->setVisible(false);

    p->xAxis->setRange(1000.0 * (-preS) / sr, 1000.0 * (sweep - preS) / sr);
    p->yAxis->setRange(mirror ? -top * 1.12 : -top * 0.04, top * 1.18);
    p->replot(QCustomPlot::rpQueuedReplot);
}

void TabFilterViews::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mThrottle.invalidate();
    for (int k = 0; k < 4; ++k) mNorm[k] = 0.0;
    if (mInfo) mInfo->setText(QStringLiteral("Waiting for signal…"));
    for (int k = 0; k < 4; ++k) {
        QCustomPlot *p = mQuad[k];
        if (!p) continue;
        p->graph(0)->data()->clear(); p->graph(1)->data()->clear();
        for (QCPItemLine *ln : mMarks[k]) ln->setVisible(false);   // 이름(셀 라벨)은 유지, T 마커만 숨김
        for (QCPItemText *tx : mTLabels[k]) tx->setVisible(false);
        p->replot();
    }
}
