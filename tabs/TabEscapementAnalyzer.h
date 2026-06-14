#ifndef TABESCAPEMENTANALYZER_H
#define TABESCAPEMENTANALYZER_H
// Escapement Analyzer & Marker-Line 탭 (FR-EAM) — Project Plan §Escapement Analyzer:
//  최근 한 비트의 파형 + A/C 수직 마커 + ms 라벨. C 는 onset(점선)과 peak(실선) 두 기준을
//  나란히 표시해 "어느 기준점이 더 반복적인가"를 비교할 수 있게 한다(Plan 명시 요구).
//  임계선(threshold %)은 창 최대값 대비 % 정규화(X_norm)로 표시한다.
#include "TabView.h"
#include "WaveBuffer.h"
class QCustomPlot;
class QLabel;
class QSpinBox;
class ReadoutBar;

class TabEscapementAnalyzer : public TabView
{
    Q_OBJECT
public:
    explicit TabEscapementAnalyzer(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Escapement"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onWave(const WaveBlock &wave) override;
    void onResetSession() override;
protected:
    void onShown() override;
private:
    void render();
    ReadoutBar  *mBar  = nullptr;
    QCustomPlot *mPlot = nullptr;
    QLabel      *mInfo = nullptr;
    QSpinBox    *mThresh = nullptr;   // 임계 % (창 최대 대비)
    WaveBuffer   mBuf;
    bool         mConfigured = false;
};
#endif // TABESCAPEMENTANALYZER_H
