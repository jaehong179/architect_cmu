#ifndef TABWAVEFORMCOMPARE_H
#define TABWAVEFORMCOMPARE_H
// Waveform Comparison 탭 (FR-WCD) — Marcello Mamino Tg(https://tg.ciovil.li) 스코프 구도 재현.
//  · 최근 비트들을 각각 독립 패널(스코프)로 위→아래 스택해 비교.
//  · 각 패널: 검정 배경, 흰 파형을 중심선(y=0) 기준 상·하 대칭 미러(|신호| 위/아래 채움) → 다이아몬드형.
//  · 왼쪽 도(°) 그리드(E9): x(α) = −t_AC(α) = −3600λ/(π·n·α). 10° 녹색 세선,
//    150/200/250/300° 빨강 기준선 + 상단 ° 라벨. 측정 진폭 = 파랑 굵은 세선.
//  · x = ms (0 = C peak), 하단 ms 축. 수치(rate·BE·bph)는 ReadoutBar.
#include "TabView.h"
#include "WaveBuffer.h"
#include <QVector>
class QCustomPlot;
class QLabel;
class QSlider;
class ReadoutBar;

class TabWaveformCompare : public TabView
{
    Q_OBJECT
public:
    explicit TabWaveformCompare(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Waveform Cmp"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    void render();
    QCustomPlot *makePanel();                  // Tg 스코프 패널 1개 생성(배경/축/그래프)
    void renderPanel(QCustomPlot *p, quint64 c); // C 피크 c 기준 한 비트를 패널에 그림
    void renderOverview(const QVector<quint64> &cs, int selFrom, int selTo); // 하단 개요(전체 비트 + 선택 강조)
    void renderStrip();                        // 좌측 세로 trace 스트립(최근 엔벨로프 세로 스크롤)
    ReadoutBar  *mBar  = nullptr;
    QLabel      *mInfo = nullptr;
    QSlider     *mCursorSld = nullptr;         // 파랑 커서 위치(ms) 조절
    QCustomPlot *mStrip = nullptr;             // 좌측 세로 trace 스트립(개요)
    QVector<QCustomPlot*> mPanels;             // 최근 비트별 스코프 패널(위=최근)
    QCustomPlot *mOverview = nullptr;          // 하단 개요 패널(넓은 구간 + 파랑 선택 강조)
    WaveBuffer   mBuf;       // 엔벨로프 + 이벤트(C 정렬 기준)
    WaveBuffer   mRawBuf;    // 바이폴라 원신호(표시용)
    bool         mConfigured = false;
    int          mLiftAngle = 52;
    double       mAmpDeg = 0.0; bool mAmpValid = false;
    int          mBeatOffset = 0;             // 표시 시작 비트(최근에서 뒤로 N): 비트 선택/정렬
    double       mCursorMs = -10.0;           // 파랑 커서 위치(ms)
    static constexpr int    kPanels = 2;      // 비교 패널 A·B (사양: 2개) + 하단 개요
    static constexpr double kPreMs  = 20.0;   // C 이전 표시(ms) — 도 그리드 영역 (사양 −20)
    static constexpr double kPostMs = 5.0;    // C 이후 표시(ms) (사양 +5)
};
#endif // TABWAVEFORMCOMPARE_H
