#include "TabWaveformCompare.h"
#include "WaveLodHistory.h"   // [③] seek replay
#include "qcustomplot.h"
#include <QPushButton>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabWaveformCompare::TabWaveformCompare(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);

    auto *ctl = new QHBoxLayout();
    ctl->addStretch(1);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear"), this);
    ctl->addWidget(clearBtn);
    lay->addLayout(ctl);
    connect(clearBtn, &QPushButton::clicked, this, &TabWaveformCompare::onResetSession);

    // ── 좌측 paperstrip (검정 배경, tic 흰점 / tac 청록점, 세로=시간) ──
    mPaper = new QCustomPlot(this);
    mPaper->setBackground(QBrush(Qt::black));
    const QFont paf(QStringLiteral("sans"), 7);
    for (QCPAxis *ax : {mPaper->xAxis, mPaper->yAxis}) {
        ax->setBasePen(QPen(Qt::white)); ax->setTickPen(QPen(Qt::white));
        ax->setSubTickPen(QPen(Qt::white)); ax->setTickLabelColor(Qt::white); ax->setLabelColor(Qt::white);
        ax->setLabelFont(paf); ax->setTickLabelFont(paf);
    }
    mPaper->axisRect()->setMargins(QMargins(2, 2, 2, 2));
    mPaper->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom);
    mPaper->xAxis->setTickLabels(false); mPaper->xAxis->setRange(0, 1);
    mPaper->xAxis->setLabel(QStringLiteral("Paperstrip"));   // 그래프 이름
    mPaper->yAxis->setLabel(QStringLiteral("time(s) ↑latest"));   // 세로 = 시간
    mPaper->addGraph();   // graph0 = onset 점 (tic·tac 합산 — beat error 표시 제거)
    mPaper->graph(0)->setLineStyle(QCPGraph::lsNone);
    mPaper->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(200, 200, 200), 3.5));
    mPaper->graph(0)->setName(QStringLiteral("onset"));
    mPaper->addGraph();   // graph1 = rate 이상치(주황)
    mPaper->graph(1)->setLineStyle(QCPGraph::lsNone);
    mPaper->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(255, 140, 0), 6));
    mPaper->graph(1)->setName(QStringLiteral("outlier"));
    mPaper->legend->setVisible(true);
    mPaper->legend->setBrush(QBrush(QColor(0, 0, 0, 190)));
    mPaper->legend->setBorderPen(QPen(QColor(120, 120, 120)));
    mPaper->legend->setTextColor(Qt::white);
    mPaper->legend->setFont(QFont(QStringLiteral("sans"), 7));
    mPaper->legend->setIconSize(9, 9);
    mPaper->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);  // 우상단 모서리(데이터 안 가림)
    mPaper->setMinimumWidth(210); mPaper->setMaximumWidth(290);

    // ── 우측: tic·toc 합본 평균파형 + period ──
    mTicToc = makeScope(); mPeriod = makeScope();
    // 합본 패널: 같은 x(ms)·도(°) 축 공유. Tic 은 '위 절반(중심 +0.5)', Toc 은 '아래 절반(중심 −0.5)'에
    //  각각 미러 파형으로 분리해 그린다. 그래프 4개 = 밴드 2개(각 up/dn 사이를 채움).
    mTicToc->addGraph();   // graph3 추가(기본 3개 → 4개)
    const QColor kYellow(255, 230, 0);
    const QColor kYellowFill(255, 230, 0, 210);
    // Tic 밴드: graph0=up(outline), graph1=dn(up↔dn 채움). Toc 밴드: graph2=up, graph3=dn.
    mTicToc->graph(0)->setPen(QPen(kYellow, 1)); mTicToc->graph(0)->setBrush(Qt::NoBrush);
    mTicToc->graph(0)->setChannelFillGraph(nullptr);
    mTicToc->graph(1)->setPen(QPen(kYellow, 1)); mTicToc->graph(1)->setBrush(kYellowFill);
    mTicToc->graph(1)->setChannelFillGraph(mTicToc->graph(0));
    mTicToc->graph(2)->setPen(QPen(kYellow, 1)); mTicToc->graph(2)->setBrush(Qt::NoBrush);
    mTicToc->graph(2)->setChannelFillGraph(nullptr);
    mTicToc->graph(3)->setPen(QPen(kYellow, 1)); mTicToc->graph(3)->setBrush(kYellowFill);
    mTicToc->graph(3)->setChannelFillGraph(mTicToc->graph(2));
    mTicToc->yAxis->setRange(-1.05, 1.05);
    mPeriod->xAxis->setLabel(QStringLiteral("ms (0=Tick T1)"));
    mPeriod->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    for (int g : {1, 2}) {                                       // Period 파형도 노란색(미러: graph1=+, graph2=−)
        mPeriod->graph(g)->setPen(QPen(kYellow, 1));
        mPeriod->graph(g)->setBrush(kYellowFill);
    }

    // ── paperstrip 데이터 누적은 유지하되 패널은 숨김 (beat error 표시 제거로 UI에서는 불표시)
    mPaper->hide();

    // 2개 패널 수직 열: Tic(위)+Toc(아래) 합본 / Period — 높이 비중 6:4
    auto *body = new QVBoxLayout();
    body->addWidget(mTicToc, 6);
    body->addWidget(mPeriod, 4);
    lay->addLayout(body, 1);
}

