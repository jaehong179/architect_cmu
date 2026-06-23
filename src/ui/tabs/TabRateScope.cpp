#include "TabRateScope.h"
#include "qcustomplot.h"
#include "WaveLodHistory.h"        // 8분 이력 버퍼(pause 중 queryWindow 렌더)
#include "PerfInstrumentation.h"   // PERF_ENABLE (afterReplot→scopeReplotted 배선 게이트)
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>   // [③] 상단 RatePlot 클릭
#include <QtMath>
#include <cmath>

// ── 색상 팔레트 (TabBeatNoiseScope 계열과 통일) ──────────────────────────────
namespace Theme {
    const QColor kEnvelope  {120, 110,   0};
    const QColor kEnvFill   {235, 215,   0, 120};
    const QColor kThreshold {220, 120,   0};   // 주황 임계선
    const QColor kMarkerA   {  0, 160,   0};   // A: beat 시작 (초록)
    const QColor kMarkerC   {220,  40,  40};   // C: lock/drop  (빨강)
    const QColor kBracket   {100, 100, 120};   // 수평 브라켓 (회보라)
    const QColor kLabelBg   {255, 255, 255, 200}; // 텍스트 배경 (반투명 흰색)
    const QColor kTicColor  {200,  40,  40};   // RatePlot tic scatter
    const QColor kTocColor  { 40,  80, 200};   // RatePlot toc scatter
    const QColor kZeroLine  { 80,  80,  80};   // 0선
    const QColor kGrid      {210, 210, 215};   // 점선 그리드
}

// Scope Y축 안정화: 상승 즉시·하강 천천히 (TabBeatNoiseScope 와 동일 패턴)
static double smoothPeak(double &norm, double inst)
{
    if (inst > norm) norm = inst; else norm = 0.92 * norm + 0.08 * inst;
    if (norm < 1e-9) norm = 1e-9;
    return norm;
}

