#include "TabRateScope.h"
#include "qcustomplot.h"
#include "PerfInstrumentation.h"   // PERF_ENABLE (afterReplot→scopeReplotted 배선 게이트)
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QtMath>
#include <cmath>

// 측정 상수.
static constexpr int    GRAPH_HISTORY_IN_SECONDS = 10;
static constexpr double ERROR_RATE_Y_SCALE       = 10.0;
static constexpr int    kEventA = 1;   // unlock
static constexpr int    kEventC = 2;   // drop/lock

// 진폭 공식(무상태) — MeasurementEngine::amplitude 와 동일.
static double amplitudeOf(double liftAngle, double t1Sec, double bph)
{
    return liftAngle / std::sin((2.0 * M_PI * t1Sec) / (7200.0 / bph));
}

TabRateScope::TabRateScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // Scope Scale 컨트롤.
    auto *ctlRow = new QHBoxLayout();
    auto *scaleLabel = new QLabel(QStringLiteral("Scope Scale"), this);
    QFont lf = scaleLabel->font(); lf.setBold(true); scaleLabel->setFont(lf);
    mScopeScale = new QSpinBox(this);
    mScopeScale->setMinimum(1); mScopeScale->setMaximum(8); mScopeScale->setValue(2);
    ctlRow->addWidget(scaleLabel);
    ctlRow->addWidget(mScopeScale);
    ctlRow->addStretch(1);
    lay->addLayout(ctlRow);

    mRatePlot  = new QCustomPlot(this);
    mScopePlot = new QCustomPlot(this);
    lay->addWidget(mRatePlot, 1);
    lay->addWidget(mScopePlot, 1);

    setupPlots();

#if PERF_ENABLE
    // ScopePlot 의 실제 paint 완료 → perf 신호(MainWindow 가 disp_paint/e2e_full/paint_fps 기록).
    //  계측 OFF 면 매 replot 마다의 시그널 방출 자체를 제거.
    connect(mScopePlot, &QCustomPlot::afterReplot, this, &TabRateScope::scopeReplotted);
#endif
}

// RatePlot/ScopePlot 그래프 설정.
void TabRateScope::setupPlots()
{
    QFont legendFont = font();
    legendFont.setPointSize(10);
    QPen pen;

    // ── ScopePlot ──
    mScopePlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    mScopePlot->legend->setVisible(true);
    mScopePlot->legend->setFont(legendFont);
    mScopePlot->legend->setSelectedFont(legendFont);
    mScopePlot->legend->setSelectableParts(QCPLegend::spItems);
    mScopePlot->yAxis->setLabel("Amplitude");
    mScopePlot->xAxis->setLabel("Time");
    mScopePlot->yAxis->setRange(0, 0.1);
    mScopePlot->xAxis->setTickLabels(false);
    mScopePlot->clearGraphs();
    mScopePlot->addGraph();
    pen.setWidth(1); pen.setColor(Qt::blue);
    mScopePlot->graph(0)->setPen(pen);
    mScopePlot->graph(0)->setBrush(QBrush(QColor(0, 0, 255, 20)));
    mScopePlot->graph(0)->setName("Rectified");
    // [드로잉 최적화] 화면 픽셀폭 초과 표본은 QCustomPlot 이 그릴 때 자동 min/max 솎음.
    //  onWave 의 decim(=sr/48000)은 '저장 점 수'를 고레이트에서만 줄이는 반면, 이건
    //  '그리는 점 수'를 모든 레이트(48k 포함)에서 화면폭으로 바운드 → 48k 과다 드로잉 해소.
    //  (피크 보존이라 시각 변화 없음. Filter Views·Sync Sweep 과 동일 방식.)
    mScopePlot->graph(0)->setAdaptiveSampling(true);
    mScopePlot->addGraph();
    pen.setWidth(1); pen.setColor(Qt::red);
    mScopePlot->graph(1)->setPen(pen);
    mScopePlot->graph(1)->setName("Trigger");
    mScopePlot->graph(1)->setAdaptiveSampling(true);
    mScopePlot->legend->setVisible(true);

    // ── RatePlot ──
    mRatePlot->legend->setVisible(true);
    mRatePlot->legend->setFont(legendFont);
    mRatePlot->legend->setSelectedFont(legendFont);
    mRatePlot->legend->setSelectableParts(QCPLegend::spItems);
    mRatePlot->yAxis->setLabel("Rate Error (milliseconds)");
    mRatePlot->yAxis->setTickLabels(true);
    mRatePlot->xAxis->setLabel("Time");
    mRatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    mRatePlot->xAxis->setTickLabels(false);
    mRatePlot->clearGraphs();
    mRatePlot->addGraph();
    mRatePlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 3));
    mRatePlot->graph(0)->setLineStyle(QCPGraph::lsNone);
    mRatePlot->graph(0)->setPen(QPen(Qt::red));
    mRatePlot->graph(0)->setName("Tic Rate");
    mRatePlot->addGraph();
    mRatePlot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 3));
    mRatePlot->graph(1)->setLineStyle(QCPGraph::lsNone);
    mRatePlot->graph(1)->setPen(QPen(Qt::blue));
    mRatePlot->graph(1)->setName("Toc Rate");
    mRatePlot->legend->setVisible(true);
}