// 검정 배경 흰 미러 파형 패널: graph0=베이스라인, graph1=+|mag|, graph2=−|mag|.
QCustomPlot *TabWaveformCompare::makeScope()
{
    auto *plot = new QCustomPlot(this);
    plot->setBackground(QBrush(Qt::black));
    const QFont axisFont(QStringLiteral("sans"), 7);
    for (QCPAxis *ax : {plot->xAxis, plot->yAxis}) {
        ax->setBasePen(QPen(Qt::white)); ax->setTickPen(QPen(Qt::white));
        ax->setSubTickPen(QPen(Qt::white)); ax->setTickLabelColor(Qt::white); ax->setLabelColor(Qt::white);
        ax->setLabelFont(axisFont); ax->setTickLabelFont(axisFont);
    }
    plot->axisRect()->setMargins(QMargins(2, 2, 2, 2));             // 플롯 영역 최대화
    plot->axisRect()->setAutoMargins(QCP::msLeft | QCP::msBottom | QCP::msTop);
    plot->yAxis->setTickLabels(false); plot->yAxis->setRange(-1.05, 1.05);
    plot->yAxis->setLabel(QStringLiteral("|amplitude|"));
    plot->xAxis->setRange(-kPreMs, kPostMs); plot->xAxis->setLabel(QStringLiteral("ms (0=C)"));
    QSharedPointer<QCPAxisTickerFixed> ticker(new QCPAxisTickerFixed);
    ticker->setTickStep(5.0); ticker->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    plot->xAxis->setTicker(ticker);
    plot->addGraph(); plot->graph(0)->setPen(Qt::NoPen);
    plot->addGraph(); plot->graph(1)->setPen(QPen(Qt::white, 1)); plot->graph(1)->setBrush(QColor(255,255,255,230)); plot->graph(1)->setChannelFillGraph(plot->graph(0));
    plot->addGraph(); plot->graph(2)->setPen(QPen(Qt::white, 1)); plot->graph(2)->setBrush(QColor(255,255,255,230)); plot->graph(2)->setChannelFillGraph(plot->graph(0));
    return plot;
}

void TabWaveformCompare::onMeasurement(const MeasurementSnapshot &s)
{
    if (s.liftAngle > 0) mLiftAngle = s.liftAngle;
    mAmplitudeValid = s.amplitudeValid; mAmplitudeDeg = s.amplitudeDeg;
}

void TabWaveformCompare::onShown() { render(); }

void TabWaveformCompare::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        mBuf.configure(w.sampleRateHz * 2);
        mRawBuf.configure(w.sampleRateHz * 2);
        mConfigured = true;
    }
    mBuf.push(w);
    if (w.raw && w.rawN > 0) {
        WaveBlock rb; rb.env = w.raw; rb.n = w.rawN; rb.startSample = w.rawStart;
        rb.sampleRateHz = w.sampleRateHz; rb.bph = w.bph; rb.synced = w.synced;
        mRawBuf.push(rb);
    }
    accumulate(w);
    if (isVisible()) render();
}

// [③] 정지 중 트렌드 클릭 → 그 시점 주변 구간을 이력에서 복원해 표시(파형부만; 평균/paperstrip 누적은 동결).
void TabWaveformCompare::onSeek(double absSample)
{
    if (!mHistory || !mHistory->hasData()) return;
    const int sr = mHistory->sampleRate();
    if (sr <= 0) return;
    if (!mConfigured) { mBuf.configure(sr * 2); mRawBuf.configure(sr * 2); mConfigured = true; }
    mBuf.clear();
    mRawBuf.clear();
    WaveLodHistory::replayInto(*mHistory, mBuf, &mRawBuf, absSample, sr * 2);
    render();
}

