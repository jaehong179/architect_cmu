#include "TabWaveformCompare.h"
#include "ReadoutBar.h"
#include "WaveLodHistory.h"   // [③] seek replay
#include "qcustomplot.h"
#include <QPushButton>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

TabWaveformCompare::TabWaveformCompare(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    mBar = new ReadoutBar(this); lay->addWidget(mBar);
    // 접이식 범례(공간 확보): 버튼으로 펼침/접힘.
    auto *legendBtn = new QPushButton(QStringLiteral("▾ Legend (collapse)"), this);
    legendBtn->setCheckable(true); legendBtn->setChecked(true);   // 기본 펼침
    legendBtn->setStyleSheet(QStringLiteral("QPushButton{ text-align:left; border:none; font-weight:bold; padding:2px; }"));
    auto *key = new QLabel(QStringLiteral(
        "<table cellspacing='0' cellpadding='2'>"
        "<tr><td valign='top'><b>Tic·Toc&nbsp;:</b></td><td>"
        "<font color='#1e78ff'><b>━ bold blue line</b></font>=measured amplitude · <font color='#1e78ff'>┊ blue(0ms)</font>=C(T3) time · "
        "<font color='#b43c3c'><b>┊ red+number(50°)</b></font>·<font color='#149014'>┊ green(10°)</font>=amplitude-degree(°) scale(top axis) · bottom axis=time(ms)</td></tr>"
        "<tr><td valign='top'><b>Period&nbsp;:</b></td><td>"
        "<font color='#00b400'>┊ green</font>=A(T1, impulse pin→pallet fork strike) · "
        "<font color='#dc2828'>┊ red</font>=C(T3, escape-wheel lock·fork→banking pin) · "
        "<font color='#2840c8'>▮ blue shaded</font>=A(T1)→C(T3) span</td></tr>"
        "<tr><td valign='top'><b>Paperstrip&nbsp;:</b></td><td>"
        "<font color='#5080ff'>● blue</font>=tic · <font color='#dc4646'>● red</font>=tac · gap between the two dot rows=beat error · slope=rate · "
        "<font color='#00b400'>┊ green</font>=wrap boundary</td></tr>"
        "</table>"), this);
    key->setWordWrap(true);
    key->setStyleSheet(QStringLiteral("QLabel{ background:#f6f6f6; border:1px solid #c4c4c4; border-radius:4px; padding:5px; }"));
    key->setVisible(true);                               // 기본 펼침
    connect(legendBtn, &QPushButton::toggled, this, [key, legendBtn](bool on){
        key->setVisible(on); legendBtn->setText(on ? QStringLiteral("▾ Legend (collapse)") : QStringLiteral("▸ Legend (expand)"));
    });
    lay->addWidget(legendBtn);
    lay->addWidget(key);

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
    mPaper->addGraph();   // graph0 = tic(파랑) — Rate/Scope·Beat Error 관례와 통일
    mPaper->graph(0)->setLineStyle(QCPGraph::lsNone);
    mPaper->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(80, 140, 255), 4.5));
    mPaper->graph(0)->setName(QStringLiteral("tic"));
    mPaper->addGraph();   // graph1 = tac(빨강)
    mPaper->graph(1)->setLineStyle(QCPGraph::lsNone);
    mPaper->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QColor(235, 70, 70), 4.5));
    mPaper->graph(1)->setName(QStringLiteral("tac"));
    mPaper->legend->setVisible(true);
    mPaper->legend->setBrush(QBrush(QColor(0, 0, 0, 190)));
    mPaper->legend->setBorderPen(QPen(QColor(120, 120, 120)));
    mPaper->legend->setTextColor(Qt::white);
    mPaper->legend->setFont(QFont(QStringLiteral("sans"), 7));
    mPaper->legend->setIconSize(9, 9);
    mPaper->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);  // 우상단 모서리(데이터 안 가림)
    mPaper->setMinimumWidth(210); mPaper->setMaximumWidth(290);

    // ── 우측: tic / toc 평균파형 + period ──
    mTic = makeScope(); mToc = makeScope(); mPeriod = makeScope();
    mPeriod->xAxis->setLabel(QStringLiteral("ms (0=Tick T1)"));
    mPeriod->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));

    auto *body = new QHBoxLayout();
    body->addWidget(mPaper, 0);
    auto *right = new QVBoxLayout();
    right->addWidget(mTic, 2);
    right->addWidget(mToc, 2);
    right->addWidget(mPeriod, 2);
    body->addLayout(right, 1);
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
    if (mBar) mBar->update(s);
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
                while (mAOnsetHist.size() > kPaperHist) mAOnsetHist.remove(0);
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

