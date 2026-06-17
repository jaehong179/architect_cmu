#ifndef TABFILTERVIEWS_H
#define TABFILTERVIEWS_H
// Scope Function with Multiple Filter Views 탭 (FR-SFM) — Project Plan 사양.
//  박자(T1) 정렬 고정창에서 F0~F3 네 필터를 테두리 칸(1×4)으로 동시에 보여 T1~T3 식별을 비교한다:
//   F0 평균기준 미러 · F1 이동평균(평활) · F2 상승강조+지수감쇠(T3·T2) · F3 정류+에지+포물선감쇠(T1·T3).
//   표시: F0·F1·F2 미러(±), F3 upper(정류). 필터 수식은 ScopeFilters(공용 DSP).
//  원본 파형은 별도 'Scope Mode with Synchronized Sweep Display' 탭.
//  [성능] 필터는 공통 계산 1회로 4종 산출, 마커/라벨은 객체 풀 재사용, 렌더는 ~30FPS 스로틀.
#include "TabView.h"
#include "WaveBuffer.h"
#include <QVector>
#include <QElapsedTimer>
class QCustomPlot;
class QCPItemLine;
class QCPItemText;
class QLabel;
class QCheckBox;
class ReadoutBar;

class TabFilterViews : public TabView
{
    Q_OBJECT
public:
    explicit TabFilterViews(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Scope Function with Multiple Filter Views"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    void render();
    // 한 패널에 F_k 출력을 미러(F0·F1·F2)/upper(F3)로 그리고 T1/T2/T3 마커를 표시.
    //  x 는 박자(T1, anchor) 기준 ms(0=T1). preS = 창 시작~anchor 샘플수. pulses = rawFull 공간 인덱스.
    void drawPanel(int k, const QVector<double> &out, int warm, int sweep, int sr, int preS, const int pulses[3]);
    ReadoutBar  *mBar      = nullptr;
    QCustomPlot *mQuad[4]  = {nullptr, nullptr, nullptr, nullptr};   // F0|F1|F2|F3 가로 1×4(칸별)
    QVector<QCPItemLine *> mMarks[4];                               // T1/T2/T3 마커선 풀(재사용)
    QVector<QCPItemText *> mTLabels[4];                                // T1/T2/T3 라벨 풀(재사용)
    QCheckBox   *mPause    = nullptr;   // 화면 정지
    QLabel      *mInfo     = nullptr;
    WaveBuffer   mBuf;       // 엔벨로프(이벤트/마커/동기용)
    WaveBuffer   mRawBuf;    // 원신호(F0~F3 필터 입력)
    bool         mConfigured = false;
    QElapsedTimer mThrottle;            // 렌더 빈도 제한(~30FPS)
    double mNorm[4] = {0, 0, 0, 0};     // 패널별 스무딩 피크(축 고정용 정규화 — AGC식 천천히 적응)
    // 박자(T1) 기준 짧은 고정 줌 창: 앞 kPreMs ~ 뒤 kPostMs(한 버스트). 항상 틱이 패널을 채움.
    static constexpr double kPreMs  = 5.0;
    static constexpr double kPostMs = 40.0;
};
#endif // TABFILTERVIEWS_H
