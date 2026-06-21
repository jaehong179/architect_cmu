#ifndef TABWAVEFORMCOMPARE_H
#define TABWAVEFORMCOMPARE_H
// Waveform Comparison 탭 (FR-WCD) — Marcello Mamino 의 Tg(vacaboja/tg, tg.ciovil.li) 구도 재현:
//  [좌] paperstrip — tic/tac onset 을 시간(세로 스크롤)·주기 폴딩(가로)으로 점 표시.
//  [우-상] tic 평균파형, [우-중] toc 평균파형 — C(pulse) 정렬, 검정 배경 흰 미러 파형,
//          상단 도(°) 스케일(lift angle), 하단 ms, 진폭 도(°) 격자(10° 녹색·50° 빨강), 파랑=측정 진폭.
//  [우-하] period — 한 박자 전체(두 버스트) 원신호 + A/C 마커 + 검출창 음영.
#include "TabView.h"
#include "WaveBuffer.h"
#include <QVector>
class QCustomPlot;
class QLabel;
class ReadoutBar;
class WaveLodHistory;   // [③] 8분 이력(중앙) — seek replay

class TabWaveformCompare : public TabView
{
    Q_OBJECT
public:
    explicit TabWaveformCompare(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Waveform Comparison Display with Timing Markers"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    void onSeek(double absSample) override;            // [③] 정지 중 트렌드 클릭 → 그 시점 표시
    void onResumeLive() override { mBuf.clear(); mRawBuf.clear(); }   // 라이브 복귀: seek 버퍼 비움
    void setHistory(WaveLodHistory *h) { mHistory = h; }
protected:
    void onShown() override;
private:
    WaveLodHistory *mHistory = nullptr;
    void render();
    void accumulate(const WaveBlock &w);                 // C 정렬 tic/toc 평균 + A onset 누적
    QCustomPlot *makeScope();                            // 검정 배경 흰 미러 파형 패널 1개
    void drawAvgPanel(QCustomPlot *plot, const QVector<double> &avg, const QString &title);
    void drawPeriod();                                  // 하단: 한 박자 전체 파형
    void drawPaperstrip();                              // 좌측: tic/tac 점

    ReadoutBar  *mBar    = nullptr;
    QCustomPlot *mPaper  = nullptr;   // 좌측 paperstrip
    QCustomPlot *mTic    = nullptr;   // tic 평균파형
    QCustomPlot *mToc    = nullptr;   // toc 평균파형
    QCustomPlot *mPeriod = nullptr;   // 하단 period 전체파형
    WaveBuffer   mBuf;        // 엔벨로프 + 이벤트(A/C 정렬·동기)
    WaveBuffer   mRawBuf;     // 바이폴라 원신호(표시)
    bool         mConfigured = false;
    int          mLiftAngle  = 52;
    double       mAmplitudeDeg = 0.0; bool mAmplitudeValid = false;

    // C 정렬 tic/toc 코히어런트 평균(EMA).
    QVector<double> mTicAvg, mTocAvg;   // 길이 = mWindowLen (pre+post)
    int      mWindowLen = 0; bool mTicInit = false, mTocInit = false;
    uint64_t mLastC = 0; bool mHaveLastC = false; long mCCount = 0;

    // paperstrip 용 최근 A onset 절대샘플.
    QVector<uint64_t> mAOnsetHist; uint64_t mAnchor = 0; bool mHaveAnchor = false;

    static constexpr double kPreMs  = 25.0;   // C 이전(tg NEGATIVE_SPAN)
    static constexpr double kPostMs = 10.0;   // C 이후(tg POSITIVE_SPAN)
    static constexpr int    kPaperHist  = 1200;
    static constexpr double kPaperSecs  = 60.0;   // paperstrip 세로 시간창(1분), 초과 시 자연 스크롤
    static constexpr double kPaperZoom  = 50.0;   // tg PAPERSTRIP_ZOOM 대응: 폴딩창 = 비트주기/zoom (클수록 beat error 확대)
};
#endif // TABWAVEFORMCOMPARE_H
