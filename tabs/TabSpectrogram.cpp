#include "TabSpectrogram.h"
#include "qcustomplot.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <cmath>

TabSpectrogram::TabSpectrogram(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QStringLiteral(
        "<b>Spectrogram</b> — 가로=시간(ms), 세로=주파수(Hz), 색=강도(dB). "
        "비트마다 반복되는 수직 줄무늬(에너지 버스트) 구조 관찰. (FR-TFS · raw STFT)"), this));

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("창:"), this));
    mWindowSel = new QComboBox(this);
    mWindowSel->addItem(QStringLiteral("Last Beat"), 0.0);
    mWindowSel->addItem(QStringLiteral("0.5 s"), 0.5);
    mWindowSel->addItem(QStringLiteral("1.0 s"), 1.0);
    mWindowSel->addItem(QStringLiteral("2.0 s"), 2.0);
    mWindowSel->setCurrentIndex(2);
    ctl->addWidget(mWindowSel);
    ctl->addStretch(1);
    mInfo = new QLabel(QStringLiteral("측정 대기 중…"), this);
    mInfo->setStyleSheet(QStringLiteral("font-family:monospace;"));
    ctl->addWidget(mInfo);
    lay->addLayout(ctl);

    mPlot = new QCustomPlot(this);
    mMap = new QCPColorMap(mPlot->xAxis, mPlot->yAxis);
    mMap->data()->setSize(kFrames, kBins);
    mMap->setGradient(QCPColorGradient::gpSpectrum);
    mPlot->xAxis->setLabel(QStringLiteral("time (ms)"));
    mPlot->yAxis->setLabel(QStringLiteral("frequency (Hz)"));
    auto *scale = new QCPColorScale(mPlot);
    mPlot->plotLayout()->addElement(0, 1, scale);
    scale->setType(QCPAxis::atRight);
    mMap->setColorScale(scale);
    scale->axis()->setLabel(QStringLiteral("dB"));
    mMap->setDataRange(QCPRange(kFloorDb, 0.0));     // 고정 −70..0 dB (참조 그림 컬러바)
    lay->addWidget(mPlot, 1);

    connect(mWindowSel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int){ if (isVisible()) recompute(); });
    onResetSession();
}

void TabSpectrogram::onWave(const WaveBlock &w)
{
    if (!mConfigured && w.sampleRateHz > 0) {
        mBuf.configure((int)(w.sampleRateHz * 2.2));    // 최대 2초 창 + 여유
        mEvtBuf.configure((int)(w.sampleRateHz * 2.2));
        mConfigured = true;
    }
    mEvtBuf.push(w);                                    // 이벤트(A) 추적
    if (w.raw && w.rawN > 0) {                          // 원신호 → STFT 입력
        WaveBlock rb; rb.env = w.raw; rb.n = w.rawN; rb.startSample = w.rawStart;
        rb.sampleRateHz = w.sampleRateHz; rb.bph = w.bph; rb.synced = w.synced;
        mBuf.push(rb);
    } else {
        mBuf.push(w);                                   // 폴백: 엔벨로프
    }
    // 스로틀: 매 8번째 블록만 재계산(렌더 비용 절감).
    if ((mTick++ % 8) == 0 && isVisible()) recompute();
}

void TabSpectrogram::recompute()
{
    if (!mBuf.hasData()) return;
    const int sr = mBuf.sampleRate() > 0 ? mBuf.sampleRate() : 48000;

    // 표시 창 결정: Last Beat = 최근 A 이벤트부터 한 비트, 그 외 = 최근 t 초.
    const double selS = mWindowSel->currentData().toDouble();
    int winN; uint64_t from;
    if (selS <= 0.0) {
        const int beat = mEvtBuf.samplesPerBeat();
        winN = beat > 0 ? beat : (int)(0.25 * sr);
        uint64_t lastA;
        if (mEvtBuf.latestEvent(1, lastA) && lastA + (uint64_t)winN <= mBuf.latestAbs())
            from = lastA;
        else
            from = mBuf.latestAbs() > (uint64_t)winN ? mBuf.latestAbs() - winN : 0;
    } else {
        winN = (int)(selS * sr);
        from = mBuf.latestAbs() > (uint64_t)winN ? mBuf.latestAbs() - winN : 0;
    }
    if (winN < kFFT) winN = kFFT;

    QVector<double> sig; mBuf.copyRange(from, winN, sig);

    // STFT: kFrames 컬럼, 컬럼당 kFFT 샘플 + Hann 윈도우.
    //  주파수 빈: 0..fs/2 선형 kBins 개 — 임의 주파수 직접 DFT(빈 보간 없이 Hz 축 정합).
    const int hop = qMax(1, (winN - kFFT) / qMax(1, kFrames - 1));
    QVector<QVector<double>> mag(kFrames, QVector<double>(kBins, 0.0));
    double magMax = 1e-12;
    QVector<double> hann(kFFT);
    for (int n = 0; n < kFFT; ++n) hann[n] = 0.5 - 0.5 * std::cos(2.0 * M_PI * n / (kFFT - 1));
    for (int c = 0; c < kFrames; ++c) {
        const int off = qMin(c * hop, winN - kFFT);
        for (int k = 0; k < kBins; ++k) {
            const double f = (double)k / kBins * (sr / 2.0);
            const double w0 = 2.0 * M_PI * f / sr;
            // 회전 점화식(트랜센던털 호출 제거): e^{-jw0(n+1)} = e^{-jw0 n} · e^{-jw0}.
            const double cw = std::cos(w0), sw = std::sin(w0);
            double cr = 1.0, ci = 0.0, re = 0.0, im = 0.0;
            for (int n = 0; n < kFFT; ++n) {
                const double s = sig[off + n] * hann[n];
                re += s * cr;
                im -= s * ci;
                const double nc = cr * cw - ci * sw;
                ci = cr * sw + ci * cw;
                cr = nc;
            }
            const double m = std::sqrt(re * re + im * im);
            mag[c][k] = m;
            if (m > magMax) magMax = m;
        }
    }
    // dB 변환(창 최대 = 0 dB), 하한 −70 dB.
    for (int c = 0; c < kFrames; ++c)
        for (int k = 0; k < kBins; ++k) {
            double db = 20.0 * std::log10(mag[c][k] / magMax + 1e-12);
            if (db < kFloorDb) db = kFloorDb;
            mMap->data()->setCell(c, k, db);
        }

    // 축: x = ms, y = Hz (Plan: 가로 시간 / 세로 주파수).
    const double winMs = 1000.0 * winN / sr;
    mMap->data()->setRange(QCPRange(0, winMs), QCPRange(0, sr / 2.0));
    mPlot->xAxis->setRange(0, winMs);
    mPlot->yAxis->setRange(0, sr / 2.0);
    mInfo->setText(QString("%1  창=%2 ms  bins=%3 (0..%4 Hz)  bph=%5")
        .arg(mWindowSel->currentText()).arg(winMs, 0, 'f', 0)
        .arg(kBins).arg(sr / 2.0, 0, 'f', 0).arg(mEvtBuf.bph()));
    mPlot->replot(QCustomPlot::rpQueuedReplot);
}

void TabSpectrogram::onShown() { recompute(); if (mPlot) mPlot->replot(); }

void TabSpectrogram::onResetSession()
{
    mBuf.clear(); mEvtBuf.clear(); mConfigured = false; mTick = 0;
    if (mMap) { mMap->data()->fill(kFloorDb); if (mPlot) mPlot->replot(); }
    if (mInfo) mInfo->setText(QStringLiteral("측정 대기 중…"));
}