// C 정렬 tic/toc 코히어런트 평균(EMA) + A onset paperstrip 누적.
void TabWaveformCompare::accumulate(const WaveBlock &)
{
    const int sr = mRawBuf.sampleRate(); if (sr <= 0) return;
    const int pre  = (int)(kPreMs  / 1000.0 * sr);
    const int post = (int)(kPostMs / 1000.0 * sr);
    if (mWindowLen <= 0) mWindowLen = pre + post;
    const uint64_t latest = mRawBuf.latestAbs();
    const QVector<WaveEvent> events = mBuf.eventsInRange(mBuf.oldestAbs(), latest);
    for (const WaveEvent &event : events) {
        if (event.type == 1) {                                   // A onset → paperstrip
            if (mAOnsetHist.isEmpty() || event.sample > mAOnsetHist.last()) {
                if (!mHaveAnchor) { mAnchor = event.sample; mHaveAnchor = true; }
                mAOnsetHist.push_back(event.sample);
                mAOnsetOutlier.push_back(event.outlier);   // [이상치] 이벤트에 박힌 플래그 그대로 보관
                while (mAOnsetHist.size() > kPaperHist) { mAOnsetHist.remove(0); mAOnsetOutlier.remove(0); }
            }
        } else if (event.type == 2) {                            // C peak → tic/toc 평균
            if (mHaveLastC && event.sample <= mLastC) continue;
            if (event.sample < (uint64_t)pre) continue;
            if (event.sample + (uint64_t)post > latest) continue;    // 아직 미래 → 다음 블록 대기
            QVector<double> window; mRawBuf.copyRange(event.sample - (uint64_t)pre, mWindowLen, window);
            const bool tic = (mCCount % 2 == 0);
            QVector<double> &avg = tic ? mTicAvg : mTocAvg;
            bool &init = tic ? mTicInit : mTocInit;
            if (avg.size() != mWindowLen || !init) { avg = window; init = true; }
            else { const double emaAlpha = 0.15; for (int i = 0; i < mWindowLen; ++i) avg[i] = (1.0 - emaAlpha) * avg[i] + emaAlpha * window[i]; }
            mLastC = event.sample; mHaveLastC = true; ++mCCount;
        }
    }
}

