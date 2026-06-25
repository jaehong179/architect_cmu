#ifndef TABSEQUENCEDISPLAY_H
#define TABSEQUENCEDISPLAY_H
// Multi-Position Sequence 탭 (FR-MPS): 포지션별 Rate/Beat/Ampl 캡처(최대 10) +
//  요약행 X(평균)·D(최대-최소)·DVH(수직-수평차)·Di(수직 불균형). (display_tab.pdf 시퀀스 표 구성)
//  ※ 하드웨어가 포지션을 USB로 전송하지 않음(EXP-12) → 수동 포지션 선택 + 캡처.
#include "TabView.h"
#include "RadarChartWidget.h"

class QTableWidget;
class QComboBox;
class QPushButton;
class QLabel;
class PositionTimingModel;
class QModelIndex;

class TabSequenceDisplay : public TabView
{
    Q_OBJECT
public:
    explicit TabSequenceDisplay(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Multi-Position Sequence Display"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
    void setTimingModel(PositionTimingModel *model);

public slots:
    void setCurrentPositionByIndex(int index);
    void setPhaseStatus(const QString &phaseLabel, int remainingSec);
    void onPositionComboChanged(int index);
    void onTimingDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void onTimingModelReset();
    void onRunningStateChanged(bool isRunning);

private:
    void capture();
    void recomputeSummary();
    void updateComplete();                          // 6개 핵심 포지션 모두 캡처 시 완료 표시
    void updateRadarChart();                        // 차트 실시간 및 정적 데이터 갱신 헬퍼
    static bool isHorizontal(const QString &pos);   // DU/DD(다이얼) = 수평
    int getRowIndexForPosition(const QString &posName) const;

    QTableWidget     *mTable   = nullptr;   // 포지션 행: Position | Rate | Beat | Ampl (고정 8행)
    RadarChartWidget *mRadar   = nullptr;   // 극좌표 레이더 차트 위젯
    QComboBox        *mPos     = nullptr;
    QPushButton      *mCapture = nullptr;
    QPushButton      *mClear   = nullptr;
    QLabel           *mLive    = nullptr;
    QLabel           *mComplete = nullptr;  // FR-MPS: 시퀀스 완료 시 녹색 "Ok"
    MeasurementSnapshot mLast;
    bool mHaveLast = false;
    QString mPrevPos;
    static constexpr double kUnbalanceSd = 10.0;   // 수직 rate 산포(Di) 불균형 경고 임계(s/d)

    PositionTimingModel *mTiming = nullptr;
    QComboBox           *mMeasTimeCombo = nullptr;
};
#endif // TABSEQUENCEDISPLAY_H
