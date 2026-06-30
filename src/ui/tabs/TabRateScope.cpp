#include "TabRateScope.h"
#include "qcustomplot.h"
#include "TrendSeek.h"             // 청록 롤리팝 커서 공용 스타일(다른 트렌드 탭과 통일)
#include "PlotHelpers.h"           // [PERF] applyFastPaint(AA off·fast polyline·adaptive)
#include "WaveLodHistory.h"        // 8분 이력 버퍼(pause 중 queryWindow 렌더)
#include "PerfInstrumentation.h"   // PERF_ENABLE (afterReplot→scopeReplotted 배선 게이트)
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>   // [③] 상단 RatePlot 클릭
#include <QtMath>
#include <cmath>
#include <limits>

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
    // [정지 후] 상·하단을 정지 진입 시점 스케일로 되돌리는 버튼(정지 중에만 활성).
    mResetZoomBtn = new QPushButton(QStringLiteral("Reset Zoom"), this);
    mResetZoomBtn->setToolTip(QStringLiteral("Restore both graphs to the view shown when STOP was pressed"));
    mResetZoomBtn->setEnabled(false);
    ctlRow->addWidget(scaleLabel);
    ctlRow->addWidget(mScopeScale);
    ctlRow->addWidget(mWindowLabel);
    ctlRow->addWidget(mScopeLogView);
    ctlRow->addWidget(mResetZoomBtn);
    ctlRow->addStretch(1);
    // (정지/재생은 모든 탭에 보이는 전역 버튼 = QTabWidget 코너위젯이 담당. MainWindow 가 setPaused 호출.)
    lay->addLayout(ctlRow);

    mRatePlot  = new QCustomPlot(this);
    mScopePlot = new QCustomPlot(this);
    lay->addWidget(mRatePlot,  1);   // rate error 그래프 (stretch 1 — 하단과 동일 높이)
    lay->addWidget(mScopePlot, 1);   // 실시간 파형 스코프 (stretch 1 — 상단과 동일 높이)

    connect(mResetZoomBtn, &QPushButton::clicked, this, [this]{ resetZoomToEntry(); });

    setupPlots();
    PlotHelpers::applyFastPaint(mScopePlot);   // [PERF] 페인트 경량화(채움/선 AA off 등) — 측정상 병목
    PlotHelpers::applyFastPaint(mRatePlot);

    // [정렬] 상·하단 플롯의 좌·우 여백을 하나의 그룹으로 묶어 x축(시간)을 픽셀 단위로 정렬한다.
    //  → y축 라벨 폭이 달라도 두 그래프의 같은 시각이 화면상 정확히 수직선상에 온다.
    auto *marginGroup = new QCPMarginGroup(mRatePlot);
    mRatePlot->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);
    mScopePlot->axisRect()->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    // [8분 스크롤백] 정지 중 사용자가 하단 x축을 드래그/줌하면 → ① 상단을 같은 시간창으로 동기,
    //  ② 그 시간창을 이력에서 다시 잘라 하단을 그린다. (라이브 중에는 mPaused=false 라 무시.)
    connect(mScopePlot->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &r) {
                if (!mPaused || mInHistoryRender || mSyncingAxes) return;   // 사용자 입력만(프로그램 설정·동기 제외)
                mSyncingAxes = true;                                        // 상단을 같은 시간창으로(쉬프트 적용)
                mRatePlot->xAxis->setRange(r.lower + mRateScopeShift, r.upper + mRateScopeShift);
                mRatePlot->replot(QCustomPlot::rpQueuedReplot);
                mSyncingAxes = false;
                renderHistoryWindow();                                      // 하단 이력 렌더
            });

    // [정지 후] 정지 중 사용자가 상단 x축을 드래그/줌하면 → 하단을 같은 시간창으로 동기 + 이력 렌더.
    connect(mRatePlot->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &r) {
                if (!mPaused || mInHistoryRender || mSyncingAxes) return;
                mSyncingAxes = true;                                        // 하단을 같은 시간창으로(쉬프트 역적용)
                mScopePlot->xAxis->setRange(r.lower - mRateScopeShift, r.upper - mRateScopeShift);
                mSyncingAxes = false;
                renderHistoryWindow();
            });

    // [정지 후] 상단 RatePlot 클릭 — 가장 가까운 tic/toc 점으로 스냅해 ① x/y 값을 떠있는 라벨로 표시,
    //  ② 그 시각의 절대 샘플로 상·하단 창을 동기 이동(같은 시간창 유지). rate 점 x 는 초(=절대샘플/sr−원점)
    //  이므로 절대 샘플 = (x + 원점) * sr 로 정확히 역산한다.
    TrendSeek::makeLollipop(mRatePlot, mRateCursor, mRateCursorHead, mRateCursorTip);   // 청록 롤리팝+툴팁(타 탭 통일)
    connect(mRatePlot, &QCustomPlot::mousePress, this, [this](QMouseEvent *e) {
        if (!mPaused || !mHistory || !mHistory->hasData()) return;   // 정지 중에만 의미
        const double sr = (double)(mHistory->sampleRate() > 0 ? mHistory->sampleRate() : mSampleRateHz);
        if (sr <= 0) return;
        const double px = e->position().x(), py = e->position().y();
        // 클릭점에 픽셀상 가장 가까운 실제 데이터 점(tic=graph0, toc=graph1)으로 스냅 → 의미 있는 (x,y).
        double bestT = mRatePlot->xAxis->pixelToCoord(px);
        double bestY = mRatePlot->yAxis->pixelToCoord(py);
        double bestD = std::numeric_limits<double>::max();
        for (int gi = 0; gi < 2; ++gi) {
            auto data = mRatePlot->graph(gi)->data();
            for (auto it = data->constBegin(); it != data->constEnd(); ++it) {
                const double dx = mRatePlot->xAxis->coordToPixel(it->key) - px;
                const double dy = mRatePlot->yAxis->coordToPixel(it->value) - py;
                const double d  = dx * dx + dy * dy;
                if (d < bestD) { bestD = d; bestT = it->key; bestY = it->value; }
            }
        }
        showRateClickLabel(bestT, bestY);                 // 떠있는 x/y 라벨(좌표는 통일된 캡처시간)
        double absSel = bestT * sr + mHistOffset;         // 통일 좌표(초) → 절대 샘플
        if (absSel < 0.0) absSel = 0.0;
        seekTo(absSel);                                   // 상·하단 창 동기 이동 + 상단 커서
        emit seekRequested(absSel);                       // seek 라벨/(전역 정지 시) 타 탭 동기
    });

    connect(mScopeScale, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        const double windowSec = kScopeWindowBaseSec / v;
        mWindowLabel->setText(QString("Window: %1 s").arg(windowSec, 0, 'f', 2));
        if (mPaused) return;   // 정지 중엔 줌/드래그·Reset 버튼이 창을 관리 → 스핀박스로 프레임 재설정 안 함
        mSweepArmed = false;   // 창 폭 변경 → 트리거락 재무장
        frameScope();
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
    mScopePlot->legend->setVisible(false);
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
    mScopePlot->legend->setVisible(false);

    // ── RatePlot ──
    mRatePlot->legend->setVisible(false);
    mRatePlot->legend->setFont(legendFont);
    mRatePlot->legend->setSelectedFont(legendFont);
    mRatePlot->legend->setSelectableParts(QCPLegend::spItems);
    mRatePlot->yAxis->setLabel("Rate Error (milliseconds)");
    mRatePlot->yAxis->setTickLabels(true);
    mRatePlot->xAxis->setLabel("Time (s)");                 // x축 = 시간(초)
    mRatePlot->yAxis->setRange(-ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
    mRatePlot->xAxis->setTickLabels(true);                  // 시간 눈금 표시(요구사항 3)
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
    // [이상치] tic/toc 이상치 점을 다른 색(주황 채움)으로 표식(평균/RLS 에선 제외된 점들).
    for (int gi = 2; gi <= 3; ++gi) {
        mRatePlot->addGraph();
        mRatePlot->graph(gi)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc,
                                              QColor(255, 140, 0), 6));   // 주황 채움(빨강/파랑과 구분)
        mRatePlot->graph(gi)->setLineStyle(QCPGraph::lsNone);
        mRatePlot->graph(gi)->removeFromLegend();
    }
    mRatePlot->legend->setVisible(false);
}

