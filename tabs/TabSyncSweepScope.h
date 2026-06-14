#ifndef TABSYNCSWEEPSCOPE_H
#define TABSYNCSWEEPSCOPE_H
// Scope Mode — Synchronized Sweep 탭 (FR-SMS) + Multiple Filter Views F0~F3 (FR-SFM).
//  [FR-SMS · Plan §Scope Mode] sweep 창 = 틱 주기의 배수(1~4). 스윕은 '프리러닝'(T_anchor +
//   k·sweep 에서 재시작) — 시계가 정시면 비트 패턴이 정지, 빠르면/느리면 화면을 가로질러
//   드리프트한다(매 렌더마다 A 에 재정렬하면 드리프트가 절대 보이지 않으므로 금지).
//  [FR-SFM · Plan §Scope Function] F0 = 원신호(평균 기준 미러), F1 = F0 이동평균(평활),
//   F2 = F1 기반 상승 기울기 강조·하강 감쇠(T3·T2 부각), F3 = 평균 위 상단부 + 상승 에지
//   강조(T1·T3 식별). 추가로 view-only 밴드패스(BP 2~10kHz) 1종 제공.
#include "TabView.h"
#include "WaveBuffer.h"
class QCustomPlot;
class QComboBox;
class QSpinBox;
class QLabel;
class QCheckBox;
class QWidget;
class ReadoutBar;

class TabSyncSweepScope : public TabView
{
    Q_OBJECT
public:
    explicit TabSyncSweepScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Sync Sweep"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    void render();
    // 한 패널에 지정 필터(mode 0=F0…3=F3, 4=BP)로 sweep 창을 그림(단일/4-패널 공용).
    void drawPanel(QCustomPlot *p, int mode, const QVector<double> &rawFull,
                   int warm, int sweep, int sr, uint64_t from, bool quadLabel);
    ReadoutBar  *mBar    = nullptr;
    QCustomPlot *mPlot   = nullptr;     // 단일 보기
    QComboBox   *mFilter = nullptr;
    QSpinBox    *mBeats  = nullptr;
    QCheckBox   *mQuadMode = nullptr;   // FR-SFM: F0~F3 4-패널 동시 비교
    QCheckBox   *mPause    = nullptr;   // Pause/Scope: 화면 정지
    QWidget     *mQuadBox  = nullptr;   // 4-패널 컨테이너
    QCustomPlot *mQuad[4]  = {nullptr,nullptr,nullptr,nullptr};
    QLabel      *mInfo   = nullptr;
    WaveBuffer   mBuf;       // 엔벨로프(이벤트/마커/동기용)
    WaveBuffer   mRawBuf;    // 원신호(F0~F3 필터 뷰용)
    bool         mConfigured = false;
    // 프리러닝 스윕 앵커: 동기 후 첫 A 이벤트 샘플(세션 리셋 시 해제).
    uint64_t     mSweepAnchor = 0; bool mHaveAnchor = false;
};
#endif // TABSYNCSWEEPSCOPE_H