// 측정 상수.
static constexpr int    GRAPH_HISTORY_IN_SECONDS = 10;
static constexpr double ERROR_RATE_Y_SCALE       = 10.0;
static constexpr double kScopeWindowBaseSec      = 1.0;   // Scope Scale=1 → 1000 ms 창
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

    // Scope Zoom 컨트롤 (구 "Scope Scale" → 의미 명확화).
    auto *ctlRow = new QHBoxLayout();
    auto *scaleLabel = new QLabel(QStringLiteral("Scope Zoom"), this);
    QFont lf = scaleLabel->font(); lf.setBold(true); scaleLabel->setFont(lf);
    mScopeScale = new QSpinBox(this);
    mScopeScale->setMinimum(1); mScopeScale->setMaximum(8); mScopeScale->setValue(1);
    mWindowLabel = new QLabel(QStringLiteral("Window: 1.00 s"), this);
    mWindowLabel->setStyleSheet(QStringLiteral("color:#555; font-style:italic;"));
    mScopeLogView = new QCheckBox(QStringLiteral("Log Scale (dB)"), this);
    ctlRow->addWidget(scaleLabel);
    ctlRow->addWidget(mScopeScale);
    ctlRow->addWidget(mWindowLabel);
    ctlRow->addWidget(mScopeLogView);
    ctlRow->addStretch(1);
    // (정지/재생은 모든 탭에 보이는 전역 버튼 = QTabWidget 코너위젯이 담당. MainWindow 가 setPaused 호출.)
    lay->addLayout(ctlRow);

    mRatePlot  = new QCustomPlot(this);
    mScopePlot = new QCustomPlot(this);
    lay->addWidget(mRatePlot,  1);   // rate error 그래프 (stretch 1)
    lay->addWidget(mScopePlot, 2);   // 실시간 파형 스코프 (stretch 2 — 더 넓게)

    setupPlots();

    // [8분 스크롤백] 정지 중 사용자가 x축을 드래그/줌하면 그 시간창을 이력에서 다시 잘라 그린다.
    //  (라이브 중에는 mPaused=false 라 무시 — line 207 의 AlignRight setRange 도 그냥 통과.)
    connect(mScopePlot->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &) {
                if (mPaused && !mInHistoryRender) renderHistoryWindow();   // 하단만 이력 렌더(상단은 동결)
            });

    // [③] 상단 RatePlot 클릭 소스 — 정지 시 상단은 '라이브 형태 그대로 동결'(재렌더 안 함)이라 x 는
    //  라이브 beat-index 좌표. 클릭 위치 비율(좌=오래/우=최근)을 동결 시점 최신에서 역산해 절대 샘플로.
    mRateCursor = new QCPItemStraightLine(mRatePlot);
    mRateCursor->setPen(QPen(QColor(200, 0, 200), 1, Qt::DashLine));
    mRateCursor->setVisible(false);
    connect(mRatePlot, &QCustomPlot::mousePress, this, [this](QMouseEvent *e) {
        if (!mPaused || !mHistory || !mHistory->hasData()) return;   // 정지 중에만 의미
        bool found = false;
        const QCPRange kr = mRatePlot->graph(0)->getKeyRange(found);  // 동결된 점들의 x범위
        if (!found) return;
        const double x  = mRatePlot->xAxis->pixelToCoord(e->position().x());
        const double lo = kr.lower, hi = kr.upper;
        const double f  = (hi > lo) ? qBound(0.0, (x - lo) / (hi - lo), 1.0) : 1.0;
        const int    bph = (mLastBph > 0) ? mLastBph : 28800;
        // (hi-lo)=표시된 tic 개수, 1 tic = 2비트 → 창의 샘플폭 = ticN * 2 * 비트주기.
        const double winSamples = (hi - lo) * 2.0 * (3600.0 / (double)bph) * (double)mSampleRateHz;
        const double latest = (mPauseLatest > 0) ? (double)mPauseLatest : (double)mHistory->latestAbs();
        double seekSample = latest - (1.0 - f) * winSamples;
        if (seekSample < 0.0) seekSample = 0.0;
        mRateCursor->point1->setCoords(x, 0); mRateCursor->point2->setCoords(x, 1);
        mRateCursor->setVisible(true);
        mRatePlot->replot();
        emit seekRequested(seekSample);
    });

    connect(mScopeScale, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        const double windowSec = kScopeWindowBaseSec / v;
        mWindowLabel->setText(QString("Window: %1 s").arg(windowSec, 0, 'f', 2));
        syncScopeXAxis(sampleToTime(mGraphTicks));
        mScopePlot->replot(QCustomPlot::rpQueuedReplot);
    });

    connect(mScopeLogView, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            mScopePlot->yAxis->setScaleType(QCPAxis::stLogarithmic);
            mScopePlot->yAxis->setLabel(QStringLiteral("Amplitude (Log / dB)"));
            QSharedPointer<QCPAxisTickerLog> ticker = QSharedPointer<QCPAxisTickerLog>::create();
            mScopePlot->yAxis->setTicker(ticker);
        } else {
            mScopePlot->yAxis->setScaleType(QCPAxis::stLinear);
            mScopePlot->yAxis->setLabel(QStringLiteral("Amplitude"));
            QSharedPointer<QCPAxisTicker> ticker = QSharedPointer<QCPAxisTicker>::create();
            mScopePlot->yAxis->setTicker(ticker);
        }
        // 즉시 Y축 범위를 업데이트합니다.
        bool foundRange;
        QCPRange valRange = mScopePlot->graph(0)->getValueRange(foundRange, QCP::sdBoth);
        const double instMax = foundRange ? valRange.upper : 0.0;
        const double ymax = smoothPeak(mScopePeakNorm, instMax);
        if (checked) {
            mScopePlot->yAxis->setRange(0.0001, qMax(0.001, ymax * 1.12));
        } else {
            mScopePlot->yAxis->setRange(0, ymax * 1.12);
        }
        mScopePlot->replot(QCustomPlot::rpQueuedReplot);
    });

    connect(mScopePlot->xAxis,
            static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this,
            [this](const QCPRange &range) { updateScopeXAxisTicks(range); });

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
    mScopePlot->xAxis->setLabel(QStringLiteral("time (s)"));
    mScopePlot->xAxis->setNumberFormat(QStringLiteral("f"));
    QFont scopeTickFont = mScopePlot->xAxis->tickLabelFont();
    scopeTickFont.setPointSize(10);
    mScopePlot->xAxis->setTickLabelFont(scopeTickFont);
    QFont scopeLabelFont = mScopePlot->xAxis->labelFont();
    scopeLabelFont.setPointSize(10);
    mScopePlot->xAxis->setLabelFont(scopeLabelFont);
    mScopePlot->yAxis->setRange(0, 0.1);
    mScopePlot->xAxis->setTickLabels(true);
    mScopePlot->xAxis->setRange(0.0, kScopeWindowBaseSec);
    updateScopeXAxisTicks(mScopePlot->xAxis->range());
    mScopePlot->clearGraphs();
    mScopePlot->addGraph();
    pen.setWidth(1); pen.setColor(Theme::kEnvelope);
    mScopePlot->graph(0)->setPen(pen);
    mScopePlot->graph(0)->setBrush(QBrush(Theme::kEnvFill));
    mScopePlot->graph(0)->setName("Rectified");
    // [드로잉 최적화] 화면 픽셀폭 초과 표본은 QCustomPlot 이 그릴 때 자동 min/max 솎음.
    //  onWave 의 decim(=sr/48000)은 '저장 점 수'를 고레이트에서만 줄이는 반면, 이건
    //  '그리는 점 수'를 모든 레이트(48k 포함)에서 화면폭으로 바운드 → 48k 과다 드로잉 해소.
    //  (피크 보존이라 시각 변화 없음. Filter Views·Sync Sweep 과 동일 방식.)
    mScopePlot->graph(0)->setAdaptiveSampling(true);
    mScopePlot->addGraph();
    pen.setWidth(1); pen.setColor(Theme::kThreshold);
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
    // [8분 스크롤백/A안] 정지 중엔 위쪽 Rate 트렌드도 동결(스코프와 함께). 측정은 백그라운드 계속.
    if (mPaused) return;
    if (snap.liftAngle > 0) mLiftAngle = snap.liftAngle;
    if (snap.rateMaxPoints > 0) mRateMaxPoints = snap.rateMaxPoints;   // 상단 클릭 비율 매핑용

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
    // [8분 스크롤백] 정지 중엔 라이브 갱신 중단. (이력은 중앙 버퍼가 WaveSink 로 계속 누적 → 데이터 손실 없음.)
    if (mPaused) return;
    if (wave.sampleRateHz > 0) mSampleRateHz = wave.sampleRateHz;
    mLastBph = wave.bph;                 // 이력 마커 진폭 계산용

    // 엔벨로프 x = 절대 샘플 인덱스(블록 시작값). A/C 마커도 절대 markSample 을 쓰므로 항상 정렬.
    //  (자유증가 카운터를 쓰면 resume·드롭 시 마커와 어긋남 → startSample 로 고정.)
    mGraphTicks = wave.startSample;

    const double threshold = wave.onsetThreshold;
    // 고샘플레이트(예: 384kHz)에서 스코프 점이 레이트×시간으로 폭증(384만 점) → QCustomPlot 렌더가 멈춘다.
    //  검출은 풀 레이트 그대로(정확도 보존)이고, '그리기'만 줄인다:
    //  min/max 데시메이션 — decim 샘플 구간마다 최저·최고점 2개만 찍어 점 수는 줄이되 '피크'는 보존한다.
    //  x(키)는 세션 기준 초 단위 시각이라 A/C 마커 정렬도 유지. 48k(decim=1)는 샘플마다 1점(원동작 동일).
    // 성능 최적화: 48kHz에서도 최소 16배 데시메이션을 적용하여 실시간 데이터 포인트 누적을 제어하고 FPS 저하를 차단합니다.
    // Peak-Hold 방식의 min/max 보존 드로잉이므로 해상도는 여전히 날카롭게 유지됩니다.
    const int decim = qMax(16, (mSampleRateHz / 48000) * 16);   // 48k→16, 96k→32, 192k→64, 384k→128
    if (wave.env) {
        if (decim == 1) {
            for (int i = 0; i < wave.n; ++i) {
                const double t = sampleToTime(mGraphTicks);
                mScopePlot->graph(0)->addData(t, wave.env[i]);
                mScopePlot->graph(1)->addData(t, threshold);
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
                        mScopePlot->graph(0)->addData(sampleToTime(mDecimMinTick), mDecimMin);
                        mScopePlot->graph(0)->addData(sampleToTime(mDecimMaxTick), mDecimMax);
                    } else {
                        mScopePlot->graph(0)->addData(sampleToTime(mDecimMaxTick), mDecimMax);
                        mScopePlot->graph(0)->addData(sampleToTime(mDecimMinTick), mDecimMin);
                    }
                    mScopePlot->graph(1)->addData(sampleToTime(mGraphTicks - 1), threshold);
                    mDecimCount = 0;
                }
            }
        }
    }

    const double inwardLenSec = (500.0 * (mSampleRateHz / 48000.0)) / mSampleRateHz;
    for (int i = 0; i < wave.numEvents; ++i) {
        const WaveEvent &e = wave.events[i];
        const double val = sampleToTime(e.markSample);   // 표시 해상 위치(A=검출, C=onset/peak)
        if (e.type == kEventA) {
            addVerticalMarker(val, e.peak, Theme::kMarkerA, true);
            if (mHaveLastA) {
                double delta = val - mLastA;
                addHorizontalMarkerOutward(mLastA, val, e.peak / 2.0, Theme::kBracket);
                QString text = QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
                addText(mLastA + (delta / 2.0), e.peak / 2.0, text, Theme::kBracket, Qt::AlignHCenter | Qt::AlignTop);
            }
            mLastA = val; mHaveLastA = true;
        } else if (e.type == kEventC) {
            double delta = val - mLastA;
            QString text;
            if (wave.synced) {
                int Amp = qRound(amplitudeOf(mLiftAngle, delta, wave.bph));
                if (Amp < 360)
                    text = QString("%1 ms\n%2°").arg(delta * 1000.0, 0, 'f', 1).arg(Amp);
                else
                    text = QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
            } else {
                text = QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
            }
            addVerticalMarker(val, e.peak, Theme::kMarkerC, false);
            addHorizontalMarkerInward(mLastA, val, inwardLenSec, e.peak, Theme::kBracket);
            addText(val + inwardLenSec, e.peak, text, Theme::kBracket, Qt::AlignLeft | Qt::AlignTop);
        }
    }

    purgeHistory();
    syncScopeXAxis(sampleToTime(mGraphTicks));

    // Scope Y축: purgeHistory 후 남은 그래프 데이터 전체의 실제 max를 기반으로 smoothPeak 적용.
    //  block 단위 max(beat 사이 무음 시 0으로 급강) 대신 누적 데이터 max를 쓰면 Y축이 안정적이다.
    bool foundRange;
    QCPRange valRange = mScopePlot->graph(0)->getValueRange(foundRange, QCP::sdBoth);
    const double instMax = foundRange ? valRange.upper : 0.0;
    const double ymax = smoothPeak(mScopePeakNorm, instMax);
    if (mScopeLogView && mScopeLogView->isChecked()) {
        mScopePlot->yAxis->setRange(0.0001, qMax(0.001, ymax * 1.12));
    } else {
        mScopePlot->yAxis->setRange(0, ymax * 1.12);
    }

    mScopePlot->replot(QCustomPlot::rpQueuedReplot);
}

