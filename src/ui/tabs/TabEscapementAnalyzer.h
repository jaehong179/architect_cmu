#ifndef TABESCAPEMENTANALYZER_H
#define TABESCAPEMENTANALYZER_H
// Escapement Analyzer & Marker-Line 탭 (FR-EAM) — etimer 구도:
//  한 화면에 beat 동기 '고정' x축으로 Tick(좌)·Tock(우) 원신호(raw) 버스트를 펼치고,
//  각 버스트의 T1/T3 빨강 마커 + ms 간격 + 이상적 Tock T3(녹색) + onset↔peak(자홍/청록).
//  '가운데'에는 최근 비트들의 타이밍 마커를 점(scatter)으로 누적:
//   Tic(짝수 비트)=파랑 점, Tac(홀수 비트)=빨강 점 → 두 점열 '간격 = beat error', '기울기 = rate'.
//  (beat error 는 sub-ms 라 가운데 밴드에서 ±kWrapMs 창을 확대 매핑해 가시화)
#include "TabView.h"
#include "WaveBuffer.h"
#include <QVector>
class QCustomPlot;
class QLabel;
class QSpinBox;
class QCheckBox;
class ReadoutBar;
class WaveLodHistory;   // [③] 8분 이력(중앙) — seek replay 시 과거 구간 복원

class TabEscapementAnalyzer : public TabView
{
    Q_OBJECT
public:
    explicit TabEscapementAnalyzer(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Escapement Analyzer and Marker-Line Display"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    void onSeek(double absSample) override;            // [③] 정지 중 트렌드 클릭 → 그 시점 파형 표시
    void setHistory(WaveLodHistory *h) { mHistory = h; }
protected:
    void onShown() override;
private:
    WaveLodHistory *mHistory = nullptr;   // 주입된 중앙 이력(소유 안 함)
    void render();
    void accumBeats(const WaveBlock &w);     // 새 A 이벤트마다 비트 타이밍오차 점 누적
    ReadoutBar  *mBar       = nullptr;
    QCustomPlot *mPlot      = nullptr;   // 단일 통합 플롯(파형 + 가운데 마커 점)
    QLabel      *mInfo      = nullptr;
    QSpinBox    *mThresh    = nullptr;   // 임계 %(창 최대 대비) — 검출기 onset 레벨 없을 때
    QCheckBox   *mOnsetPeak = nullptr;   // onset↔peak 비교 마커
    WaveBuffer   mBuf;        // 엔벨로프 + 이벤트(마커/동기/검출)
    WaveBuffer   mRawBuf;     // 원신호(raw bipolar) — 표시용
    bool         mConfigured = false;
    // y 스케일 고정: 초기 kScaleWarm 프레임 grow-only 관찰 → 이후 고정. 신호 급변 시만 재보정.
    double       mAmpScale = 0.0; int mScaleFrames = 0; bool mScaleLocked = false;
    // 비트 타이밍오차 누적(E1~E3): 짝수=Tic, 홀수=Tac. 가운데 점열로 표시.
    bool     mAnchored = false; uint64_t mAnchorStartSample = 0; long mBeatNumber = 0; uint64_t mLastASample = 0; int mAnchorBph = 0;
    QVector<double> mBeatErrVals;   // 랩된 타이밍오차(ms, ±kWrapMs/2)
    QVector<long>   mBeatErrNums;   // 비트 번호(짝/홀 → Tic/Tac)
    double   mLastBeatErr = 0.0;
    static constexpr int    kScaleWarm = 40;
    static constexpr double kWrapMs    = 6.0;   // 가운데 마커 ±3ms 창(sub-ms beat error 가독)
    static constexpr int    kHist      = 140;   // 가운데에 쌓는 최근 비트 점 수
};
#endif // TABESCAPEMENTANALYZER_H