// RatePlot: snapshot 의 tic/toc 시리즈를 받아 그린다(setData 가 자체 복사).
void TabRateScope::onMeasurement(const MeasurementSnapshot &snap)
{
    // [8분 스크롤백/A안] 정지 중엔 위쪽 Rate 트렌드도 동결(스코프와 함께). 측정은 백그라운드 계속.
    if (mPaused) return;
    if (snap.liftAngle > 0) mLiftAngle = snap.liftAngle;
    if (snap.rateMaxPoints > 0) mRateMaxPoints = snap.rateMaxPoints;   // 상단 클릭 비율 매핑용
    mPlotOriginSec = snap.plotTimeOriginSec;   // rate x(초) ↔ 절대샘플 환산 원점(정지 시 이 값 고정)

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
    // [이상치] 점별 표식 — 이상치(NaN 아님)만 골라 (x,y) 로 그린다(평균/RLS 제외된 점).
    auto outliersOf = [](const QVector<double> &xv, const double *outY, int n,
                         QVector<double> &xo, QVector<double> &yo) {
        if (!outY) return;
        for (int i = 0; i < xv.size() && i < n; ++i)
            if (!qIsNaN(outY[i])) { xo.push_back(xv[i]); yo.push_back(outY[i]); }
    };
    QVector<double> tox, toy_, oox, ooy;
    outliersOf(tx, snap.rateTicOutY, snap.rateTicN, tox, toy_);
    outliersOf(ox, snap.rateTocOutY, snap.rateTocN, oox, ooy);
    mRatePlot->graph(2)->setData(tox, toy_);
    mRatePlot->graph(3)->setData(oox, ooy);
    // [요구사항 1] SoundPrint처럼 '좌→우로 점을 찍어가는' 스트립차트.
    //  표시 폭 W = ring(최근 N점)이 가득 찼을 때의 시간폭 추정(평균 점간격 × 최대점수).
    //  점이 적을수록 W가 커 우측이 비어 있어 → 왼쪽부터 채워지는 모습. 가득 차면 좌측이 빠지며 스크롤.
    //  (x값은 실제 시간(초)이라 요구사항 3의 '시간축'도 그대로 유지.)
    bool fx0 = false, fx1 = false;
    const QCPRange kx0 = mRatePlot->graph(0)->getKeyRange(fx0);
    const QCPRange kx1 = mRatePlot->graph(1)->getKeyRange(fx1);
    if (fx0 || fx1) {
        double oldest = fx0 ? kx0.lower : kx1.lower;
        double latest = fx0 ? kx0.upper : kx1.upper;
        if (fx1) { oldest = qMin(oldest, kx1.lower); latest = qMax(latest, kx1.upper); }
        const double span = latest - oldest;
        const int    N    = tx.size() + ox.size();              // 현재 표시 점 수(tic+toc)
        const double maxN = 2.0 * qMax(1, snap.rateMaxPoints);  // ring 최대(tic+toc)
        double W = span;
        if (span > 0.0 && N >= 2 && (double)N < maxN)
            W = span * (maxN - 1.0) / (double)(N - 1);          // 미충전 구간 → 우측 여백(좌→우 채움)
        if (W <= 0.0) W = 1.0;
        const double m = W * 0.02;
        mRatePlot->xAxis->setRange(oldest - m, oldest + W + m); // 좌측 끝 = 가장 오래된 점(가득 차면 스크롤)
    }
    if (isVisible())
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
    // min/max 데시메이션 — decim 구간마다 최저·최고 2점만 찍어 점 수↓·피크 보존(고레이트 FPS 보호).
    const int decim = qMax(16, (mSampleRateHz / 48000) * 16);
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

    purgeHistory();             // 항상 — 누적 그래프/마커 메모리 바운드(점 수 무관)
    if (!isVisible()) return;   // 이하 마커·스코프 프레이밍·축·replot 은 보일 때만 → 숨김 탭 부하↓
                                //  (데이터 누적/decim 은 위에서 이미 처리)

    const double inwardLenSec = (500.0 * (mSampleRateHz / 48000.0)) / mSampleRateHz;
    // 마커 높이는 비트별 peak 가 아니라 '현재 y축 범위' 기준 고정 비율 → 모든 비트가 같은 높이.
    const double H = mScopePlot->yAxis->range().upper;
    const double Htop = H * 0.85;   // 세로선·진폭 브래킷(상단)
    const double Hmid = H * 0.45;   // A 간격(125ms) 브래킷(중단)
    for (int i = 0; i < wave.numEvents; ++i) {
        const WaveEvent &e = wave.events[i];
        const double val = sampleToTime(e.markSample);
        if (e.type == kEventA) {
            addVerticalMarker(val, Htop, Theme::kMarkerA, true);
            if (mHaveLastA) {
                double delta = val - mLastA;
                addHorizontalMarkerOutward(mLastA, val, Hmid, Theme::kBracket);
                addText(mLastA + (delta / 2.0), Hmid, QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1),
                        Theme::kBracket, Qt::AlignHCenter | Qt::AlignTop);
            }
            if (!mHaveFirstTick) { mFirstTickTime = val; mHaveFirstTick = true; }   // 첫 검출 시각 기록
            mLastA = val; mHaveLastA = true;
        } else if (e.type == kEventC) {
            double delta = val - mLastA;
            QString text;
            if (wave.synced) {
                int Amp = qRound(amplitudeOf(mLiftAngle, delta, wave.bph));
                text = (Amp < 360) ? QString("%1 ms\n%2°").arg(delta * 1000.0, 0, 'f', 1).arg(Amp)
                                   : QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
            } else {
                text = QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1);
            }
            addVerticalMarker(val, Htop, Theme::kMarkerC, false);
            addHorizontalMarkerInward(mLastA, val, inwardLenSec, Htop, Theme::kBracket);
            addText(val + inwardLenSec, Htop, text, Theme::kBracket, Qt::AlignLeft | Qt::AlignTop);
        }
    }

    frameScope();   // roll(미검출 시 흐름) → 검출되면 트리거락(정지)

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

    // [§3] 스코프 페인트 ~30fps 상한 — 보일 때만, 디스플레이보다 빠른 페인트는 합침(고-fps 누적 부하↓).
    //  마커/축은 매 onWave 갱신하되 실제 페인트만 coalesce(정상 실시간 ≤30fps 면 영향 없음).
    if (isVisible() && frameDue(33))
        mScopePlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabRateScope::onShown()
{
    if (mRatePlot) mRatePlot->replot(QCustomPlot::rpQueuedReplot);
    if (mScopePlot) mScopePlot->replot(QCustomPlot::rpQueuedReplot);
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
        mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal); // 하단: 가로(시간) 전용 드래그/줌
        mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal);
        mScopePlot->xAxis->setLabel(QStringLiteral("STOPPED — drag/zoom to scroll; click top graph to seek"));
        // 상단: 정지 중 가로(시간) 드래그/줌 활성 → 하단과 같은 시간창으로 동기.
        mRatePlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
        mRatePlot->axisRect()->setRangeDrag(Qt::Horizontal);
        mRatePlot->axisRect()->setRangeZoom(Qt::Horizontal);
        if (mResetZoomBtn) mResetZoomBtn->setEnabled(true);
        enterLockedView();           // 상·하단을 같은 시간창으로 잠금 + 하단 이력 렌더 + 진입 뷰 저장
    } else {
        // Resume: restore live axis interaction only — keep waveform/markers/counters so
        // the amplitude trace continues from the pause point (seek cleanup is onResumeLive).
        mHistActive = false;
        TrendSeek::hideLollipop(mRateCursor, mRateCursorHead, mRateCursorTip);   // 상단 클릭 커서 숨김
        if (mRateClickLabel) mRateClickLabel->setVisible(false);  // 떠있는 x/y 라벨 숨김
        if (mResetZoomBtn) mResetZoomBtn->setEnabled(false);
        mRatePlot->setInteractions(QCP::Interactions());   // 상단 상호작용 끔(라이브 중 줌/팬 금지)
        mGraphTicks = 0; mHaveLastA = false; mDecimCount = 0; mSweepArmed = false; mHaveFirstTick = false;   // 재무장
        mScopePlot->graph(0)->data()->clear();
        mScopePlot->graph(1)->data()->clear();
        mScopePlot->clearItems();
        mScopePlot->xAxis->setLabel(QStringLiteral("Time"));
        mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
        mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
        mScopePlot->replot(QCustomPlot::rpQueuedReplot);
        mRatePlot->replot(QCustomPlot::rpQueuedReplot);
    }
}

