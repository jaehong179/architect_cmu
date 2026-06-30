#ifndef TABRATESCOPE_H
#define TABRATESCOPE_H
// Rate/Scope 탭 — rate 오차 시계열(RatePlot) + 실시간 파형 스코프(ScopePlot).
//  과거에는 MainWindow.ui 의 정적 RateTab + MainWindow 인라인 렌더링이었으나, 다른 탭과
//  동일하게 TabView 로 분리한다:
//   · RatePlot  ← onMeasurement (snapshot 의 tic/toc rate 시리즈)
//   · ScopePlot ← onWave        (엔벨로프 + A/C 이벤트 마커)
//  [perf] ScopePlot 의 실제 paint 완료(afterReplot)를 scopeReplotted() 시그널로 알려,
//   MainWindow 가 disp_paint_ms·e2e_full_ms·paint_fps 를 기록한다(perf 상태는 MainWindow 잔류).
#include "TabView.h"
#include <QColor>
class QCustomPlot;
class QSpinBox;
class QLabel;
class QPushButton;
class QCPItemStraightLine;
class QCPItemLine;
class QCPItemTracer;
class QCPItemText;
class WaveLodHistory;     // 8분 엔벨로프 이력 버퍼(중앙 1개) — pause 중 스크롤백 렌더 원본
class QCPRange;
class QCheckBox;

class TabRateScope : public TabView
{
    Q_OBJECT
public:
    explicit TabRateScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Rate/Scope"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    bool resetOnResume() const override { return true; }   // [pause→start] resume 시 그래프·데이터 초기화
    void onSeek(double absSample) override;   // [③] 정지 중 트렌드 클릭 → 그 시점으로 스코프 이동
    void onResumeLive(bool seeked) override;  // [③] seek 후 resume 시 이력으로 그린 임시 상태 정리

    // 8분 이력 버퍼 주입(MainWindow 소유). pause 중 이 버퍼를 queryWindow 로 그린다.
    void setHistory(WaveLodHistory *h) { mHistory = h; }

    // 정지 ↔ 8분 이력 스크롤백 전환. 전역 Pause 버튼(MainWindow)이 호출한다.
    void setPaused(bool paused);
protected:
    void onShown() override;
signals:
    void scopeReplotted();   // ScopePlot afterReplot → MainWindow::OnScopeReplotted 로 연결(perf)
    void seekRequested(double absSample);   // [③] 정지 중 상단 RatePlot 클릭 → 그 시점
private:
    void setupPlots();
    void renderHistoryWindow();         // 현재 보이는 시간창을 이력에서 잘라 그림(줌/팬 시 재호출)
    void drawHistoryMarkers(uint64_t fromAbs, uint64_t toAbs);   // 이력 이벤트로 A/C 마커·ms 복원
    void addVerticalMarker(double x, double height, const QColor &color, bool isEventA);
    void addText(double x, double height, const QString &text, const QColor &color, Qt::Alignment alignment);
    void addHorizontalMarkerInward(double xLeft, double xRight, double length, double height, const QColor &color);
    void addHorizontalMarkerOutward(double xLeft, double xRight, double height, const QColor &color);
    void removeMarkersAndText(double rangeMin, double rangeMax);
    void updateLabelVisibility();
    void purgeHistory();
    void frameScope();   // 스코프 x창 — roll(미검출 시 따라감) vs 트리거락(win 고정+재무장, 정지)
    void syncScopeXAxis(double timeEndSec);
    void updateScopeXAxisTicks(const QCPRange &range);
    double sampleToTime(uint64_t sample) const;

    // ── [정지 후] 상·하단 시간 동기(같은 시간창 잠금) ────────────────────────────
    void enterLockedView();              // 정지 진입: 상·하단을 같은 시간창으로 잠그고 진입 뷰 저장
    void seekTo(double absSample);       // 그 절대샘플 시점으로 상·하단 창 동기 이동 + 상단 커서
    void resetZoomToEntry();             // [버튼] 정지 진입 시점 뷰로 상·하단 복원

    QLabel      *mWindowLabel = nullptr;
    QPushButton *mResetZoomBtn = nullptr;   // [버튼] 원래(정지 진입) 스케일로 복원
    QCustomPlot *mRatePlot   = nullptr;
    QCustomPlot *mScopePlot  = nullptr;
    QSpinBox    *mScopeScale = nullptr;
    QCheckBox   *mScopeLogView = nullptr;
    // 비트 트리거락(오실로스코프식 정지) — 항상 on. 검출(틱) 전엔 roll, 검출 후 창을 win 고정하고 win마다 재무장.
    bool         mTriggerLock = true;
    bool         mSweepArmed  = false;
    double       mSweepEnd    = 0.0;        // 고정창의 우측 끝 시각(틱). win 지나면 다음 틱으로 재무장
    double       mFirstTickTime = 0.0; bool mHaveFirstTick = false;  // 첫 검출 시각 — 잠금 전 한 창 채우는 기준
    QCPItemLine    *mRateCursor = nullptr;      // 상단 RatePlot seek 롤리팝 커서(줄기)
    QCPItemTracer  *mRateCursorHead = nullptr;  // 롤리팝 머리
    QCPItemText    *mRateCursorTip  = nullptr;  // 롤리팝 상단 툴팁(선택 시각)
    int             mRateMaxPoints = 0;         // RatePlot x축 폭(0..N) — 클릭 비율 매핑용
    uint64_t        mPauseLatest = 0;           // 정지 시점의 이력 latest(상단 클릭 비율/커서 역산 기준)
    WaveLodHistory *mHistory    = nullptr;      // 주입된 중앙 이력 버퍼(소유 안 함)
    bool            mPaused     = false;        // true=이력 스크롤백 모드
    bool            mInHistoryRender = false;   // rangeChanged 재귀 가드
    bool            mSyncingAxes = false;       // 상↔하단 x축 동기 재귀 가드
    bool            mHistActive = false;        // 정지 후 사용자가 드래그/줌해 이력 렌더로 전환됨
    double          mHistOffset = 0.0;          // 이력 절대인덱스 − 라이브 mGraphTicks (정지 시 고정)
    double          mPlotOriginSec = 0.0;       // rate x 원점(초) — 라이브 수신 시 갱신, 정지 시 고정
    double          mRateScopeShift = 0.0;      // rateX − scopeX (정지 시 고정 상수) = mHistOffset/sr − origin
    double          mEntryRateLo = 0.0, mEntryRateHi = 0.0;   // 정지 진입(초기 시작) 상단 x창 — Reset Zoom 시 상단 복원용
    uint64_t        mGraphTicks = 0;        // 엔벨로프 샘플 카운터
    double       mLastA = 0.0; bool mHaveLastA = false;
    int          mSampleRateHz = 48000;
    int          mLiftAngle = 52;        // C 마커 진폭 라벨용(onMeasurement 에서 갱신)
    int          mLastBph = 0;           // 이력 마커 진폭 계산용(onWave 에서 갱신)
    double       mScopePeakNorm = 0.0;   // Scope Y축 smoothPeak 상태
    // min/max 데시메이션 누적 상태(고레이트 스코프: 점 수↓ + 피크 보존). 구간을 onWave 경계 넘어 누적.
    int          mDecimCount = 0;
    float        mDecimMin = 0.0f, mDecimMax = 0.0f;
    uint64_t     mDecimMinTick = 0, mDecimMaxTick = 0;
};
#endif // TABRATESCOPE_H
