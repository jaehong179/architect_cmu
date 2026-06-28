#include "TabBeatErrorTrace.h"
#include "PlotHelpers.h"
#include "qcustomplot.h"
#include <QFont>
#include <QFontMetrics>
#include <cmath>

namespace {
// DiagWorker/DiagConfig kConfThresh 와 동일 — 탭은 diag 빌드에 의존하지 않음.
constexpr float kDiagConfThresh = 0.6f;
}

TabBeatErrorTrace::TabBeatErrorTrace(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    QFont statusFont = font();
    statusFont.setPointSizeF(14.0 * 1.5);   // 기존 diag 라벨(14pt) 대비 1.5배
    statusFont.setBold(true);
    const int statusAreaH = QFontMetrics(statusFont).height() * 2 + 8;

    mAlert = new QLabel(this);
    mAlert->setWordWrap(true);
    mAlert->setFont(statusFont);
    mAlert->setFixedHeight(statusAreaH);
    mAlert->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(mAlert);

    mDiagLabel = new QLabel(this);
    mDiagLabel->setWordWrap(true);
    mDiagLabel->setFont(statusFont);
    mDiagLabel->setFixedHeight(statusAreaH);
    mDiagLabel->setStyleSheet(QStringLiteral("color:#9e9e9e;"));
    lay->addWidget(mDiagLabel);

    mPlot = new QCustomPlot(this);
    // 두 선 모델(문서 §탭5): 짝수 비트=Tic 선, 홀수 비트=Toc 선. 점을 선으로 이어 두 trace 로 보이게 함.
    mPlot->addGraph();   // Tic (짝수 비트)
    mPlot->graph(0)->setLineStyle(QCPGraph::lsLine);
    mPlot->graph(0)->setPen(QPen(QColor(40,80,200), 1));
    mPlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(40,80,200), 3));
    mPlot->graph(0)->setName(QStringLiteral("Tic"));
    mPlot->addGraph();   // Toc (홀수 비트)
    mPlot->graph(1)->setLineStyle(QCPGraph::lsLine);
    mPlot->graph(1)->setPen(QPen(QColor(200,40,40), 1));
    mPlot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(200,40,40), 3));
    mPlot->graph(1)->setName(QStringLiteral("Toc"));
    mPlot->addGraph();   // graph(2) = rate 이상치(주황) — 점 위에 겹쳐 강조(선 연속성 유지)
    mPlot->graph(2)->setLineStyle(QCPGraph::lsNone);
    mPlot->graph(2)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(255,140,0), 6));
    mPlot->graph(2)->setName(QStringLiteral("outlier"));
    mPlot->xAxis->setLabel(QStringLiteral("beat #"));
    mPlot->yAxis->setLabel(QStringLiteral("timing error (ms) · gap between the two lines = beat error"));
    mPlot->yAxis->setRange(-kWrapMs / 2.0, kWrapMs / 2.0);
    mPlot->legend->setVisible(true);

    // 최신 비트의 Tic↔Toc 간격(=beat error)을 보여주는 초록 양방향 화살표 + ms 라벨.
    mGapLine = new QCPItemLine(mPlot);
    mGapLine->setHead(QCPLineEnding::esSpikeArrow);
    mGapLine->setTail(QCPLineEnding::esSpikeArrow);
    mGapLine->setPen(QPen(QColor(0,150,0), 1.5));
    mGapLine->setVisible(false);
    mGapText = new QCPItemText(mPlot);
    mGapText->setColor(QColor(0,120,0));
    mGapText->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mGapText->setPadding(QMargins(4,0,0,0));
    mGapText->setVisible(false);

    lay->addWidget(mPlot, 1);

    // [③] 정지 중 점 클릭 → 그 비트(beat#)의 절대 샘플 방출 + 커서.
    mSeek.attach(mPlot, [this](double absSample) { emit seekRequested(absSample); });

    onResetSession();
}

// 스냅샷은 수치 readout + beat error 경고에 사용. (트레이스 자체는 비트 이벤트 기반)
void TabBeatErrorTrace::onMeasurement(const MeasurementSnapshot &s)
{
    mBeatErrValid = s.beatErrorValid; mBeatErrMs = s.beatErrorMs;
}