// [③] seek 후 라이브 복귀 — 정지 중 seek 로 이력을 그려 둔 임시 상태를 비워 라이브로 깨끗이 복귀.
//  (seek 없이 정지만 했으면 파형이 연속이라 그대로 둬서 amplitude trace 가 이어진다.)
void TabRateScope::onResumeLive(bool seeked)
{
    if (!seeked)
        return;

    mHistActive = false;
    TrendSeek::hideLollipop(mRateCursor, mRateCursorHead, mRateCursorTip);
    if (mRateClickLabel) mRateClickLabel->setVisible(false);
    mDecimCount = 0;
    mScopePlot->graph(0)->data()->clear();
    mScopePlot->graph(1)->data()->clear();
    mScopePlot->clearItems();
    mScopePlot->xAxis->setLabel(QStringLiteral("Time"));
    mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    mScopePlot->replot(QCustomPlot::rpQueuedReplot);
}

// [정지 후] 정지 진입: 상·하단을 같은 시간창으로 잠근다. 진입 시점 뷰(상단 rate 데이터 전체 범위)를
//  Reset 대상으로 저장한다. rateX = scopeX + mRateScopeShift (상수) 로 두 좌표계가 affine 정렬된다.
void TabRateScope::enterLockedView()
{
    if (!mPaused || !mHistory) return;
    const double sr = (double)(mHistory->sampleRate() > 0 ? mHistory->sampleRate() : mSampleRateHz);
    if (sr <= 0) return;
    // 상단 rate 점 x = absSample/sr − origin,  하단 scope x = (absSample − histOffset)/sr.
    //  두 좌표차 shift = rateX − scopeX = histOffset/sr − origin (상수).
    //  → 상단 점 전체를 이만큼 평행이동하면 상·하단이 '완전히 같은 시간 좌표'(캡처 시작 기준 초)가 된다.
    //    이렇게 좌표계를 하나로 통일하면 두 x축 눈금 숫자가 정확히 일치하고 동기가 단순해진다.
    const double shift = mHistOffset / sr - mPlotOriginSec;
    const QCPRange curLive = mRatePlot->xAxis->range();   // 정지 직전 화면(라이브 스트립차트 윈도우) — 변환 전 좌표
    for (int gi = 0; gi < mRatePlot->graphCount(); ++gi) {
        auto data = mRatePlot->graph(gi)->data();
        QVector<double> xs, ys; xs.reserve(data->size()); ys.reserve(data->size());
        for (auto it = data->constBegin(); it != data->constEnd(); ++it) {
            xs.push_back(it->key - shift); ys.push_back(it->value);
        }
        mRatePlot->graph(gi)->setData(xs, ys, true);   // 평행이동(순서 보존) → alreadySorted=true
    }
    mRateScopeShift = 0.0;   // 통일 완료 → 이후 상·하단 동기는 같은 좌표를 그대로 복사

    // 정지 진입 화면 = '정지 직전 라이브 화면에 보이던 윈도우'를 통일 좌표로 시프트(그대로 멈춤).
    //  이 범위를 '초기 시작 크기'로 저장 → Reset Zoom 시 상단을 여기로 복원한다.
    double lo = curLive.lower - shift, hi = curLive.upper - shift;
    if (hi <= lo) hi = lo + 1.0;
    mEntryRateLo = lo; mEntryRateHi = hi;

    // [정지 시 하단 스케일 보존] 상단만 제자리에 유지(데이터 시프트량만큼 축도 이동 → 화면상 그대로)하고,
    //  하단 scope 의 x창·이력 렌더는 건드리지 않는다 → 정지 직전 파형 프레임을 그대로 동결한다.
    //  하단 동기·이력 렌더는 사용자가 상단을 클릭(seekTo)하거나 드래그할 때 비로소 수행한다.
    mInHistoryRender = true; mSyncingAxes = true;          // 프로그램 설정 → rangeChanged 동기/렌더 억제
    mRatePlot->xAxis->setRange(lo, hi);
    mSyncingAxes = false; mInHistoryRender = false;
    mRatePlot->replot(QCustomPlot::rpQueuedReplot);
}

