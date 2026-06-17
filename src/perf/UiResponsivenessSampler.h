#ifndef UIRESPONSIVENESSSAMPLER_H
#define UIRESPONSIVENESSSAMPLER_H
// [PERF 계측 · §A-3 · QA-RT-01] UI 응답성(이벤트 루프 지연) 상시 샘플러.
//  주기 타이머(기본 100ms)가 '실제로 얼마나 늦게' 발화하는지 = 메인 스레드가 막힌 시간을
//  정량화한다. ProcessSamples/replot 등이 메인 스레드를 점유하면 타이머가 늦게 불려 그 초과분이
//  곧 UI 비응답 시간. 과거 MainWindow 에 박혀 있던 계측 책임을 분리(UI=표시, 계측=여기 / SRP).
#include <QObject>
class QTimer;

class UiResponsivenessSampler : public QObject
{
    Q_OBJECT
public:
    explicit UiResponsivenessSampler(QObject *parent = nullptr, int periodMs = 100);

private slots:
    void sample();   // 타이머 발화 시: 명목 주기 대비 초과 지연을 Perf 에 기록

private:
    int    mPeriodMs;
    double mLastMs = 0.0;
    bool   mHave   = false;   // 첫 회(워밍업)는 간격 산출 불가 → 제외
};

#endif // UIRESPONSIVENESSSAMPLER_H