// ── [8분 스크롤백] 정지 ↔ 라이브 전환 ─────────────────────────────────────────
//  정지 = "비디오 일시정지": 현재 라이브 프레임을 그대로 동결한다(재렌더 X, 마커 유지).
//  사용자가 드래그/줌해 화면이 바뀔 때 비로소 이력으로 스크롤한다(좌표는 라이브와 일치).
void TabRateScope::setPaused(bool paused)
{
    if (paused && (!mHistory || !mHistory->hasData())) return;   // 이력 없으면 무시(전역 버튼이 원복)
    if (paused == mPaused) return;
    mPaused = paused;

    if (paused) {
        // 라이브 mGraphTicks 좌표 ↔ 이력 절대 인덱스의 고정 오프셋을 잡아둔다.
        //  이걸로 이력을 라이브와 같은 좌표로 환산 → 동결 화면에서 이음매 없이 스크롤.
        mHistOffset = (double)mHistory->latestAbs() - (double)mGraphTicks;
        mPauseLatest = mHistory->latestAbs();                 // 상단 RatePlot 클릭 비율 기준(정지 시점 최신)
        mHistActive = false;                                  // 아직 동결(드래그 전): 재렌더 안 함
        mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal); // 가로(시간) 전용 드래그/줌
        mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal);
        mScopePlot->xAxis->setLabel(QStringLiteral("PAUSED — drag/zoom to scroll back up to 8 min"));
        renderHistoryWindow();       // 하단: 엔벨로프+마커(좁은 파형 창)
        // 상단 rate 플롯은 '라이브 형태 그대로 동결' — 재렌더/재스케일 안 함(정지해도 모습 불변).
        //  seek/스크롤 시엔 커서선만 라이브 좌표 위에서 이동(데이터·축 불변). onSeek 참고.
    } else {
        // 라이브 복귀: 클리어 + 카운터 리셋 + 축 원복.
        mHistActive = false;
        if (mRateCursor) mRateCursor->setVisible(false);   // 상단 클릭 커서 숨김
        mGraphTicks = 0; mHaveLastA = false; mDecimCount = 0;
        mScopePlot->graph(0)->data()->clear();
        mScopePlot->graph(1)->data()->clear();
        mScopePlot->clearItems();
        mScopePlot->xAxis->setLabel(QStringLiteral("Time"));
        mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
        mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
        mScopePlot->replot(QCustomPlot::rpQueuedReplot);
    }
}