// [정지 후] 절대 샘플 시점으로 상·하단 창을 동기 이동(현재 줌 폭 유지) + 상단 커서. 같은 시간창 잠금 유지.
void TabRateScope::seekTo(double absSample)
{
    if (!mPaused || !mHistory || !mHistory->hasData()) return;
    const double sr = (double)(mHistory->sampleRate() > 0 ? mHistory->sampleRate() : mSampleRateHz);
    if (sr <= 0) return;
    const double center = (absSample - mHistOffset) / sr;   // 하단(스코프) 좌표
    const QCPRange r = mScopePlot->xAxis->range();
    double w = r.size(); if (w <= 0.0) w = 1.0;             // 현재 보이는 폭 유지
    mInHistoryRender = true; mSyncingAxes = true;
    mScopePlot->xAxis->setRange(center - w * 0.5, center + w * 0.5);
    mRatePlot->xAxis->setRange(center - w * 0.5 + mRateScopeShift, center + w * 0.5 + mRateScopeShift);
    mSyncingAxes = false; mInHistoryRender = false;
    renderHistoryWindow();                                  // 하단: 그 시점 파형(좁은 창)

    const double rateX = center + mRateScopeShift;          // = absSample/sr − origin (상단 좌표)
    TrendSeek::showLollipop(mRateCursor, mRateCursorHead, mRateCursorTip, rateX,
                            QString("%1 s").arg(rateX, 0, 'f', 1));   // 선택 시각 툴팁
    mRatePlot->replot(QCustomPlot::rpQueuedReplot);
}