// Tic(위 밴드, 중심 +0.5) 과 Toc(아래 밴드, 중심 −0.5) 를 하나의 패널에 분리해 그린다 — x(ms)·도(°) 축 공유.
void TabWaveformCompare::drawTicTocPanel()
{
    QCustomPlot *plot = mTicToc;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int pre = (int)(kPreMs / 1000.0 * sr);
    constexpr double kCenterTic = 0.5, kCenterToc = -0.5, kHalf = 0.48;   // 각 밴드 중심·반높이(가득 채움)

    // avg → 밴드 미러 파형(up/dn). 각자 90퍼센타일로 정규화해 자기 절반을 가득 채움.
    auto buildBand = [&](const QVector<double> &avg, double center,
                         QVector<double> &x, QVector<double> &up, QVector<double> &dn) -> bool {
        const int n = avg.size();
        if (n < 2) return false;
        QVector<double> magnitude(n); for (int i = 0; i < n; ++i) magnitude[i] = std::fabs(avg[i]);
        // 실제 최대값으로 정규화 → 가장 큰 피크가 밴드 끝에 딱 닿고 그 이하는 실제 모양대로(포화·잘림 없음).
        double peak = 1e-9; for (double m : magnitude) peak = std::max(peak, m);
        x.resize(n); up.resize(n); dn.resize(n);
        for (int i = 0; i < n; ++i) {
            x[i] = 1000.0 * (i - pre) / sr;                      // ms, 0 = C
            const double mag = (magnitude[i] / peak) * kHalf;    // [0,1]·반높이 → 클램프 불필요
            up[i] = center + mag; dn[i] = center - mag;          // 밴드 중심 기준 미러
        }
        return true;
    };

    QVector<double> xT, upT, dnT, xC, upC, dnC;
    const bool hasTic = buildBand(mTicAvg, kCenterTic, xT, upT, dnT);
    const bool hasToc = buildBand(mTocAvg, kCenterToc, xC, upC, dnC);
    if (!hasTic && !hasToc) {
        for (int g = 0; g < plot->graphCount(); ++g) plot->graph(g)->data()->clear();
        plot->clearItems(); plot->replot(); return;
    }
    if (hasTic) { plot->graph(0)->setData(xT, upT, true); plot->graph(1)->setData(xT, dnT, true); }   // Tic 밴드(위)
    else { plot->graph(0)->data()->clear(); plot->graph(1)->data()->clear(); }
    if (hasToc) { plot->graph(2)->setData(xC, upC, true); plot->graph(3)->setData(xC, dnC, true); }   // Toc 밴드(아래)
    else { plot->graph(2)->data()->clear(); plot->graph(3)->data()->clear(); }

    plot->clearItems();
    auto drawVLine = [&](double xMs, const QColor &color, double width){
        if (xMs < -kPreMs || xMs > kPostMs) return;
        auto *line = new QCPItemLine(plot); line->start->setCoords(xMs, -1.18); line->end->setCoords(xMs, 1.18);
        line->setPen(QPen(color, width));
    };
    // tg 식 도(°) 격자(시간 격자가 아님): 10°마다 세로선(비선형). x = −t_AC(°) = −3600·lift/(π·bph·°).
    const int bph = mBuf.bph();
    auto degToMs = [&](double deg){ return -1000.0 * (3600.0 * mLiftAngle) / (M_PI * bph * deg); };
    if (bph > 0) {
        QSharedPointer<QCPAxisTickerText> topTicker(new QCPAxisTickerText);
        for (int deg = 100; deg <= 360; deg += 10) {             // tg: i=10..360 step 10
            const double xMs = degToMs(deg);
            if (xMs < -kPreMs || xMs > kPostMs) continue;
            const bool major = (deg % 50 == 0);                  // 50° = 빨강 major + 라벨, 그 외 10° = 녹색
            drawVLine(xMs, major ? QColor(150, 40, 40) : QColor(25, 90, 25), 1);
            if (major) topTicker->addTick(xMs, QString::number(deg));
        }
        plot->xAxis2->setVisible(true); plot->xAxis2->setTickLabels(true);
        plot->xAxis2->setBasePen(QPen(Qt::white)); plot->xAxis2->setTickPen(QPen(Qt::white));
        plot->xAxis2->setSubTickPen(QPen(Qt::white)); plot->xAxis2->setTickLabelColor(Qt::white);
        plot->xAxis2->setLabelColor(Qt::white); plot->xAxis2->setLabel(QStringLiteral("amplitude(°)"));
        plot->xAxis2->setLabelFont(QFont(QStringLiteral("sans"), 7));
        plot->xAxis2->setTickLabelFont(QFont(QStringLiteral("sans"), 7));
        plot->xAxis2->setRange(plot->xAxis->range()); plot->xAxis2->setTicker(topTicker);
        if (mAmplitudeValid && mAmplitudeDeg > 0.0) drawVLine(degToMs(mAmplitudeDeg), QColor(40, 110, 255), 3);   // 측정 진폭
    }
    drawVLine(0.0, QColor(60, 120, 255), 1.5);                   // C(pulse) = 파랑(0ms)

    // 위·아래 구분선(y=0) + Tic/Toc 라벨.
    auto *mid = new QCPItemLine(plot);
    mid->start->setCoords(-kPreMs, 0.0); mid->end->setCoords(kPostMs, 0.0);
    mid->setPen(QPen(QColor(120, 120, 120), 1, Qt::DashLine));
    auto addLabel = [&](const QString &t, double yRatio, Qt::Alignment va){
        auto *txt = new QCPItemText(plot);
        txt->position->setType(QCPItemPosition::ptAxisRectRatio);
        txt->position->setCoords(0.015, yRatio); txt->setPositionAlignment(Qt::AlignLeft | va);
        txt->setText(t); txt->setColor(Qt::white);
        txt->setFont(QFont(QStringLiteral("sans"), 9, QFont::Bold));
        txt->setBrush(QColor(0, 0, 0, 150)); txt->setPadding(QMargins(3, 1, 3, 1));
    };
    addLabel(QStringLiteral("Tic"), 0.02, Qt::AlignTop);        // 위 = Tic
    addLabel(QStringLiteral("Toc"), 0.98, Qt::AlignBottom);     // 아래 = Toc
    plot->replot(QCustomPlot::rpQueuedReplot);
}