// 정지 중 사용자가 x축을 드래그/줌하면 그 시간창을 이력에서 잘라 그린다(라이브와 같은 좌표).
void TabRateScope::renderHistoryWindow()
{
    if (!mPaused || !mHistory || !mHistory->hasData()) return;
    const int sr = mHistory->sampleRate();
    if (sr <= 0) return;
    const QCPRange r = mScopePlot->xAxis->range();          // 라이브(mGraphTicks) 좌표
    double fromAbs = r.lower * sr + mHistOffset;            // → 이력 절대 인덱스
    double toAbs   = r.upper * sr + mHistOffset;
    if (fromAbs < 0.0) fromAbs = 0.0;
    if (toAbs <= fromAbs) return;
    const int px = qMax(64, mScopePlot->axisRect()->width());   // 화면 픽셀폭 = 목표 점 수
    QVector<double> xs, ys;
    mHistory->queryWindow((uint64_t)fromAbs, (uint64_t)toAbs, px, xs, ys);
    for (double &x : xs) x = (x - mHistOffset) / sr;        // 다시 라이브 좌표로(초 단위)
    mInHistoryRender = true;
    mHistActive = true;
    mScopePlot->clearItems();                               // 매 렌더 마커 갱신
    mScopePlot->graph(1)->data()->clear();                  // 트리거선(이력 미저장)
    mScopePlot->graph(0)->setData(xs, ys, true);
    drawHistoryMarkers((uint64_t)fromAbs, (uint64_t)toAbs); // A/C 마커 + ms/진폭 라벨 복원
    mScopePlot->yAxis->rescale();                           // y만 자동 맞춤(x는 사용자 유지)
    mScopePlot->replot(QCustomPlot::rpQueuedReplot);
    mInHistoryRender = false;
}

