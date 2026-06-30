#ifndef TABSOUNDPRINT_H
#define TABSOUNDPRINT_H
// Sound Print 탭 — 폴딩 사운드 이미지(비트 주기로 접은 음향 '지문') 표시.
//  과거에는 MainWindow.ui의 정적 SoundTab + MainWindow::ProcessSamples 인라인 렌더링이었으나,
//  다른 디스플레이 탭과 동일하게 TabView 추상화로 분리한다(SRP·일관성).
//  데이터는 onWave(원신호 raw·이벤트·BPH)만으로 자기완결적으로 렌더링한다.
#include "TabView.h"
#include "SoundImageRenderer.h"
class SoundImageWidget;

class TabSoundPrint : public TabView
{
    Q_OBJECT
public:
    explicit TabSoundPrint(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Sound Print"); }
    void onWave(const WaveBlock &wave) override;
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
    bool resetOnResume() const override { return true; }   // [pause→start] resume 시 그래프·데이터 초기화
protected:
    void onShown() override;
private:
    void ensureRenderer(quint64 originSample);              // 샘플레이트 확정 시 고정 캔버스로 렌더러 1회 초기화(절대 샘플 원점 지정)
    SoundImageRenderer::Config makeConfig(int sampleRateHz) const;
    SoundImageWidget   *mImage = nullptr;
    SoundImageRenderer  mRenderer;
    bool   mInitialized  = false;
    bool   mHasBph       = false;   // BPH 확정 여부(확정 후에만 컬럼 렌더·마커 표시)
    int    mSampleRateHz = 48000;
};
#endif // TABSOUNDPRINT_H
