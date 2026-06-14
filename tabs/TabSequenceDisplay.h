#ifndef TABSEQUENCEDISPLAY_H
#define TABSEQUENCEDISPLAY_H
// Multi-Position Sequence 탭 (FR-MPS): 포지션별 Rate/Beat/Ampl 캡처(최대 10) +
//  요약행 X(평균)·D(최대-최소)·DVH(수직-수평차)·Di(수직 불균형). (display_tab.pdf 시퀀스 표 구성)
//  ※ 하드웨어가 포지션을 USB로 전송하지 않음(EXP-12) → 수동 포지션 선택 + 캡처.
#include "TabView.h"
class QTableWidget;
class QComboBox;
class QPushButton;
class QLabel;

class TabSequenceDisplay : public TabView
{
    Q_OBJECT
public:
    explicit TabSequenceDisplay(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Sequence"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
private:
    void capture();
    void recomputeSummary();
    void updateComplete();                          // 6개 핵심 포지션 모두 캡처 시 완료 표시
    static bool isHorizontal(const QString &pos);   // DU/DD(다이얼) = 수평
    QTableWidget *mTable   = nullptr;   // 포지션 행: Position | Rate | Beat | Ampl
    QTableWidget *mSummary = nullptr;   // 요약 행: X | D | DVH | Di
    QComboBox    *mPos     = nullptr;
    QPushButton  *mCapture = nullptr;
    QPushButton  *mClear   = nullptr;
    QLabel       *mLive    = nullptr;
    QLabel       *mComplete = nullptr;  // FR-MPS: 시퀀스 완료 시 녹색 "Ok"
    MeasurementSnapshot mLast;
    bool mHaveLast = false;
    static constexpr double kUnbalanceSd = 10.0;   // 수직 rate 산포(Di) 불균형 경고 임계(s/d)
};
#endif // TABSEQUENCEDISPLAY_H
