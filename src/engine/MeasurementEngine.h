#ifndef MEASUREMENTENGINE_H
#define MEASUREMENTENGINE_H
// MeasurementEngine — rate(s/d)·beat error(ms)·amplitude(°) 측정 계산 (MainWindow 에서 추출).
//  A/C 이벤트(절대 샘플 위치)를 받아 롤링 통계로 측정값을 산출한다. UI/Qt 위젯 의존 없음:
//  rate 그래프 데이터는 시리즈(QVector)로 '노출'만 하고, 실제 그리기는 호출측(뷰)이 담당한다.
//  → 측정 '계산'과 '표시'의 관심사 분리(SRP/SoC).
#include "RollingLeastSquares.h"
#include "RollingAverage.h"
#include <QVector>
#include <cstdint>

class MeasurementEngine
{
public:
    // rate 그래프 어느 시리즈가 갱신됐는지 — 호출측이 해당 그래프만 setData/replot 한다.
    enum SeriesUpdate { NoUpdate, TicUpdated, TocUpdated };

    struct Results {
        bool   bphValid       = false;  int    bph          = 0;
        bool   rateValid      = false;  double rateSecPerDay = 0.0;
        bool   beatErrorValid = false;  double beatErrorMs   = 0.0;
        bool   amplitudeValid = false;  double amplitudeDeg  = 0.0;
    };

    MeasurementEngine();
    ~MeasurementEngine();
    MeasurementEngine(const MeasurementEngine&) = delete;
    MeasurementEngine& operator=(const MeasurementEngine&) = delete;

    // 이벤트 계산 직전 현재 설정을 반영(샘플레이트·평균구간·lift angle).
    void setConfig(int sampleRateHz, int averagingPeriodSec, int liftAngleDeg);
    void reset();                                                  // 측정 상태/시리즈 비움(UI 무관)

    SeriesUpdate onAEvent(double aEventSample, bool haveValidBph, double bph); // rate + beat error
    void         onCEvent(double cEventSample, bool haveValidBph, double bph); // amplitude

    Results results() const;
    int     maxDataPoints() const { return mRate.MaxTicTocDataPoints; }
    const QVector<double>& ticX() const { return mRate.xTic; }
    const QVector<double>& ticY() const { return mRate.yTic; }
    const QVector<double>& tocX() const { return mRate.xToc; }
    const QVector<double>& tocY() const { return mRate.yToc; }

    // 진폭 공식(무상태) — 스코프 마커 라벨 등에서도 쓰이므로 public static.
    static double amplitude(double liftAngleDeg, double t1Sec, double bph);

private:
    SeriesUpdate computeRateError(double aEventSample, bool haveValidBph, double bph);
    void computeBeatError(double aEventSample, bool haveValidBph, double bph);
    void computeAmplitude(double cEventSample, bool haveValidBph, double bph);
    static void   addOrOverwrite(QVector<double>& xv, QVector<double>& yv,
                                 double xValue, double yValue, int maxS, int& index);
    static double wrapInToRange(double number, double lowerBound, double upperBound);

    struct RateState {
        uint64_t TicTocBeatNumber = 0;
        QVector<double> xTic, xToc, yTic, yToc;
        int    xTicIndex = 0, xTocIndex = 0;
        bool   HaveStartTime = false, HaveZeroOffset = false;
        double StartTime = 0.0, ZeroOffsetValue = 0.0;
        int    MaxTicTocDataPoints = 0;
        RollingLeastSquares *RlsTicRate = nullptr, *RlsTocRate = nullptr;
        double RlsRate = 0.0; bool RlsRateValid = false;
        int    BPH = 0; bool BPH_Valid = false; int WatchHertz = 0;
    } mRate;
    struct BeatState {
        double BeatErrorTimes[3] = {0,0,0}; int BeatErrorIdx = 0; double BeatErrorMs = 0.0;
        RollingAverage *RollBeatError = nullptr;
    } mBeat;
    struct AmpState {
        double Last_A_Event = 0.0; bool Have_A_Event = false;
        RollingAverage *RollAmplitude = nullptr;
        double Amplitude_Tic = 0.0, Amplitude_Toc = 0.0; bool Amplitude_Tic_Valid = false;
    } mAmp;

    int mSampleRate = 48000, mAveragingPeriod = 20, mLiftAngle = 52;
};
#endif // MEASUREMENTENGINE_H
