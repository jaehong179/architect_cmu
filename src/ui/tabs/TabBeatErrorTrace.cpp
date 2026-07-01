#include "TabBeatErrorTrace.h"
#include "PlotHelpers.h"
#include "qcustomplot.h"
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <cmath>

namespace {
enum BedAlertTier { kTierUnknown = 0, kTierGood = 1, kTierCaution = 2, kTierBad = 3 };

const char *bedTierLabel(int tier)
{
    switch (tier) {
    case kTierGood:    return "Good";
    case kTierCaution: return "Caution";
    case kTierBad:     return "Bad";
    default:           return "Unknown";
    }
}
} // namespace

TabBeatErrorTrace::TabBeatErrorTrace(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);

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
    mPlot->xAxis->setLabel(QStringLiteral("time (s)"));   // 측정 시작 기준 경과 초(툴팁 t 와 동일 좌표)
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

// 스냅샷은 수치 readout + Good/Bad 경고에 사용. (트레이스 자체는 비트 이벤트 기반)
void TabBeatErrorTrace::onMeasurement(const MeasurementSnapshot &s)
{
    mBeatErrValid = s.beatErrorValid; mBeatErrMs = s.beatErrorMs;
    mRateValid    = s.rateValid;      mRateSd    = s.rate;
    mPlotOriginSec = s.plotTimeOriginSec;   // x축·툴팁 t 의 공통 원점(측정 시작=워밍업 종료)
    updateStatusAlert();
}

void TabBeatErrorTrace::updateStatusAlert()
{
    if (!mAnchored || !mBeatErrValid) return;

    int tier = kTierGood;
    if (mBeatErrMs > kServiceMs)
        tier = kTierBad;
    else if (mBeatErrMs > kGoodMs)
        tier = kTierCaution;

    if (tier != mAlertTier) {
        const double iTargetMs = mBph > 0 ? 3600.0 / mBph * 1000.0 : 0.0;
        const double traceRate = iTargetMs > 0.0 ? -(mSlopeAvg / iTargetMs) * 86400.0 : 0.0;
        qInfo().noquote() << QStringLiteral(
            "[BED-status] → %1 | beatErr=%2 ms (good≤%3 caution≤%4) | engineRate=%5 s/d | traceRate=%6 s/d | beat#=%7")
                                 .arg(QString::fromUtf8(bedTierLabel(tier)))
                                 .arg(mBeatErrMs, 0, 'f', 3)
                                 .arg(kGoodMs, 0, 'f', 1)
                                 .arg(kServiceMs, 0, 'f', 1)
                                 .arg(mRateValid ? QString::number(mRateSd, 'f', 2) : QStringLiteral("--"))
                                 .arg(traceRate, 0, 'f', 2)
                                 .arg(mN);
        mAlertTier = tier;
    }
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
        // X = 측정 시작(워밍업 종료) 기준 경과 초 — 툴팁 t 와 동일 좌표.  비트 번호 n 은
        //  tic/toc 위상(n%2)·기울기(ΔE/Δn) 계산용으로만 유지하고, 화면 x 는 시간으로 그린다.
        const double xSec = (double)e.sample / sr - mPlotOriginSec;
        mPlot->graph(n % 2 == 0 ? 0 : 1)->addData(xSec, y);
        if (e.outlier) mPlot->graph(2)->addData(xSec, y);   // [이상치] rate 이상치 비트에 주황 점 겹침
        mSeek.addPoint(xSec, (double)e.sample);   // [③] 시간(초) → 절대 샘플(클릭→시점)

        // E6: 기울기 m = ΔE/Δn (ms/비트) — 검출 누락으로 비트 번호가 건너뛰어도
        //  '비트당'으로 정규화해야 rate 환산이 맞는다. 지수평활로 안정화.
        if (mHavePrevE && n > mPrevN) {
            const double m = (En - mPrevE) / (double)(n - mPrevN);
            mSlopeAvg = 0.9 * mSlopeAvg + 0.1 * m;
        }
        mPrevE = En; mPrevN = n; mHavePrevE = true;
    }

    // 메모리 바운드: 오래된 점 제거. x 가 시간(초)이므로 beat# 대신 보존 점 수 기준으로
    //  컷오프 '시각'을 구해 그 이전을 버린다(최근 kMaxDots 틱 지점 + 같은 구간 toc/이상치 유지).
    auto tic = mPlot->graph(0)->data();
    if (tic->size() > kMaxDots) {
        const double cutKey = tic->at(tic->size() - kMaxDots)->key;
        mPlot->graph(0)->data()->removeBefore(cutKey);
        mPlot->graph(1)->data()->removeBefore(cutKey);
        mPlot->graph(2)->data()->removeBefore(cutKey);
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

    // x 범위는 숨김 중에도 데이터에 맞춰 갱신해 둔다(축 갱신은 저렴) → 정지 중 타 탭 seek 가
    //  도착해도 기본(0~5) 폭으로 확대되지 않고, 그 폭 그대로 선택 지점을 중심에 보여준다.
    mPlot->xAxis->rescale();
    // 시간축은 항상 0(측정 시작)부터 시작 — 좌측 음수 구간을 두지 않는다. 우측은 최신 비트에 딱 붙는
    //  초록 gap 화살표·"gap=beat error …" 라벨이 잘리지 않도록 라벨 폭(≈160px)만큼 여백을 더한다.
    double upper = mPlot->xAxis->range().upper;
    if (const int rectW = mPlot->axisRect()->width())
        upper += qMax(0.0, upper) / (double)rectW * 160.0;
    mPlot->xAxis->setRange(0.0, qMax(upper, 1.0));
    mPlot->yAxis->setRange(-kWrapMs / 2.0, kWrapMs / 2.0);   // Y 는 랩 창 고정
    if (isVisible())
        mPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabBeatErrorTrace::onShown() { if (mPlot) mPlot->replot(); }

void TabBeatErrorTrace::onResetSession()
{
    mAnchored = false; mTstart = 0; mN = 0; mLastA = 0; mBph = 0;
    mPlotOriginSec = 0.0;
    mSeek.clear();
    mPrevE = 0.0; mPrevN = 0; mHavePrevE = false; mSlopeAvg = 0.0;
    mBeatErrMs = 0.0; mBeatErrValid = false;
    mRateSd = 0.0; mRateValid = false;
    mAlertTier = kTierUnknown;
    if (mGapLine) mGapLine->setVisible(false);
    if (mGapText) mGapText->setVisible(false);
    if (mPlot) { PlotHelpers::clearAllGraphs(mPlot); mPlot->replot(); }
}