void TabBeatErrorTrace::onWave(const WaveBlock &w)
{
    if (w.sampleRateHz <= 0) return;
    // BPH 동기 전엔 I_target 을 알 수 없음(E1) → 대기. BPH 변경 시 재앵커.
    if (!w.synced || w.bph <= 0) return;
    if (mAnchored && w.bph != mBph) onResetSession();

    const double sr = (double)w.sampleRateHz;
    const double iTargetMs = 3600.0 / w.bph * 1000.0;          // E1: I_target = 3600/BPH

    for (int i = 0; i < w.numEvents; ++i) {
        const WaveEvent &e = w.events[i];
        if (e.type != 1) continue;                              // A 이벤트만(타이밍 기준, T1)
        if (mAnchored && e.sample <= mLastA) continue;          // 중복/역행 방지
        if (!mAnchored) {                                       // 첫 유효 비트 = T_start 앵커
            mTstart = e.sample; mN = 0; mLastA = e.sample;
            mBph = w.bph; mAnchored = true;
            continue;
        }
        // 비트 번호 n: 이상 간격에 가장 가까운 정수(누락 비트가 있어도 시퀀스 유지).
        const double dtMs = (double)(e.sample - mTstart) / sr * 1000.0;
        const long n = (long)std::llround(dtMs / iTargetMs);
        if (n <= mN) { mLastA = e.sample; continue; }
        mN = n; mLastA = e.sample;

        // E2: E_n = T_measured − (T_start + n·I_target)   (ms)
        const double En = dtMs - (double)n * iTargetMs;
        // E3: Y = E_n mod PlotHeight — ±kWrapMs/2 창으로 랩.
        //  표시 부호는 −E_n: 빠른 시계(비트 조기 도착, E_n<0)가 '상승' 트레이스가 되도록
        //  (Plan: "positive reading shall correspond to a positively sloped trace").
        double y = std::fmod(-En + kWrapMs / 2.0, kWrapMs);
        if (y < 0) y += kWrapMs;
        y -= kWrapMs / 2.0;
        mPlot->graph(n % 2 == 0 ? 0 : 1)->addData((double)n, y);
        if (e.outlier) mPlot->graph(2)->addData((double)n, y);   // [이상치] rate 이상치 비트에 주황 점 겹침
        mSeek.addPoint((double)n, (double)e.sample);   // [③] beat# → 절대 샘플(클릭→시점)

        // E6: 기울기 m = ΔE/Δn (ms/비트) — 검출 누락으로 비트 번호가 건너뛰어도
        //  '비트당'으로 정규화해야 rate 환산이 맞는다. 지수평활로 안정화.
        if (mHavePrevE && n > mPrevN) {
            const double m = (En - mPrevE) / (double)(n - mPrevN);
            mSlopeAvg = 0.9 * mSlopeAvg + 0.1 * m;
        }
        mPrevE = En; mPrevN = n; mHavePrevE = true;
    }

    // 메모리 바운드: 오래된 점 제거.
    if (mPlot->graph(0)->dataCount() > kMaxDots) {
        const double cut = (double)mN - kMaxDots;
        mPlot->graph(0)->data()->removeBefore(cut);
        mPlot->graph(1)->data()->removeBefore(cut);
        mPlot->graph(2)->data()->removeBefore(cut);
    }

    // 최신 Tic·Toc 점 사이 간격(=beat error)을 양방향 화살표로 표시.
    //  (랩 경계로 두 점이 갈라진 경우는 시각 왜곡 방지를 위해 숨김.)
    {
        auto d0 = mPlot->graph(0)->data();
        auto d1 = mPlot->graph(1)->data();
        bool shown = false;
        if (mAnchored && mBeatErrValid && d0->size() > 0 && d1->size() > 0) {
            const double x0 = (d0->constEnd()-1)->key, y0 = (d0->constEnd()-1)->value;
            const double x1 = (d1->constEnd()-1)->key, y1 = (d1->constEnd()-1)->value;
            const double xr = qMax(x0, x1);
            if (std::fabs(y0 - y1) <= kWrapMs / 2.0) {
                mGapLine->start->setCoords(xr, y0);
                mGapLine->end->setCoords(xr, y1);
                mGapText->position->setCoords(xr, (y0 + y1) / 2.0);
                mGapText->setText(QString("gap=beat error %1 ms").arg(mBeatErrMs, 0, 'f', 2));
                shown = true;
            }
        }
        mGapLine->setVisible(shown);
        mGapText->setVisible(shown);
    }

    if (mAnchored && mHavePrevE) {
        const double slopeDeg = std::atan2(-mSlopeAvg, 1.0) * 180.0 / M_PI;
        const double rateSd = -(mSlopeAvg / iTargetMs) * 86400.0;   // E6: R = −(m/I_target)·86400
        const QString gapTxt = mBeatErrValid ? QString("%1 ms").arg(mBeatErrMs, 0, 'f', 2) : QStringLiteral("--");
        QStringList warn;
        if (mBeatErrValid && mBeatErrMs > kGoodMs)
            warn << QString("⚠ beat error (gap between the two lines) too large: %1 ms").arg(mBeatErrMs, 0, 'f', 2);
        if (std::fabs(rateSd) > kMaxRateSd)
            warn << QString("⚠ MAJOR FAULT — rate %1 s/d (|rate| > %2 s/d)").arg(rateSd, 0, 'f', 1).arg(kMaxRateSd, 0, 'f', 0);
        if (warn.isEmpty()) {
            mAlert->setText(QString("Good — beat error (gap between the two lines) %1 · slope %2° (≈ %3 s/d)")
                                .arg(gapTxt).arg(slopeDeg,0,'f',1).arg(rateSd,0,'f',1));
            mAlert->setStyleSheet(QStringLiteral("color:#2ed573; font-weight:bold;"));
        } else {
            mAlert->setText(warn.join(QStringLiteral("    ")));
            mAlert->setStyleSheet(QStringLiteral("color:#ff4757; font-weight:bold;"));
        }
    }

    if (isVisible()) {
        mPlot->xAxis->rescale();
        mPlot->yAxis->setRange(-kWrapMs / 2.0, kWrapMs / 2.0);   // Y 는 랩 창 고정
        mPlot->replot(QCustomPlot::rpQueuedReplot);
    }
}