// 이력 이벤트로 마커 복원 — 라이브 onWave 와 동일: A→A 비트간격(ms) + A→C 진폭(ms/°).
void TabRateScope::drawHistoryMarkers(uint64_t fromAbs, uint64_t toAbs)
{
    if (!mHistory) return;
    const QVector<WaveEvent> evs = mHistory->eventsInRange(fromAbs, toAbs);
    const double inwardLenSec = (500.0 * (mSampleRateHz / 48000.0)) / mSampleRateHz;
    double lastA = 0.0; bool haveA = false;
    for (const WaveEvent &e : evs) {
        const double x = ((double)e.markSample - mHistOffset) / mSampleRateHz;       // 라이브 좌표(초)
        if (e.type == kEventA) {
            addVerticalMarker(x, e.peak, Theme::kMarkerA, true);
            if (haveA) {                                           // A→A 비트-투-비트 간격(예: 125 ms)
                const double delta = x - lastA;
                addHorizontalMarkerOutward(lastA, x, e.peak / 2.0, Theme::kBracket);
                addText(lastA + delta / 2.0, e.peak / 2.0,
                        QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1),
                        Theme::kBracket, Qt::AlignHCenter | Qt::AlignTop);
            }
            lastA = x; haveA = true;
        } else if (e.type == kEventC) {                            // A→C 진폭(6.9 ms / 303°)
            addVerticalMarker(x, e.peak, Theme::kMarkerC, false);
            if (haveA) {                                           // 선행 A 있을 때만 간격/진폭 라벨
                const double delta = x - lastA;
                QString text;
                if (mLastBph > 0) {
                    const int Amp = qRound(amplitudeOf(mLiftAngle, delta, mLastBph));
                    text = (Amp < 360) ? QString("%1 ms\n%2°").arg(delta * 1000.0, 0, 'f', 1).arg(Amp)
                                       : QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
                } else {
                    text = QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
                }
                addHorizontalMarkerInward(lastA, x, inwardLenSec, e.peak, Theme::kBracket);
                addText(x + inwardLenSec, e.peak, text, Theme::kBracket, Qt::AlignLeft | Qt::AlignTop);
            }
        }
    }
}

