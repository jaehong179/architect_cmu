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
    void onSeek(double absSample) override;            // [③] 다른 탭에서 선택한 시점을 Scope1에 표시    void onResumeLive(bool seeked) override { mSelBeat = mSelCell = -1; mStripsDirty = true; if (seeked) { mBuf.clear(); mHaveLastBeat = false; } }   // 라이브 복귀
    void setHistory(WaveLodHistory *h) { mHistory = h; }
signals:
    void seekRequested(double absSample);   // [③] 스트립 비트 선택 → 그 비트의 절대 샘플
protected:
    void onShown() override;
private:
    void processNewBeats();      // 완료된 비트를 trace1/2 평균·셀·비트띠에 누적
    void renderScope1();
    void renderStrips();         // Scope1 하단: 최근 개별 비트 띠
    void renderCells();          // Scope2 하단: 누적평균 셀 8칸
    void renderScope2();
    void updateCellWindows(bool tick, const QVector<double> &beat);  // 슬라이딩 윈도우 증분 갱신
    void applyScopeView();       // Scope1/Scope2 토글에 따른 표시 전환
    // 활성 스코프와 그에 맞는 하단 영역만 그린다(하단은 새 비트/전환 시에만 갱신).
    void render() { const bool d = mStripsDirty; mStripsDirty = false;
                    if (mShowScope2) { renderScope2(); if (d) renderCells(); }
                    else             { renderScope1();  if (d) renderStrips(); } }
    void onStripCellClicked(QMouseEvent *ev);   // Scope1 비트 칸 클릭 → 그 비트 확대
    void onCellClicked(QMouseEvent *ev);    // Scope2 누적평균 셀 클릭 → Scope2 표시
    double beatAmplitudeDeg(uint64_t aEventSample) const;   // E8: A→C 간격으로 비트 진폭(°)
    double beatAcSamples(uint64_t aEventSample) const;      // A→C 간격(샘플) — Scope2 T3 위치

    QWidget     *mScope1Box = nullptr; // Scope1 컨테이너(라벨+플롯)
    QWidget     *mScope2Box = nullptr; // Scope2 컨테이너(라벨+사이클+트레이스2)
    QWidget     *mStripBox = nullptr;  // Scope1 하단(최근 비트 띠) 컨테이너
    QWidget     *mCellsBox = nullptr;  // Scope2 하단(누적평균 셀) 컨테이너
    QCustomPlot *mScope1 = nullptr;   // 단일 비트
    QVector<QCustomPlot *> mStripCells;   // Scope1 하단: 최근 개별 비트 8칸(분리·테두리, 클릭 → 확대)
    QVector<QCustomPlot *> mCells;      // Scope2 하단: 누적평균 셀 8칸(개별 플롯)
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
    double       mPeakScope1 = 0, mPeakStrips = 0, mPeakBeats = 0, mPeakTr1 = 0, mPeakTr2 = 0;
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
    // Scope1 하단: 최근 개별 비트 띠(클릭 → 확대). 명세: "최근 비트 띠 + 이전 비트 선택 확대".
    QVector<QVector<double>> mRecent;          // 최근 개별 비트(최신=뒤)
    QVector<uint64_t>        mRecentSample;    // 각 비트의 A 절대 샘플(선택→seek)
    int                      mSelBeat = -1;    // 선택된 최근 비트(-1=라이브) — Scope1
    // Scope2 하단: 누적평균 셀(8칸): 칸 k = 최근 kAvgStep·(k+1) 비트 평균(5,10,…,40), tick/tock 분리.
    QVector<QVector<double>> mTickHist, mTockHist;   // 최근 비트 원본(최신=뒤), tick=짝/tock=홀
    QVector<QVector<double>> mCellTick, mCellTock;    // 칸별 평균 파형(렌더·Scope2 선택 표시 공용)
    QVector<QVector<double>> mTickWinSum, mTockWinSum; // 칸별 윈도우 합(증분 갱신용) — 평균 = 합/N
    QVector<int>             mCellTickN, mCellTockN;   // 칸별 평균에 쓴 비트 수
    QVector<double>          mTickAc, mTockAc;        // 비트별 A→C 간격(샘플) — Scope2 T3(평균) 위치
    int                      mSelCell = -1;            // 선택된 셀(-1=Σ) — Scope2
    bool                     mStripsDirty = false;     // 새 비트/전환 시에만 하단 재그리기
    static constexpr int     kStrips  = 8;             // Scope1 최근 비트 띠 개수
    static constexpr int     kCells   = 8;             // Scope2 누적평균 셀 8칸
    static constexpr int     kAvgStep = 5;             // 칸 k = 5·(k+1) 비트 평균
    static constexpr long    kCycleN  = 50;            // Plan: 50 tic + 50 tac 간격에서 사이클 완료
};
#endif // TABBEATNOISESCOPE_H