void TabWaveformCompare::drawAvgPanel(QCustomPlot *plot, const QVector<double> &avg, const QString &title)
{
    const int sr = mRawBuf.sampleRate() > 0 ? mRawBuf.sampleRate() : 48000;
    const int pre = (int)(kPreMs / 1000.0 * sr);
    const int n = avg.size();
    if (n < 2) { for (int g = 0; g < 3; ++g) plot->graph(g)->data()->clear(); plot->clearItems(); plot->replot(); return; }
    QVector<double> magnitude(n); for (int i = 0; i < n; ++i) magnitude[i] = std::fabs(avg[i]);
    QVector<double> sorted = magnitude; const int pctIndex = std::min(n - 1, (int)(n * 0.90));
    std::nth_element(sorted.begin(), sorted.begin() + pctIndex, sorted.end());
    const double peak = std::max(1e-9, sorted[pctIndex]);
    QVector<double> x(n), yUpper(n), yLower(n), base(n);
    for (int i = 0; i < n; ++i) {
        x[i] = 1000.0 * (i - pre) / sr;                          // ms, 0 = C
        const double mag = std::min(1.0, magnitude[i] / peak * 1.0);  // 90퍼센타일을 가득 채움(파형 크게)
        yUpper[i] = mag; yLower[i] = -mag; base[i] = 0.0;
    }
    plot->graph(0)->setData(x, base, true);
    plot->graph(1)->setData(x, yUpper, true);
    plot->graph(2)->setData(x, yLower, true);

    plot->clearItems();
    auto drawVLine = [&](double xMs, const QColor &color, double width){
        if (xMs < -kPreMs || xMs > kPostMs) return;
        auto *line = new QCPItemLine(plot); line->start->setCoords(xMs, -1.15); line->end->setCoords(xMs, 1.15);
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
    auto *titleText = new QCPItemText(plot);                     // 그래프 이름(좌상단)
    titleText->position->setType(QCPItemPosition::ptAxisRectRatio);
    titleText->position->setCoords(0.015, 0.02); titleText->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    titleText->setText(title); titleText->setColor(Qt::white);
    titleText->setFont(QFont(QStringLiteral("sans"), 9, QFont::Bold));
    titleText->setBrush(QColor(0, 0, 0, 150)); titleText->setPadding(QMargins(3, 1, 3, 1));
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
    QVector<double> sorted = magnitude; const int pctIndex = std::min(count - 1, (int)(count * 0.98));
    std::nth_element(sorted.begin(), sorted.begin() + pctIndex, sorted.end());
    const double peak = std::max(1e-9, sorted[pctIndex]);
    const int step = std::max(1, count / 2000);
    QVector<double> x, yUpper, yLower, base;
    for (int i = 0; i < count; i += step) {
        x.push_back(1000.0 * ((double)from + i - (double)ticA) / sr);
        const double mag = std::min(1.0, magnitude[i] / peak * 0.96);
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
    QVector<double> ticX, ticY, tacX, tacY;
    for (uint64_t aSample : mAOnsetHist) {
        const double tSec = (double)aSample / sr;
        if (tSec < yLo) continue;
        const long beatIndex = (long)llround((double)((int64_t)aSample - (int64_t)mAnchor) / (double)beat);
        double foldRemainder = std::fmod((double)((int64_t)aSample - (int64_t)mAnchor), foldWidth);
        if (foldRemainder < 0) foldRemainder += foldWidth;
        double phase = foldRemainder / foldWidth + 0.5;          // 고정 앵커·오프셋 → x 안 흔들림
        phase -= std::floor(phase);                              // [0,1) wrap
        if (beatIndex % 2 == 0) { ticX.push_back(phase); ticY.push_back(tSec); }
        else                    { tacX.push_back(phase); tacY.push_back(tSec); }
    }
    mPaper->graph(0)->setData(ticX, ticY, false);
    mPaper->graph(1)->setData(tacX, tacY, false);
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
    drawAvgPanel(mTic, mTicAvg, QStringLiteral("Tic"));
    drawAvgPanel(mToc, mTocAvg, QStringLiteral("Toc"));
    drawPeriod();
    drawPaperstrip();
}

void TabWaveformCompare::onResetSession()
{
    mBuf.clear(); mRawBuf.clear(); mConfigured = false;
    mTicAvg.clear(); mTocAvg.clear(); mWindowLen = 0; mTicInit = mTocInit = false;
    mLastC = 0; mHaveLastC = false; mCCount = 0;
    mAOnsetHist.clear(); mAnchor = 0; mHaveAnchor = false;
    if (mBar) mBar->update(MeasurementSnapshot{});
    for (QCustomPlot *plot : {mPaper, mTic, mToc, mPeriod}) {
        if (!plot) continue;
        for (int g = 0; g < plot->graphCount(); ++g) plot->graph(g)->data()->clear();
        plot->clearItems(); plot->replot();
    }
}