// [③] 트렌드에서 선택한 절대 샘플 시점으로 스코프를 이동(정지 중에만). 현재 줌 폭은 유지.
void TabRateScope::onSeek(double absSample)
{
    if (!mPaused || !mHistory || !mHistory->hasData()) return;
    const double center = (absSample - mHistOffset) / mSampleRateHz;     // 이력 절대 인덱스 → 라이브(초) 좌표
    const QCPRange r = mScopePlot->xAxis->range();
    double w = r.size();                               // 현재 보이는 폭 유지
    if (w <= 0.0) w = 1.0;                             // 안전값(~1초)
    mInHistoryRender = true;                            // setRange 가 rangeChanged 재귀 트리거하지 않도록
    mScopePlot->xAxis->setRange(center - w * 0.5, center + w * 0.5);
    mInHistoryRender = false;
    renderHistoryWindow();              // 하단: 그 시점 파형(좁은 창)

    // [③] 상단은 동결된 라이브 형태 유지 — 데이터·축 불변, 커서선만 역산 이동.
    //  graph(0)=Tic 의 x인덱스는 'tic' 단위(tic/toc 교대 → 2비트마다 1) 이므로 tic 주기(=2비트)로 나눈다.
    //  x = hi - (latest - absSample)/samplesPerTic. 동결 창 밖이면 숨김.
    if (mRateCursor) {
        bool found = false;
        const QCPRange kr = mRatePlot->graph(0)->getKeyRange(found);
        if (found && kr.upper > kr.lower) {
            const int    bph = (mLastBph > 0) ? mLastBph : 28800;
            const double samplesPerTic = 2.0 * (3600.0 / (double)bph) * (double)mSampleRateHz;
            const double latest = (mPauseLatest > 0) ? (double)mPauseLatest : (double)mHistory->latestAbs();
            const double x = kr.upper - (latest - absSample) / samplesPerTic;
            if (x >= kr.lower && x <= kr.upper) {
                mRateCursor->point1->setCoords(x, 0); mRateCursor->point2->setCoords(x, 1);
                mRateCursor->setVisible(true);
            } else {
                mRateCursor->setVisible(false);   // 동결 창 밖(더 과거) → 상단엔 표시 불가(하단이 담당)
            }
            mRatePlot->replot(QCustomPlot::rpQueuedReplot);
        }
    }
}