// 하단 period: 최신 한 박자(Tick·Tock 두 버스트) 원신호 + A/C 마커 + 검출창 음영.
void TabWaveformCompare::drawPeriod()
{
    if (!mPeriod || !mRawBuf.hasData()) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int beat = mBuf.samplesPerBeat();
    const QVector<WaveEvent> events = mBuf.eventsInRange(mBuf.oldestAbs(), mBuf.latestAbs());
    QVector<uint64_t> aSamples; for (const WaveEvent &event : events) if (event.type == 1) aSamples.push_back(event.sample);
    if (aSamples.size() < 2 || beat <= 0) { for (int g = 0; g < 3; ++g) mPeriod->graph(g)->data()->clear(); mPeriod->clearItems(); mPeriod->replot(); return; }
    const uint64_t ticA = aSamples[aSamples.size() - 2], tocA = aSamples.last();
    const int pre = (int)(0.005 * sr);
    const uint64_t from = ticA > (uint64_t)pre ? ticA - pre : 0;
    const int count = (int)(tocA - from) + (int)(0.012 * sr);
    if (count <= 1) return;
    QVector<double> raw; mRawBuf.copyRange(from, count, raw);
    QVector<double> magnitude(count); for (int i = 0; i < count; ++i) magnitude[i] = std::fabs(raw[i]);
    // 실제 최대값으로 정규화 → 최대 피크가 가장자리에 닿고 그 이하는 실제 모양대로(포화·잘림 없음).
    double peak = 1e-9; for (double m : magnitude) peak = std::max(peak, m);
    const int step = std::max(1, count / 2000);
    QVector<double> x, yUpper, yLower, base;
    for (int i = 0; i < count; i += step) {
        x.push_back(1000.0 * ((double)from + i - (double)ticA) / sr);
        const double mag = (magnitude[i] / peak) * 0.98;        // [0,1]·여백 → 클램프 불필요
        yUpper.push_back(mag); yLower.push_back(-mag); base.push_back(0.0);
    }
    mPeriod->graph(0)->setData(x, base, true);
    mPeriod->graph(1)->setData(x, yUpper, true);
    mPeriod->graph(2)->setData(x, yLower, true);
    mPeriod->clearItems();
    auto shadeImpulse = [&](uint64_t aSample){
        for (const WaveEvent &event : events) if (event.type == 2 && event.sample > aSample && event.sample < aSample + (uint64_t)beat / 2) {
            const double aMs = 1000.0 * ((double)aSample - (double)ticA) / sr, cMs = 1000.0 * ((double)event.sample - (double)ticA) / sr;
            auto *rect = new QCPItemRect(mPeriod); rect->topLeft->setCoords(aMs, 1.15); rect->bottomRight->setCoords(cMs, -1.15);
            rect->setPen(Qt::NoPen); rect->setBrush(QColor(40, 60, 200, 70));
            break;
        }
    };
    shadeImpulse(ticA); shadeImpulse(tocA);
    auto drawVLine = [&](uint64_t sample, const QColor &color){
        const double xMs = 1000.0 * ((double)sample - (double)ticA) / sr;
        auto *line = new QCPItemLine(mPeriod); line->start->setCoords(xMs, -1.15); line->end->setCoords(xMs, 1.15);
        line->setPen(QPen(color, 1.2));
    };
    drawVLine(ticA, QColor(0, 200, 0)); drawVLine(tocA, QColor(0, 200, 0));
    for (const WaveEvent &event : events) if (event.type == 2 && event.sample >= ticA && event.sample <= tocA + (uint64_t)beat / 2) drawVLine(event.sample, QColor(220, 40, 40));
    auto *titleText = new QCPItemText(mPeriod);                  // 그래프 이름
    titleText->position->setType(QCPItemPosition::ptAxisRectRatio);
    titleText->position->setCoords(0.015, 0.02); titleText->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    titleText->setText(QStringLiteral("Period")); titleText->setColor(Qt::white);
    titleText->setFont(QFont(QStringLiteral("sans"), 9, QFont::Bold));
    titleText->setBrush(QColor(0, 0, 0, 150)); titleText->setPadding(QMargins(3, 1, 3, 1));
    mPeriod->xAxis->setRange(x.isEmpty() ? -5 : x.first(), x.isEmpty() ? 5 : x.last());
    mPeriod->yAxis->setRange(-1.05, 1.05);
    mPeriod->replot(QCustomPlot::rpQueuedReplot);
}

