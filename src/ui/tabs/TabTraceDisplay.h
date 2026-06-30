#ifndef TABTRACEDISPLAY_H
#define TABTRACEDISPLAY_H
// Trace Display 탭 (FR-TD) — Witschi Trace 스타일: 상단 readout + rate(상)·amplitude(하) 2단 스택.
//  s/d 스무딩(FR-TD-2), late 알림(FR-TD-3), amplitude 270~300° 알림+시각 밴드(FR-TD-4),
//  세션/롤링 평균(FR-TD-6), 파생 측정 DiffTicTac/DiffPeriod/AvgPeriod (Project Plan §Expected Enhancements).
#include "TabView.h"
#include <QVector>
class QCustomPlot;
class QCPItemRect;
class QCPItemLine;
class QCPItemTracer;
class QCPItemText;
class QLabel;

class TabTraceDisplay : public TabView
{
    Q_OBJECT
public:
    explicit TabTraceDisplay(QWidget *parent = nullptr);
    QString tabTitle() const override { return QStringLiteral("Trace Display"); }
    void onMeasurement(const MeasurementSnapshot &snap) override;
    void onResetSession() override;
    void onSeek(double absSample) override;   // [③] 다른 탭 seek → 커서 동기화
    void onSeekClear() override;              // [③] 선택 해제 → 커서 숨김
    void onResumeLive(bool seeked) override { (void)seeked; onSeekClear(); }  // [③] resume → 선택 리셋(커서 숨김)
signals:
    // [③] 정지 중 트렌드 그래프 클릭 → 해당 절대 샘플 시점(스코프 탭들이 점프).
    void seekRequested(double absSample);
protected:
    void onShown() override;
private:
    // 클릭한 x(초) → 가장 가까운 측정점의 절대 샘플 인덱스(totalSamples).
    double sampleAtX(double xSeconds) const;
    void   showCursor(double xSeconds, double labelSample);   // [③] 커서 위치(x초) + 라벨용 절대샘플(타 탭 통일)
    // [이상치] 엔진 snapshot 플래그 → 배경 빨강 음영 마킹(상승엣지 1회). rate/amplitude 각각.
    void   redrawAnomalies(QCustomPlot *plot, QVector<double> &anomX, QVector<QCPItemRect*> &marks);
    QVector<double> mRateAnomX, mAmpAnomX;
    QVector<QCPItemRect*> mRateAnomMarks, mAmpAnomMarks;
    bool mRatePrevOut = false, mAmpPrevOut = false;
    static constexpr double kAnomBandSec = 1.5;   // 마킹 배경 폭(초)
    static constexpr int    kMaxAnomMarks = 256;
    static constexpr bool   kShowAnomalyShade = false;  // 빨강 음영 표시(현재 off; 코드/검출은 보존)
    QVector<QPair<double,double>> mXtoSample;   // (x초, totalSamples) — 클릭→시점 변환용
    QCPItemLine         *mCurRate = nullptr;    // 롤리팝 줄기(상단)
    QCPItemTracer       *mCurRateHead = nullptr;
    QCPItemText         *mCurRateTip  = nullptr;
    QCPItemLine         *mCurAmp  = nullptr;    // 롤리팝 줄기(하단)
    QCPItemTracer       *mCurAmpHead = nullptr;
    QCPItemText         *mCurAmpTip  = nullptr;
    QCustomPlot *mRate  = nullptr;   // 상단: rate(raw+smoothed)
    QCustomPlot *mAmp   = nullptr;   // 하단: amplitude
    QCPItemRect *mAmpBand = nullptr; // 진폭 정상범위(270~300°) 시각 밴드
    QLabel      *mAlert = nullptr;
    QLabel      *mDerived = nullptr; // 파생 측정(DiffTicTac/DiffPeriod/AvgPeriod) + 롤링/세션 평균
    double       mT0 = 0.0; bool mHaveT0 = false;
    QVector<double> mRateWin; int mSmoothN = 20;
    double mRateSum=0, mAmpSum=0; long mRateN=0, mAmpN=0;
    // 파생 측정: 비트 주기 편차(ms) — DiffPeriod(최근 4초 창) / AvgPeriod(세션 누적).
    QVector<QPair<double,double>> mDevWin;   // (x초, 편차ms) 최근 4초
    double mDevSum = 0; long mDevN = 0;
    static constexpr double kAmpLo=270.0, kAmpHi=300.0, kLateSlow=-1.0;
    static constexpr double kDiffPeriodWinS = 4.0;   // Chour DiffPeriod 창(초)
    // 그래프 무한 누적 방지: 최근 kHistorySec 구간만 유지(장기추이는 Long-Term 탭 담당).
    //  지속 관찰용 — 8분 보존(넘으면 오토스케일이 최근 8분으로 흘러감). 장기추이는 Long-Term(10분).
    static constexpr double kHistorySec = 480.0;     // rate/amp 플롯 보존 시간(초, 8분)
};
#endif // TABTRACEDISPLAY_H