void TabRateScope::onResetSession()
{
    // 정지 상태였다면 라이브로 원복(축 상호작용·라벨 복구). 전역 버튼 원복은 MainWindow 담당.
    mPaused = false;
    mHistActive = false;
    mScopePlot->xAxis->setTickLabels(true);
    mScopePlot->xAxis->setLabel(QStringLiteral("Time"));
    mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);

    mGraphTicks = 0; mLastA = 0.0; mHaveLastA = false; mDecimCount = 0;
    mScopePeakNorm = 0.0;

    for (int i = 0; i < mScopePlot->graphCount(); ++i) mScopePlot->graph(i)->data()->clear();
    mScopePlot->clearItems();
    mScopePlot->xAxis->setRange(0.0, kScopeWindowBaseSec / mScopeScale->value());
    if (mScopeLogView && mScopeLogView->isChecked()) {
        mScopePlot->yAxis->setRange(0.0001, 0.1);
    } else {
        mScopePlot->yAxis->setRange(0, 0.1);
    }
    mScopePlot->replot();

    for (int i = 0; i < mRatePlot->graphCount(); ++i) mRatePlot->graph(i)->data()->clear();
    if (mRateCursor) mRateCursor->setVisible(false);   // 클릭 커서는 삭제하지 말고 숨김(clearItems 금지)
    mRatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    mRatePlot->replot();
}

// ── 마커 헬퍼 ──
void TabRateScope::addVerticalMarker(double x, double height, const QColor &color, bool isEventA)
{
    QCPItemLine *marker = new QCPItemLine(mScopePlot);
    marker->start->setCoords(x, 0.0);
    marker->end->setCoords(x, height);
    QPen pen;
    pen.setColor(color);
    pen.setWidth(isEventA ? 2 : 1);
    pen.setStyle(isEventA ? Qt::SolidLine : Qt::DashLine);   // A=실선, C=점선
    marker->setPen(pen);
}