// [버튼] Reset Zoom — 하단은 'Scope Zoom' 창 크기로 최신 위치에 정리, 상단은 정지 진입(초기 시작) 크기로 복원.
//  상·하단 x창이 서로 달라져 동기가 일시적으로 풀릴 수 있다(의도된 동작). 이후 한쪽을 드래그/줌하거나
//  다음에 다시 정지(일시정지)하면 enterLockedView 가 두 그래프를 같은 시간창으로 재동기한다.
void TabRateScope::resetZoomToEntry()
{
    if (!mPaused || !mHistory) return;
    const double sr = (double)(mHistory->sampleRate() > 0 ? mHistory->sampleRate() : mSampleRateHz);
    if (sr <= 0) return;
    const double windowSec = kScopeWindowBaseSec / qMax(1, mScopeScale->value());   // Scope Zoom 창폭
    const double latest = ((double)mPauseLatest - mHistOffset) / sr;                 // 통일 좌표 최신(마지막 위치)
    mInHistoryRender = true; mSyncingAxes = true;          // 상·하단을 서로 다르게 설정(동기 핸들러 억제)
    mScopePlot->xAxis->setRange(latest - windowSec, latest);  // 하단: Scope Zoom 창 + 최신 위치
    mRatePlot->xAxis->setRange(mEntryRateLo, mEntryRateHi);   // 상단: 초기 시작 크기
    mSyncingAxes = false; mInHistoryRender = false;
    renderHistoryWindow();                                  // 하단 파형(스코프 창) 렌더
    mRatePlot->replot(QCustomPlot::rpQueuedReplot);
}