// RatePlot: snapshot 의 tic/toc 시리즈를 받아 그린다(setData 가 자체 복사).
void TabRateScope::onMeasurement(const MeasurementSnapshot &snap)
{
    if (snap.liftAngle > 0) mLiftAngle = snap.liftAngle;

    QVector<double> tx, ty, ox, oy;
    if (snap.rateTicN > 0 && snap.rateTicX && snap.rateTicY) {
        tx = QVector<double>(snap.rateTicX, snap.rateTicX + snap.rateTicN);
        ty = QVector<double>(snap.rateTicY, snap.rateTicY + snap.rateTicN);
    }
    if (snap.rateTocN > 0 && snap.rateTocX && snap.rateTocY) {
        ox = QVector<double>(snap.rateTocX, snap.rateTocX + snap.rateTocN);
        oy = QVector<double>(snap.rateTocY, snap.rateTocY + snap.rateTocN);
    }
    mRatePlot->graph(0)->setData(tx, ty);
    mRatePlot->graph(1)->setData(ox, oy);
    if (snap.rateMaxPoints > 0) mRatePlot->xAxis->setRange(0, snap.rateMaxPoints);
    mRatePlot->replot(QCustomPlot::rpQueuedReplot);
}

// ScopePlot: 엔벨로프 + 임계선 + A/C 마커.
void TabRateScope::onWave(const WaveBlock &wave)
{
    if (wave.sampleRateHz > 0) mSampleRateHz = wave.sampleRateHz;

    const double threshold = wave.onsetThreshold;
    // 고샘플레이트(예: 384kHz)에서 스코프 점이 레이트×시간으로 폭증(384만 점) → QCustomPlot 렌더가 멈춘다.
    //  검출은 풀 레이트 그대로(정확도 보존)이고, '그리기'만 줄인다:
    //  min/max 데시메이션 — decim 샘플 구간마다 최저·최고점 2개만 찍어 점 수는 줄이되 '피크'는 보존한다.
    //  x(키)는 실제 샘플 인덱스라 A/C 마커 정렬도 유지. 48k(decim=1)는 샘플마다 1점(원동작 동일).
    const int decim = qMax(1, mSampleRateHz / 48000);   // 48k→1, 96k→2, 192k→4, 384k→8
    if (wave.env) {
        if (decim == 1) {
            for (int i = 0; i < wave.n; ++i) {
                mScopePlot->graph(0)->addData((double)mGraphTicks, wave.env[i]);
                mScopePlot->graph(1)->addData((double)mGraphTicks, threshold);
                mGraphTicks++;
            }
        } else {
            for (int i = 0; i < wave.n; ++i) {
                const float v = wave.env[i];
                if (mDecimCount == 0) { mDecimMin = mDecimMax = v; mDecimMinTick = mDecimMaxTick = mGraphTicks; }
                else {
                    if (v < mDecimMin) { mDecimMin = v; mDecimMinTick = mGraphTicks; }
                    if (v > mDecimMax) { mDecimMax = v; mDecimMaxTick = mGraphTicks; }
                }
                mGraphTicks++;
                if (++mDecimCount >= decim) {
                    // 구간의 최저·최고를 x(시간) 순서로 2점 추가 → 피크 보존.
                    if (mDecimMinTick <= mDecimMaxTick) {
                        mScopePlot->graph(0)->addData((double)mDecimMinTick, mDecimMin);
                        mScopePlot->graph(0)->addData((double)mDecimMaxTick, mDecimMax);
                    } else {
                        mScopePlot->graph(0)->addData((double)mDecimMaxTick, mDecimMax);
                        mScopePlot->graph(0)->addData((double)mDecimMinTick, mDecimMin);
                    }
                    mScopePlot->graph(1)->addData((double)(mGraphTicks - 1), threshold);
                    mDecimCount = 0;
                }
            }
        }
    }

    const double inwardLen = 500.0 * (mSampleRateHz / 48000.0);
    for (int i = 0; i < wave.numEvents; ++i) {
        const WaveEvent &e = wave.events[i];
        const double val = (double)e.markSample;   // 표시 해상 위치(A=검출, C=onset/peak)
        if (e.type == kEventA) {
            addVerticalMarker(val, e.peak, Qt::green);
            if (mHaveLastA) {
                double delta = val - mLastA;
                addHorizontalMarkerOutward(mLastA, val, e.peak / 2.0, Qt::black);
                QString text = QString(" %1 ms ").arg(delta * 1000.0 / mSampleRateHz, 0, 'f', 2);
                addText(mLastA + (delta / 2.0), e.peak / 2.0, text, Qt::black, Qt::AlignHCenter | Qt::AlignTop);
            }
            mLastA = val; mHaveLastA = true;
        } else if (e.type == kEventC) {
            double delta = val - mLastA;
            QString text;
            if (wave.synced) {
                int Amp = qRound(amplitudeOf(mLiftAngle, delta / mSampleRateHz, wave.bph));
                if (Amp < 360)
                    text = QString(" %1 ms\n%2°").arg(delta * 1000.0 / mSampleRateHz, 0, 'f', 1).arg(Amp);
                else
                    text = QString(" %1 ms ").arg(delta * 1000.0 / mSampleRateHz, 0, 'f', 1);
            } else {
                text = QString(" %1 ms ").arg(delta * 1000.0 / mSampleRateHz, 0, 'f', 1);
            }
            addVerticalMarker(val, e.peak, Qt::red);
            addHorizontalMarkerInward(mLastA, val, inwardLen, e.peak, Qt::black);
            addText(val + inwardLen, e.peak, text, Qt::black, Qt::AlignLeft | Qt::AlignTop);
        }
    }

    purgeHistory();
    mScopePlot->xAxis->setRange((double)mGraphTicks, (double)mSampleRateHz / mScopeScale->value(), Qt::AlignRight);
    mScopePlot->yAxis->rescale();
    mScopePlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabRateScope::onResetSession()
{
    mGraphTicks = 0; mLastA = 0.0; mHaveLastA = false; mDecimCount = 0;

    for (int i = 0; i < mScopePlot->graphCount(); ++i) mScopePlot->graph(i)->data()->clear();
    mScopePlot->clearItems();
    mScopePlot->replot();

    for (int i = 0; i < mRatePlot->graphCount(); ++i) mRatePlot->graph(i)->data()->clear();
    mRatePlot->clearItems();
    mRatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    mRatePlot->replot();
}

// ── 마커 헬퍼 ──
void TabRateScope::addVerticalMarker(double x, double height, const QColor &color)
{
    QCPItemLine *marker = new QCPItemLine(mScopePlot);
    marker->start->setCoords(x, 0.0);
    marker->end->setCoords(x, height);
    QPen pen; pen.setColor(color); pen.setWidth(2); pen.setStyle(Qt::DashLine);
    marker->setPen(pen);
}

void TabRateScope::addText(double x, double height, const QString &text, const QColor &color, Qt::Alignment alignment)
{
    QCPItemText *textLabel = new QCPItemText(mScopePlot);
    textLabel->setColor(color);
    textLabel->setFont(QFont(font().family(), 10));
    textLabel->setPositionAlignment(alignment);
    textLabel->position->setType(QCPItemPosition::ptPlotCoords);
    textLabel->position->setCoords(x, height);
    textLabel->setText(text);
    textLabel->setPen(QPen(color));
}

void TabRateScope::addHorizontalMarkerInward(double xLeft, double xRight, double Length, double Height, const QColor &Color)
{
    QPen pen; pen.setColor(Color); pen.setWidth(1); pen.setStyle(Qt::SolidLine);
    QCPItemLine *markerLeft = new QCPItemLine(mScopePlot);
    markerLeft->start->setCoords(xLeft - Length, Height);
    markerLeft->end->setCoords(xLeft, Height);
    markerLeft->setHead(QCPLineEnding::esSpikeArrow);
    markerLeft->setPen(pen);
    QCPItemLine *markerRight = new QCPItemLine(mScopePlot);
    markerRight->start->setCoords(xRight, Height);
    markerRight->end->setCoords(xRight + Length, Height);
    markerRight->setTail(QCPLineEnding::esSpikeArrow);
    markerRight->setPen(pen);
}

void TabRateScope::addHorizontalMarkerOutward(double xLeft, double xRight, double Height, const QColor &Color)
{
    QCPItemLine *marker = new QCPItemLine(mScopePlot);
    marker->start->setCoords(xLeft, Height);
    marker->end->setCoords(xRight, Height);
    QPen pen; pen.setColor(Color); pen.setWidth(1); pen.setStyle(Qt::SolidLine);
    marker->setHead(QCPLineEnding::esSpikeArrow);
    marker->setTail(QCPLineEnding::esSpikeArrow);
    marker->setPen(pen);
}

void TabRateScope::removeMarkersAndText(double rangeMin, double rangeMax)
{
    for (int i = mScopePlot->itemCount() - 1; i >= 0; --i) {
        QCPAbstractItem *baseItem = mScopePlot->item(i);
        QCPItemLine *lineItem = qobject_cast<QCPItemLine*>(baseItem);
        QCPItemText *textLabel = qobject_cast<QCPItemText*>(baseItem);
        if (lineItem) {
            double startKey = lineItem->start->coords().x();
            double endKey = lineItem->end->coords().x();
            if ((startKey >= rangeMin && startKey <= rangeMax) ||
                (endKey >= rangeMin && endKey <= rangeMax))
                mScopePlot->removeItem(lineItem);
        } else if (textLabel) {
            double Key = textLabel->position->coords().x();
            if (Key >= rangeMin && Key <= rangeMax)
                mScopePlot->removeItem(textLabel);
        }
    }
}

void TabRateScope::purgeHistory()
{
    // 보관 한도를 '점 수'가 아니라 '키 폭(=샘플 인덱스 폭 = 표시 시간)'으로 판단한다.
    //  데시메이션과 무관하게 항상 최근 GRAPH_HISTORY_IN_SECONDS 초만 유지 → 고레이트에서도 점 수가 바운드.
    //  (48k/decim=1 에서는 키 폭≈점 수라 기존 동작과 동일.)
    const double historyKeys = (double)GRAPH_HISTORY_IN_SECONDS * mSampleRateHz;
    for (int i = 0; i < mScopePlot->graphCount(); ++i) {
        bool foundRange;
        QCPRange keyRange = mScopePlot->graph(i)->getKeyRange(foundRange, QCP::sdBoth);
        if (!foundRange) continue;
        double NumKeys = keyRange.upper - keyRange.lower;
        if (NumKeys > historyKeys) {
            double RemoveStart = keyRange.lower;
            double RemoveEnd   = keyRange.lower + (NumKeys - historyKeys / 2.0);
            removeMarkersAndText(RemoveStart, RemoveEnd);
            mScopePlot->graph(i)->data()->remove(RemoveStart, RemoveEnd);
        }
    }
}
