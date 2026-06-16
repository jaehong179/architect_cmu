#ifndef TABSYNCSWEEPSCOPE_H
#define TABSYNCSWEEPSCOPE_H
// Scope Mode — Synchronized Sweep 탭 (FR-SMS).
//  sweep 창 = 틱 주기의 배수(1~4)에 원신호(평균 기준 미러)를 그린다. 스윕은 '프리러닝'
//  (T_anchor + k·sweep 에서 재시작) — 시계가 정시면 비트 패턴이 정지, 빠르면/느리면 화면을
//  가로질러 드리프트한다(매 렌더마다 A 에 재정렬하면 드리프트가 절대 보이지 않으므로 금지).
//  필터(F0~F3·BP) 비교는 별도 'Scope Function with Multiple Filter Views' 탭.
#include "TabView.h"
#include "WaveBuffer.h"
class QCustomPlot;
class QSpinBox;
class QLabel;
class QCheckBox;
class ReadoutBar;

class TabSyncSweepScope : public TabView
{
    Q_OBJECT
public:
    explicit TabSyncSweepScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Scope Mode with Synchronized Sweep Display"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    void render();
    ReadoutBar  *mBar    = nullptr;
    QCustomPlot *mPlot   = nullptr;     // 단일 sweep 보기(원신호)
    QSpinBox    *mBeats  = nullptr;
    QCheckBox   *mPause  = nullptr;     // Pause/Scope: 화면 정지
    QLabel      *mInfo   = nullptr;
    WaveBuffer   mBuf;       // 엔벨로프(이벤트/마커/동기용)
    WaveBuffer   mRawBuf;    // 원신호(표시용)
    bool         mConfigured = false;
    // 프리러닝 스윕 앵커: 동기 후 첫 A 이벤트 샘플(세션 리셋 시 해제).
    uint64_t     mSweepAnchor = 0; bool mHaveAnchor = false;
};
#endif // TABSYNCSWEEPSCOPE_H