void TabBeatErrorTrace::onShown() { if (mPlot) mPlot->replot(); }

void TabBeatErrorTrace::setDiagResult(const QString &label, float confidence, int windows)
{
    if (!mDiagLabel) return;
    const QString prefix = confidence < kDiagConfThresh ? QStringLiteral("? ") : QString();
    mDiagLabel->setText(QStringLiteral("[diag] %1%2 (%3%, %4 windows)")
                            .arg(prefix)
                            .arg(label)
                            .arg(QString::number(confidence * 100.0f, 'f', 1))
                            .arg(windows));
    if (label == QStringLiteral("normal"))
        mDiagLabel->setStyleSheet(QStringLiteral("color:#2ed573;"));
    else
        mDiagLabel->setStyleSheet(QStringLiteral("color:#ffa502;"));
}

void TabBeatErrorTrace::setDiagError(const QString &message)
{
    if (!mDiagLabel) return;
    mDiagLabel->setText(QStringLiteral("[diag] %1").arg(message));
    mDiagLabel->setStyleSheet(QStringLiteral("color:#ff4757;"));
}

void TabBeatErrorTrace::onResetSession()
{
    mAnchored = false; mTstart = 0; mN = 0; mLastA = 0; mBph = 0;
    mSeek.clear();
    mPrevE = 0.0; mPrevN = 0; mHavePrevE = false; mSlopeAvg = 0.0;
    mBeatErrMs = 0.0; mBeatErrValid = false;
    mAlert->setText(QStringLiteral("Waiting for signal…")); mAlert->setStyleSheet(QStringLiteral("color:#9e9e9e; font-weight:bold;"));
    if (mDiagLabel) {
        mDiagLabel->setText(QStringLiteral("Waiting for diagnosis…"));
        mDiagLabel->setStyleSheet(QStringLiteral("color:#9e9e9e;"));
    }
    if (mGapLine) mGapLine->setVisible(false);
    if (mGapText) mGapText->setVisible(false);
    if (mPlot) { PlotHelpers::clearAllGraphs(mPlot); mPlot->replot(); }
}
