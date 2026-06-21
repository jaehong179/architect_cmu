#ifndef TABBEATNOISESCOPE_H
#define TABBEATNOISESCOPE_H
// Beat-Noise Scope 탭 (FR-BNS) — Project Plan §Beat-Noise Scope Display:
//  Scope 1: 단일 비트 파형(20/200/400ms 선택) + 최근 비트 썸네일 스트립(클릭 → 확대) + lift angle 표시.
//  Scope 2: 두 수평축(고정 20ms)의 평균 듀얼-트레이스. Σ(평균) ON/OFF,
//           사이클 = 50 tic + 50 tac 간격 완료 시 축별 평균 진폭 표시(중간 결과 10·20 간격).
//  ※ 시스템이 tic/tac 축 대응을 보장하지 않으므로 축 라벨은 "trace 1/2" (Plan 명시).
#include "TabView.h"
#include "WaveBuffer.h"
#include <QVector>
class QCustomPlot;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QWidget;
class ReadoutBar;
class QMouseEvent;
class WaveLodHistory;   // [③] 8분 이력(중앙) — seek 대상

class TabBeatNoiseScope : public TabView
{
    Q_OBJECT
public:
    explicit TabBeatNoiseScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Beat-Noise Scope Display"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    void onSeek(double absSample) override;            // [③] 다른 탭에서 선택한 시점을 Scope1에 표시
    void onResumeLive() override { mBuf.clear(); mHaveLastBeat = false; mSelectedStrip = -1; }   // 라이브 복귀
    void setHistory(WaveLodHistory *h) { mHistory = h; }
signals:
    void seekRequested(double absSample);   // [③] 스트립 비트 선택 → 그 비트의 절대 샘플
protected:
    void onShown() override;
private:
    void processNewBeats();      // 완료된 비트를 trace1/2 평균·스트립에 누적
    void renderScope1();
    void renderStrips();
    void renderScope2();
    void applyScopeView();       // Scope1/Scope2 토글에 따른 표시 전환
    // 활성 스코프만 그리고 스트립은 항상 갱신(스트립은 항상 하단).
    void render() { if (mShowScope2) renderScope2(); else renderScope1(); renderStrips(); }
    void onStripClicked(QMouseEvent *ev);   // 스트립 클릭 → Scope1 확대
    double beatAmplitudeDeg(uint64_t aEventSample) const;   // E8: A→C 간격으로 비트 진폭(°)

    ReadoutBar  *mBar    = nullptr;
    QWidget     *mScope1Box = nullptr; // Scope1 컨테이너(라벨+플롯)
    QWidget     *mScope2Box = nullptr; // Scope2 컨테이너(라벨+사이클+트레이스2)
    QCustomPlot *mScope1 = nullptr;   // 단일 비트
    QCustomPlot *mStrips = nullptr;   // 최근 비트 썸네일
    QCustomPlot *mTr1    = nullptr;   // Scope2 trace 1 (짝수 비트)
    QCustomPlot *mTr2    = nullptr;   // Scope2 trace 2 (홀수 비트)
    QPushButton *mScopeToggle = nullptr; // Scope1 ↔ Scope2 전환
    QComboBox   *mRange  = nullptr;
    QCheckBox   *mAvg    = nullptr;   // Σ 평균 토글
    QLabel      *mInfo   = nullptr;
    QLabel      *mCycle  = nullptr;   // Σ 사이클 진행/완료 + 축별 평균 진폭
    WaveBuffer   mBuf;
    WaveLodHistory *mHistory = nullptr;   // 주입된 중앙 이력(소유 안 함)
    bool         mConfigured = false;
    bool         mShowScope2 = false; // false=Scope1, true=Scope2
    // y 스케일 안정화: 매 프레임 max 대신 스무딩 피크(상승 즉시·하강 천천히) → 출렁임 억제.
    double       mPeakScope1 = 0, mPeakStrips = 0, mPeakTr1 = 0, mPeakTr2 = 0;
    int          mRangeMs = 20;
    int          mLiftAngle = 52;     // 최근 스냅샷의 lift angle(°) — Scope1 표시용

    // 비트 누적(trace1/2 평균 + 스트립)
    uint64_t                 mLastBeatASample = 0; bool mHaveLastBeat = false;
    long                     mBeatCount = 0;
    int                      mWin = 0;        // 평균 윈도우 샘플 수(20ms)
    QVector<double>          mTr1Sum, mTr2Sum; long mTr1N = 0, mTr2N = 0;
    QVector<double>          mTr1Last, mTr2Last;       // Σ OFF: 최신 단일 비트 표시
    double                   mTr1AmpSum = 0, mTr2AmpSum = 0;   // 축별 진폭(E8) 누적
    long                     mTr1AmpN = 0,  mTr2AmpN = 0;
    double                   mLastCycleAmp1 = 0, mLastCycleAmp2 = 0; bool mHaveCycleResult = false;
    QVector<QVector<double>> mRecent;          // 최근 비트(스트립용)
    QVector<uint64_t>        mRecentSample;    // [③] 각 스트립 비트의 A 절대 샘플(선택→seek)
    int                      mSelectedStrip = -1;   // 선택된 스트립(-1=라이브)
    static constexpr int     kStrips = 8;     // 최근 비트 스트립 8개
    static constexpr long    kCycleN = 50;     // Plan: 50 tic + 50 tac 간격에서 사이클 완료
};
#endif // TABBEATNOISESCOPE_H
