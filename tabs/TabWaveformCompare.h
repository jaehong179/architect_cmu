#ifndef TABWAVEFORMCOMPARE_H
#define TABWAVEFORMCOMPARE_H
// Waveform Comparison 탭 (FR-WCD) — Marcello Mamino Tg(https://tg.ciovil.li) 구도 재현:
//  · C(lock) 피크를 x=0 에 정렬, 음수 ms 가 왼쪽(−25..+8ms) — 최근 N개 비트를 세로 레인으로 비교.
//  · 바이폴라 원신호(흰색 채움, 검정 배경) — 레인별 정규화로 각 레인을 꽉 채워 형태 비교가 쉽다.
//  · 신호 분해(Plan): 엔벨로프 ±곡선(녹색) 오버레이 + A→C 구간 파란 하이라이트 + t_AC ms 라벨.
//  · 도(°) 그리드(E9): x(α) = −t_AC(α) = −3600λ/(π·n·α). 10° 간격 녹색 세선,
//    150/200/250/300° 빨간 기준선 + 라벨 — A onset 이 닿는 그리드 선이 곧 진폭.
//  · 측정 진폭 위치 = 파란 굵은 선(정상이면 파형 onset 과 겹침). 수치(rate·BE·bph)는 ReadoutBar.
#include "TabView.h"
#include "WaveBuffer.h"
class QCustomPlot;
class QLabel;
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
    // 레인 k 의 그래프 인덱스: raw 채움용 4개 한 묶음.
    int gRaw(int k)  const { return k * 4 + 0; }   // 원신호(흰색, 베이스라인까지 채움)
    int gBase(int k) const { return k * 4 + 1; }   // 베이스라인(채움 기준, 투명)
    int gEnvU(int k) const { return k * 4 + 2; }   // +엔벨로프(녹색)
    int gEnvD(int k) const { return k * 4 + 3; }   // −엔벨로프(녹색)
    ReadoutBar  *mBar  = nullptr;
    QCustomPlot *mPlot = nullptr;
    QLabel      *mInfo = nullptr;
    WaveBuffer   mBuf;       // 엔벨로프 + 이벤트(C 정렬 기준, 분해 오버레이)
    WaveBuffer   mRawBuf;    // 바이폴라 원신호(표시용)
    bool         mConfigured = false;
    int          mLiftAngle = 52;
    double       mAmpDeg = 0.0; bool mAmpValid = false;
    static constexpr int    kLanes  = 4;      // 비교할 최근 비트 수
    static constexpr double kPreMs  = 25.0;   // C 이전 표시(ms) — 도 그리드 영역
    static constexpr double kPostMs = 8.0;    // C 이후 표시(ms)
};
#endif // TABWAVEFORMCOMPARE_H