// 좌측 paperstrip: tic/tac onset 을 세로(시간)·가로(2박자 폴딩 위상)로 점 표시.
void TabWaveformCompare::drawPaperstrip()
{
    if (!mPaper) return;
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int beat = mBuf.samplesPerBeat();
    if (beat <= 0 || mAOnsetHist.isEmpty() || !mHaveAnchor) {
        mPaper->graph(0)->data()->clear(); mPaper->graph(1)->data()->clear(); mPaper->clearItems(); mPaper->replot(); return;
    }
    // tg paperstrip: 폴딩창 W = 비트주기/zoom. 고정 앵커(점프 없음) + 고정 0.5 오프셋(초기 가운데).
    //  → tic·tac 이 거의 같은 x 로 접히되 'beat error' 만큼 벌어지고, rate 드리프트는 기울기/ wrap 으로 보임.
    const double foldWidth   = (double)beat / kPaperZoom;
    const double foldWidthMs = 1000.0 * foldWidth / sr;
    const double latestSec = (double)mRawBuf.latestAbs() / sr;
    // 처음부터 1분 폭 고정: 경과<60s 면 [0,60], 그 이후엔 [현재−60s, 현재]로 스크롤.
    const double yLo = (latestSec <= kPaperSecs) ? 0.0 : (latestSec - kPaperSecs);
    const double yHi = yLo + kPaperSecs;
    QVector<double> dotX, dotY, outX, outY;
    for (int idx = 0; idx < mAOnsetHist.size(); ++idx) {
        const uint64_t aSample = mAOnsetHist[idx];
        const double tSec = (double)aSample / sr;
        if (tSec < yLo) continue;
        double foldRemainder = std::fmod((double)((int64_t)aSample - (int64_t)mAnchor), foldWidth);
        if (foldRemainder < 0) foldRemainder += foldWidth;
        double phase = foldRemainder / foldWidth + 0.5;          // 고정 앵커·오프셋 → x 안 흔들림
        phase -= std::floor(phase);                              // [0,1) wrap
        if (idx < mAOnsetOutlier.size() && mAOnsetOutlier[idx]) { outX.push_back(phase); outY.push_back(tSec); }  // [이상치] 주황
        else { dotX.push_back(phase); dotY.push_back(tSec); }   // tic·tac 합산 — beat error 구분 제거
    }
    mPaper->graph(0)->setData(dotX, dotY, false);
    mPaper->graph(1)->setData(outX, outY, false);
    mPaper->clearItems();
    auto drawVLine = [&](double xPos, const QColor &color){
        auto *line = new QCPItemLine(mPaper); line->start->setCoords(xPos, yLo); line->end->setCoords(xPos, latestSec);
        line->setPen(QPen(color, 1));
    };
    drawVLine(0.08, QColor(0, 120, 0)); drawVLine(0.92, QColor(0, 120, 0));   // tg 녹색 마진(wrap 경계)
    auto *widthLabel = new QCPItemText(mPaper);                  // 폭 라벨 — 좌상단 모서리(데이터 안 가림)
    widthLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    widthLabel->position->setCoords(0.02, 0.012); widthLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    widthLabel->setText(QString("W=%1ms").arg(foldWidthMs, 0, 'f', 1));
    widthLabel->setColor(Qt::white); widthLabel->setBrush(QColor(0, 0, 0, 170)); widthLabel->setPadding(QMargins(3, 1, 3, 1));
    mPaper->xAxis->setRange(0, 1);                               // x 고정(autoscale 아님)
    mPaper->yAxis->setRange(yLo, yHi);                           // 항상 60초 폭 고정
    mPaper->replot(QCustomPlot::rpQueuedReplot);
}

void TabWaveformCompare::render()
{
    if (!mRawBuf.hasData()) return;
    drawTicTocPanel();
    drawPeriod();
    drawPaperstrip();
}

void TabWaveformCompare::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mTicAvg.clear(); mTocAvg.clear(); mWindowLen = 0; mTicInit = mTocInit = false;
    mLastC = 0; mHaveLastC = false; mCCount = 0;
    mAOnsetHist.clear(); mAOnsetOutlier.clear(); mAnchor = 0; mHaveAnchor = false;
    for (QCustomPlot *plot : {mPaper, mTicToc, mPeriod}) {
        if (!plot) continue;
        for (int g = 0; g < plot->graphCount(); ++g) plot->graph(g)->data()->clear();
        plot->clearItems(); plot->replot();
    }
}