void TabRateScope::addText(double x, double height, const QString &text, const QColor &color, Qt::Alignment alignment)
{
    QCPItemText *textLabel = new QCPItemText(mScopePlot);
    textLabel->setColor(color);
    textLabel->setFont(QFont("monospace", 9));
    textLabel->setPositionAlignment(alignment);
    textLabel->position->setType(QCPItemPosition::ptPlotCoords);
    textLabel->position->setCoords(x, height);
    textLabel->setText(text);
    textLabel->setPen(Qt::NoPen);                       // 테두리 제거
    textLabel->setBrush(QBrush(Theme::kLabelBg));       // 반투명 흰색 배경
    textLabel->setPadding(QMargins(4, 1, 4, 1));        // 여백은 padding으로
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

void TabRateScope::updateLabelVisibility()
{
    const double spanSec = mScopePlot->xAxis->range().size();
    // 가로 시간 축 범위가 1.5초를 초과하면(줌 아웃 시) 개별 비트 정보(ms/각도) 라벨과 브라켓을 숨김
    const bool showLabels = (spanSec <= 1.5);

    for (int i = 0; i < mScopePlot->itemCount(); ++i) {
        QCPAbstractItem *item = mScopePlot->item(i);
        QCPItemText *textLabel = qobject_cast<QCPItemText*>(item);
        if (textLabel) {
            textLabel->setVisible(showLabels);
        } else {
            QCPItemLine *lineItem = qobject_cast<QCPItemLine*>(item);
            if (lineItem) {
                // 수평 브라켓 감지 (시작 X와 끝 X가 다르면 수평/사선 라인, 같으면 수직 마커)
                const bool isHorizontal = qAbs(lineItem->start->coords().x() - lineItem->end->coords().x()) > 1e-9;
                if (isHorizontal) {
                    lineItem->setVisible(showLabels);
                }
            }
        }
    }
}

void TabRateScope::purgeHistory()
{
    // 보관 한도를 '점 수'가 아니라 '키 폭(=표시 시간, 초)'으로 판단한다.
    //  데시메이션과 무관하게 항상 최근 GRAPH_HISTORY_IN_SECONDS 초만 유지 → 고레이트에서도 점 수가 바운드.
    const double historySec = (double)GRAPH_HISTORY_IN_SECONDS;
    for (int i = 0; i < mScopePlot->graphCount(); ++i) {
        bool foundRange;
        QCPRange keyRange = mScopePlot->graph(i)->getKeyRange(foundRange, QCP::sdBoth);
        if (!foundRange) continue;
        const double spanSec = keyRange.upper - keyRange.lower;
        if (spanSec > historySec) {
            const double removeStart = keyRange.lower;
            const double removeEnd   = keyRange.lower + (spanSec - historySec / 2.0);
            removeMarkersAndText(removeStart, removeEnd);
            mScopePlot->graph(i)->data()->remove(removeStart, removeEnd);
        }
    }
}

double TabRateScope::sampleToTime(uint64_t sample) const
{
    return sample / (double)mSampleRateHz;
}

void TabRateScope::syncScopeXAxis(double timeEndSec)
{
    const double windowSec = kScopeWindowBaseSec / mScopeScale->value();
    if (timeEndSec <= windowSec)
        mScopePlot->xAxis->setRange(0.0, windowSec);
    else
        mScopePlot->xAxis->setRange(timeEndSec, windowSec, Qt::AlignRight);

    updateScopeXAxisTicks(mScopePlot->xAxis->range());
}

void TabRateScope::updateScopeXAxisTicks(const QCPRange &range)
{
    const double spanSec = qMax(1e-6, range.size());

    double tickStep = 1.0;
    int precision = 0;
    if (spanSec <= 0.2) {
        tickStep = 0.02;
        precision = 3;
    } else if (spanSec <= 0.5) {
        tickStep = 0.05;
        precision = 2;
    } else if (spanSec <= 1.0) {
        tickStep = 0.1;
        precision = 2;
    } else if (spanSec <= 2.0) {
        tickStep = 0.2;
        precision = 1;
    } else if (spanSec <= 5.0) {
        tickStep = 0.5;
        precision = 1;
    } else if (spanSec <= 10.0) {
        tickStep = 1.0;
        precision = 0;
    } else if (spanSec <= 20.0) {
        tickStep = 2.0;
        precision = 0;
    } else {
        tickStep = 5.0;
        precision = 0;
    }

    QSharedPointer<QCPAxisTickerFixed> ticker = QSharedPointer<QCPAxisTickerFixed>::create();
    ticker->setTickStep(tickStep);
    ticker->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    ticker->setTickOrigin(0.0);
    mScopePlot->xAxis->setTicker(ticker);
    mScopePlot->xAxis->setNumberPrecision(precision);
    updateLabelVisibility();
}
