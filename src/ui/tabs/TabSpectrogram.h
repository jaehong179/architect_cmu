#ifndef TABSPECTROGRAM_H
#define TABSPECTROGRAM_H
// Time-Frequency Spectrogram 탭 (FR-TFS) — Project Plan §Spectrogram:
//  가로=시간(ms), 세로=주파수(Hz), 색=강도(dB, −70..0 dB 컬러 스케일/범례).
//  "최근 비트(Last Beat)" 또는 최근 시간창(0.5/1/2초)을 선택해 검사(Plan 명시 요구).
//  원신호(raw PCM)에 Hann 윈도우 STFT.
#include "TabView.h"
#include "WaveBuffer.h"
#include <QVector>
class QCustomPlot;
class QCPColorMap;
class QComboBox;
class QLabel;
class QProgressBar;

class TabSpectrogram : public TabView
{
    Q_OBJECT
public:
    explicit TabSpectrogram(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Time-Frequency Spectrogram Display"); }
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    void recompute();
    QCustomPlot *mPlot = nullptr;
    QCPColorMap *mMap  = nullptr;
    QComboBox   *mWindowSel = nullptr;   // Last Beat / 0.5s / 1s / 2s
    QLabel       *mPeak = nullptr;       // True Peak 수치(dBFS)
    QProgressBar *mPeakBar = nullptr;    // True Peak 녹색 레벨바
    QLabel      *mInfo = nullptr;
    WaveBuffer   mBuf;       // 원신호(raw) 저장
    WaveBuffer   mEvtBuf;    // 이벤트(Last Beat 의 A 위치)용 엔벨로프 버퍼
    bool         mConfigured = false;
    long         mTick = 0;
    static constexpr int kFrames = 128;      // 시간 컬럼 수
    static constexpr int kFFT    = 256;      // 컬럼당 윈도우 길이(샘플)
    static constexpr int kBins   = 64;       // 표시 주파수 빈 수 (0..fs/2 선형)
    static constexpr double kFloorDb = -70.0;  // 컬러 스케일 하한(dB)
};
#endif // TABSPECTROGRAM_H
