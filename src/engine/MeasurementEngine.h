#ifndef MEASUREMENTENGINE_H
#define MEASUREMENTENGINE_H
// MeasurementEngine — rate(s/d)·beat error(ms)·amplitude(°) 측정 계산 (MainWindow 에서 추출).
//  A/C 이벤트(절대 샘플 위치)를 받아 롤링 통계로 측정값을 산출한다. UI/Qt 위젯 의존 없음:
//  rate 그래프 데이터는 시리즈(QVector)로 '노출'만 하고, 실제 그리기는 호출측(뷰)이 담당한다.
//  → 측정 '계산'과 '표시'의 관심사 분리(SRP/SoC).
#include "RollingLeastSquares.h"
#include "RollingAverage.h"
#include "RollingOutlier.h"   // 지표별 이상치 검출(median+MAD) — 평균에서 제외 + snapshot 플래그
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
        // [이상치] 직전 이벤트의 지표별 이상치 여부(평균/RLS 계산에서는 제외됨). 표시 마킹용.
        bool   rateOutlier      = false;
        bool   beatErrorOutlier = false;
        bool   amplitudeOutlier = false;
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
    // [이상치] tic/toc 점별 이상치 표식용 — 이상치면 그 y, 아니면 NaN (x 는 ticX/tocX 공용).
    const QVector<double>& ticOutY() const { return mRate.yTicOut; }
    const QVector<double>& tocOutY() const { return mRate.yTocOut; }
    // [이상치] 직전 onAEvent 가 rate 이상치였는지 — 소스에서 WaveEvent 에 플래그 박을 때 즉시 조회용.
    bool lastRateOutlier() const { return mRate.LastOutlier; }

    // 진폭 공식(무상태) — 스코프 마커 라벨 등에서도 쓰이므로 public static.
    static double amplitude(double liftAngleDeg, double t1Sec, double bph);

private:
    SeriesUpdate computeRateError(double aEventSample, bool haveValidBph, double bph);
    void computeBeatError(double aEventSample, bool haveValidBph, double bph);
    void computeAmplitude(double cEventSample, bool haveValidBph, double bph);
    static void   addOrOverwrite(QVector<double>& xv, QVector<double>& yv, QVector<double>& ov,
                                 double xValue, double yValue, double outlierValue, int maxS, int& index);
    static double wrapInToRange(double number, double lowerBound, double upperBound);

    struct RateState {
        uint64_t TicTocBeatNumber = 0;
        QVector<double> xTic, xToc, yTic, yToc;
        QVector<double> yTicOut, yTocOut;     // [이상치] 점별 표식(이상치 y / 아니면 NaN)
        int    xTicIndex = 0, xTocIndex = 0;
        bool   HaveStartTime = false, HaveZeroOffset = false;
        double StartTime = 0.0, ZeroOffsetValue = 0.0;
        int    MaxTicTocDataPoints = 0;
        RollingLeastSquares *RlsTicRate = nullptr, *RlsTocRate = nullptr;
        // [이상치] 검출 전용 단기 추세 적합선 — rate 곡선의 국소 직선만 보고 잔차를 구해 곡률 오검 방지.
        //  (측정용 RlsTic/TocRate 는 긴 윈도우 그대로. 이건 짧은 윈도우 별도.)
        RollingLeastSquares *DetrendTic = nullptr, *DetrendToc = nullptr;
        double RlsRate = 0.0; bool RlsRateValid = false;
        int    BPH = 0; bool BPH_Valid = false; int WatchHertz = 0;
        // [이상치] rate(InstTimingError)는 추세가 있어, RLS 적합선으로 추세를 제거한 '잔차'로 검출한다
        //  → 위치 무관하게 균일 민감도. 잔차는 0 주변 정상신호라 윈도우를 충분히 길게 둔다.
        //  k=4.0(임계 위 가짜 컷) + madFloor 5e-6 s(=0.005 ms; 잔차는 초 단위) → 베이스라인 collapse 가짜 차단.
        RollingOutlier Outlier{60, 4.0, 12, 5e-6}; bool LastOutlier = false;
    } mRate;
    struct BeatState {
        double BeatErrorTimes[3] = {0,0,0}; int BeatErrorIdx = 0; double BeatErrorMs = 0.0;
        RollingAverage *RollBeatError = nullptr;
        RollingOutlier Outlier; bool LastOutlier = false;   // [이상치] beat error 급변
    } mBeat;
    struct AmpState {
        double Last_A_Event = 0.0; bool Have_A_Event = false;
        RollingAverage *RollAmplitude = nullptr;
        double Amplitude_Tic = 0.0, Amplitude_Toc = 0.0; bool Amplitude_Tic_Valid = false;
        RollingOutlier Outlier; bool LastOutlier = false;   // [이상치] amplitude 급변
    } mAmp;

    int mSampleRate = 48000, mAveragingPeriod = 20, mLiftAngle = 52;
};
#endif // MEASUREMENTENGINE_H
