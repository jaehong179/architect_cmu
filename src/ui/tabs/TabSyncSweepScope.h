#ifndef TABSYNCSWEEPSCOPE_H
#define TABSYNCSWEEPSCOPE_H
// Scope Mode — Synchronized Sweep 탭 (FR-SMS) — scope_mode.c 구도.
//  sweep 창 = 틱 주기의 배수(NBEATS)에 folded 신호 |raw-avg|(양·음 절반 합침=정류)를
//  바닥에서 위로 채워 그린다(오실로스코프 그래스). 스윕은 '프리러닝'(T_anchor + k·sweep 에서
//  재시작) — 시계가 정시면 비트 패턴이 정지, 빠르면/느리면 화면을 가로질러 드리프트한다
//  (매 렌더마다 A 에 재정렬하면 드리프트가 절대 보이지 않으므로 금지).
//  필터(F0~F3) 비교는 별도 'Scope Function with Multiple Filter Views' 탭.
#include "TabView.h"
#include "WaveBuffer.h"
class QCustomPlot;
class QSpinBox;
class QLabel;
class QCheckBox;
class WaveLodHistory;   // [③] 8분 이력(중앙) — seek replay

class TabSyncSweepScope : public TabView
{
    Q_OBJECT
public:
    explicit TabSyncSweepScope(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Sync Sweep Scope"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
    void onSeek(double absSample) override;            // [③] 정지 중 트렌드 클릭 → 그 시점 표시    void onResumeLive(bool seeked) override { if (seeked) { mBuf.clear(); mRawBuf.clear(); } }   // seek 했으면 버퍼 비움
    void setHistory(WaveLodHistory *h) { mHistory = h; }
protected:
    void onShown() override;
private:
    WaveLodHistory *mHistory = nullptr;
    void render();
    QCustomPlot *mPlot   = nullptr;     // folded 그래스 sweep 보기
    QSpinBox    *mBeats  = nullptr;     // sweep 창 = NBEATS
    QLabel      *mInfo   = nullptr;
    WaveBuffer   mBuf;       // 엔벨로프(동기/박자용)
    WaveBuffer   mRawBuf;    // 원신호(folded 표시용)
    bool         mConfigured = false;
    double       mNorm = 0.0;   // 스무딩 피크(축 고정용 정규화 — AGC식 천천히 적응)
    // 프리러닝 스윕 앵커: 동기 후 첫 A 이벤트 샘플(세션 리셋 시 해제).
    uint64_t     mSweepAnchor = 0; bool mHaveAnchor = false;
};
#endif // TABSYNCSWEEPSCOPE_H