// [클릭] 상단 위 떠있는 x/y 값 라벨(비모달). 데이터 좌표에 고정 → 팬/줌에도 점에 붙어 있음.
void TabRateScope::showRateClickLabel(double t, double y)
{
    if (!mRateClickLabel) {
        mRateClickLabel = new QCPItemText(mRatePlot);
        mRateClickLabel->setColor(QColor(120, 0, 120));
        mRateClickLabel->setFont(QFont("monospace", 9, QFont::Bold));
        mRateClickLabel->position->setType(QCPItemPosition::ptPlotCoords);
        mRateClickLabel->setPen(QPen(QColor(120, 0, 120)));
        mRateClickLabel->setBrush(QBrush(Theme::kLabelBg));
        mRateClickLabel->setPadding(QMargins(5, 2, 5, 2));
    }
    mRateClickLabel->setText(QString("t = %1 s\nrate = %2 ms").arg(t, 0, 'f', 2).arg(y, 0, 'f', 2));
    // 라벨을 점 '옆'(기본 오른쪽, 우측 가장자리면 왼쪽)에 둔다 → 데이터를 위에서 가리지 않게.
    const QCPRange r = mRatePlot->xAxis->range();
    const bool toLeft = (r.size() > 0.0) && (t > r.lower + r.size() * 0.7);
    mRateClickLabel->setPositionAlignment(Qt::AlignVCenter | (toLeft ? Qt::AlignRight : Qt::AlignLeft));
    mRateClickLabel->position->setCoords(t, y);
    mRateClickLabel->setVisible(true);
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
    // 마커 높이 고정(라이브와 동일) — 비트별 peak 대신 y축 범위 비율.
    const double H = mScopePlot->yAxis->range().upper, Htop = H * 0.85, Hmid = H * 0.45;
    double lastA = 0.0; bool haveA = false;
    for (const WaveEvent &e : evs) {
        const double x = ((double)e.markSample - mHistOffset) / mSampleRateHz;       // 라이브 좌표(초)
        if (e.type == kEventA) {
            addVerticalMarker(x, Htop, Theme::kMarkerA, true);
            if (haveA) {                                           // A→A 비트-투-비트 간격(예: 125 ms)
                const double delta = x - lastA;
                addHorizontalMarkerOutward(lastA, x, Hmid, Theme::kBracket);
                addText(lastA + delta / 2.0, Hmid,
                        QString("%1 ms").arg(delta * 1000.0, 0, 'f', 1),
                        Theme::kBracket, Qt::AlignHCenter | Qt::AlignTop);
            }
            lastA = x; haveA = true;
        } else if (e.type == kEventC) {                            // A→C 진폭(6.9 ms / 303°)
            addVerticalMarker(x, Htop, Theme::kMarkerC, false);
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
                addHorizontalMarkerInward(lastA, x, inwardLenSec, Htop, Theme::kBracket);
                addText(x + inwardLenSec, Htop, text, Theme::kBracket, Qt::AlignLeft | Qt::AlignTop);
            }
        }
    }
}

