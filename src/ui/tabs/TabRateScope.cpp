#include "TabRateScope.h"
#include "qcustomplot.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QtMath>
#include <cmath>

// 측정 상수(구 MainWindow 매크로와 동일 값).
static constexpr int    GRAPH_HISTORY_IN_SECONDS = 10;
static constexpr double ERROR_RATE_Y_SCALE       = 10.0;
static constexpr int    kEventA = 1;   // unlock
static constexpr int    kEventC = 2;   // drop/lock

// 진폭 공식(무상태) — 구 MainWindow::Amplitude / MeasurementEngine::amplitude 와 동일.
static double amplitudeOf(double liftAngle, double t1Sec, double bph)
{
    return liftAngle / std::sin((2.0 * M_PI * t1Sec) / (7200.0 / bph));
}

TabRateScope::TabRateScope(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // Scope Scale 컨트롤(구 .ui ScopeScaleSpinBox/Label: min1 max8 val2).
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

    // ScopePlot 의 실제 paint 완료 → perf 신호(MainWindow 가 disp_paint/e2e_full/paint_fps 기록).
    connect(mScopePlot, &QCustomPlot::afterReplot, this, &TabRateScope::scopeReplotted);
}

// 구 MainWindow::CreateGraphs 의 RatePlot/ScopePlot 설정 그대로.
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
    mScopePlot->addGraph();
    pen.setWidth(1); pen.setColor(Qt::red);
    mScopePlot->graph(1)->setPen(pen);
    mScopePlot->graph(1)->setName("Trigger");
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

// ScopePlot: 엔벨로프 + 임계선 + A/C 마커. (구 MainWindow::ProcessSamples 의 ScopePlot 부분)
void TabRateScope::onWave(const WaveBlock &wave)
{
    if (wave.sampleRateHz > 0) mSampleRateHz = wave.sampleRateHz;

    const double threshold = wave.onsetThreshold;
    if (wave.env) {
        for (int i = 0; i < wave.n; ++i) {
            mScopePlot->graph(0)->addData(mGraphTicks, wave.env[i]);
            mScopePlot->graph(1)->addData(mGraphTicks, threshold);
            mGraphTicks++;
        }
    }

    const double inwardLen = 500.0 * (mSampleRateHz / 48000.0);   // 구 INWARD_MARKER_LENGTH
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
    mGraphTicks = 0; mLastA = 0.0; mHaveLastA = false;

    for (int i = 0; i < mScopePlot->graphCount(); ++i) mScopePlot->graph(i)->data()->clear();
    mScopePlot->clearItems();
    mScopePlot->replot();

    for (int i = 0; i < mRatePlot->graphCount(); ++i) mRatePlot->graph(i)->data()->clear();
    mRatePlot->clearItems();
    mRatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    mRatePlot->replot();
}

// ── 마커 헬퍼 (구 MainWindow 동명 메서드 — 대상 Plot 을 mScopePlot 으로 고정) ──
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
    for (int i = 0; i < mScopePlot->graphCount(); ++i) {
        if (mScopePlot->graph(i)->data()->size() > (GRAPH_HISTORY_IN_SECONDS * mSampleRateHz)) {
            bool foundRange;
            QCPRange keyRange = mScopePlot->graph(i)->getKeyRange(foundRange, QCP::sdBoth);
            if (foundRange) {
                double minKey = keyRange.lower;
                double maxKey = keyRange.upper;
                double NumKeys = maxKey - minKey;
                double NumToRemove = NumKeys - ((GRAPH_HISTORY_IN_SECONDS * mSampleRateHz) / 2);
                double RemoveStart = minKey;
                double RemoveEnd = minKey + NumToRemove;
                removeMarkersAndText(RemoveStart, RemoveEnd);
                mScopePlot->graph(i)->data()->remove(RemoveStart, RemoveEnd);
            }
        }
    }
}