// [③] 트렌드(자기/타 탭)에서 선택한 절대 샘플 시점으로 상·하단을 동기 이동(정지 중에만). 줌 폭은 유지.
void TabRateScope::onSeek(double absSample)
{
    seekTo(absSample);
}

void TabRateScope::onResetSession()
{
    // 정지 상태였다면 라이브로 원복(축 상호작용·라벨 복구). 전역 버튼 원복은 MainWindow 담당.
    mPaused = false;
    mHistActive = false;
    if (mResetZoomBtn) mResetZoomBtn->setEnabled(false);
    if (mRateClickLabel) mRateClickLabel->setVisible(false);   // 떠있는 x/y 라벨 숨김
    mRatePlot->setInteractions(QCP::Interactions());           // 상단 상호작용 끔(라이브)
    mScopePlot->xAxis->setTickLabels(true);
    mScopePlot->xAxis->setLabel(QStringLiteral("Time"));
    mScopePlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    mScopePlot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);

    mGraphTicks = 0; mLastA = 0.0; mHaveLastA = false; mDecimCount = 0;
    mScopePeakNorm = 0.0;
    mSweepArmed = false; mHaveFirstTick = false;   // 트리거락 고정창 초기화

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
    TrendSeek::hideLollipop(mRateCursor, mRateCursorHead, mRateCursorTip);   // 클릭 커서는 삭제하지 말고 숨김(clearItems 금지)
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

// 스코프 x창 프레이밍.
//  · roll(검출 전): 우측 끝 = 최신 샘플 → 매 블록 전진 = 좌측 스크롤(흐름).
//  · 트리거락(검출 후): 고정창 [mSweepEnd−win, mSweepEnd] 을 win 동안 그대로 고정 → 절대-x 데이터·마커가
//    멈춘다(정지). 최신이 win 만큼 더 쌓이면(latest≥mSweepEnd+win) 다음 틱(mLastA)으로 재무장 → 다음
//    구간으로. 위상이 같은 틱 경계라 패턴이 동일 → 화면 정지. 첫 검출 시 mSweepEnd≈최신이라 공백 없이 정지.
void TabRateScope::frameScope()
{
    const double winSec = kScopeWindowBaseSec / mScopeScale->value();
    const double latest = sampleToTime(mGraphTicks);
    double anchorEnd;
    // 검출(첫 틱) 후에도 '한 창(win)이 좋은 비트로 다 찰 때까지' roll 을 유지 → 비트가 왼→오로 하나하나 채워짐.
    //  그 뒤에야 잠금 → pre-sync 구간이 담긴 창에 얼거나 통째 점프(빈 화면)하는 일이 없다.
    const bool readyToLock = mTriggerLock && mHaveFirstTick && (latest >= mFirstTickTime + winSec);
    if (readyToLock) {
        if (!mSweepArmed || latest >= mSweepEnd + winSec) { mSweepEnd = mLastA; mSweepArmed = true; }
        anchorEnd = mSweepEnd;
    } else {
        mSweepArmed = false;
        anchorEnd = latest;   // roll(검출 전 + 검출 후 첫 win 동안)
    }
    syncScopeXAxis(anchorEnd);
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
