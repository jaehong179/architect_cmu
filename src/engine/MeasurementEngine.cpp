#include "MeasurementEngine.h"
#include <QtMath>
#include <cmath>

// [이상치] 운영 토글 — 현재는 Rate 만 검출/평균제외/표시. beat error·amplitude 는 로직을 보존하되
//  비활성(아래 false→true 로 즉시 부활). false 면 push() 자체가 단락되어 검출/로그/평균제외 모두 안 함.
static constexpr bool kRateOutlierEnabled = true;
static constexpr bool kBeatOutlierEnabled = false;
static constexpr bool kAmpOutlierEnabled  = false;

// 측정 상수.
static constexpr double ERROR_RATE_Y_SCALE      = 10.0;
static constexpr int    ERROR_RATE_X_DATA_POINTS = 250;
static constexpr int    RLS_WINDOW_INIT          = 100;
static constexpr int    TIC = 0;
static constexpr int    TOC = 1;

MeasurementEngine::MeasurementEngine()
{
    // 롤링 통계 객체 할당 + 초기 상태.
    mAmp.Have_A_Event = false;
    mAmp.Amplitude_Tic_Valid = false;
    mAmp.RollAmplitude = new RollingAverage(10);

    mBeat.BeatErrorIdx = 0;
    mBeat.RollBeatError = new RollingAverage(10);

    mRate.BPH_Valid = false;
    mRate.MaxTicTocDataPoints = ERROR_RATE_X_DATA_POINTS;
    mRate.xTicIndex = 0;
    mRate.xTocIndex = 0;
    mRate.HaveStartTime = false;
    mRate.HaveZeroOffset = false;
    mRate.ZeroOffsetValue = 0.0;
    mRate.xTic.reserve(mRate.MaxTicTocDataPoints);
    mRate.xToc.reserve(mRate.MaxTicTocDataPoints);
    mRate.yTic.reserve(mRate.MaxTicTocDataPoints);
    mRate.yToc.reserve(mRate.MaxTicTocDataPoints);
    mRate.RlsTicRate = new RollingLeastSquares(RLS_WINDOW_INIT);
    mRate.RlsTocRate = new RollingLeastSquares(RLS_WINDOW_INIT);
    mRate.DetrendTic = new RollingLeastSquares(12);   // [이상치] 검출 전용 단기 추세(국소 직선)
    mRate.DetrendToc = new RollingLeastSquares(12);
    mRate.RlsRateValid = false;
}

MeasurementEngine::~MeasurementEngine()
{
    delete mAmp.RollAmplitude;
    delete mBeat.RollBeatError;
    delete mRate.RlsTicRate;
    delete mRate.RlsTocRate;
    delete mRate.DetrendTic;
    delete mRate.DetrendToc;
}

void MeasurementEngine::setConfig(int sampleRateHz, int averagingPeriodSec, int liftAngleDeg)
{
    mSampleRate      = sampleRateHz;
    mAveragingPeriod = averagingPeriodSec;
    mLiftAngle       = liftAngleDeg;
}

void MeasurementEngine::reset()
{
    // 상태 비움(UI/RatePlot 갱신은 호출측 담당).
    mAmp.Have_A_Event = false;
    mAmp.Amplitude_Tic_Valid = false;
    mAmp.RollAmplitude->Reset();

    mBeat.BeatErrorIdx = 0;
    mBeat.RollBeatError->Reset();

    // [이상치] 검출기·플래그 초기화
    mRate.Outlier.reset(); mBeat.Outlier.reset(); mAmp.Outlier.reset();
    mRate.LastOutlier = mBeat.LastOutlier = mAmp.LastOutlier = false;

    mRate.BPH_Valid = false;
    mRate.xTicIndex = 0;
    mRate.xTocIndex = 0;
    mRate.StartTime = 0;
    mRate.HaveStartTime = false;
    mRate.HaveZeroOffset = false;
    mRate.ZeroOffsetValue = 0.0;
    mRate.xTic.clear();
    mRate.xToc.clear();
    mRate.yTic.clear();
    mRate.yToc.clear();
    mRate.yTicOut.clear();
    mRate.yTocOut.clear();
    mRate.RlsTicRate->Reset();
    mRate.RlsTocRate->Reset();
    mRate.DetrendTic->Reset();
    mRate.DetrendToc->Reset();
    mRate.RlsRateValid = false;
}

void MeasurementEngine::addOrOverwrite(QVector<double>& xvec, QVector<double>& yvec, QVector<double>& ovec,
                                       double xValue, double yValue, double outlierValue, int maxS, int& index)
{
    if (yvec.size() < maxS) {
        xvec.append(xValue);
        yvec.append(yValue);
        ovec.append(outlierValue);   // [이상치] 같은 위치에 마스크(이상치 y / NaN)
        index = (index + 1) % maxS;
    } else {
        xvec[index] = xValue;
        yvec[index] = yValue;
        ovec[index] = outlierValue;  // [이상치] 링 위치 덮어쓰기 시 마스크도 동기 갱신
        index = (index + 1) % maxS;
    }
}

double MeasurementEngine::wrapInToRange(double number, double lower_bound, double upper_bound)
{
    double range_size = upper_bound - lower_bound;
    double shifted = number - lower_bound;
    double wrapped = std::fmod(shifted, range_size);
    if (wrapped < 0) wrapped += range_size;
    return wrapped + lower_bound;
}

MeasurementEngine::SeriesUpdate
MeasurementEngine::computeRateError(double A_EventTime, bool haveValidBPH, double BPH)
{
    SeriesUpdate upd = NoUpdate;

    if ((!haveValidBPH) && (mRate.HaveStartTime)) {
        mRate.HaveStartTime = false;
        mRate.BPH_Valid = false;
        //TODO More reset needed
    } else if ((haveValidBPH) && (!mRate.HaveStartTime)) {
        mRate.HaveStartTime = true;
        mRate.TicTocBeatNumber = 0;
        mRate.BPH_Valid = true;
        mRate.BPH = BPH;
        mRate.StartTime = A_EventTime / ((double)mSampleRate);
        mRate.HaveZeroOffset = false;
        mRate.ZeroOffsetValue = 0.0;
        mRate.RlsRateValid = false;
        mRate.WatchHertz = mRate.BPH / 3600;
        mRate.RlsTicRate->Resize(mAveragingPeriod * mRate.WatchHertz);
        mRate.RlsTocRate->Resize(mAveragingPeriod * mRate.WatchHertz);
        mRate.RlsTicRate->Reset();
        mRate.RlsTocRate->Reset();
        mRate.DetrendTic->Reset();
        mRate.DetrendToc->Reset();

        mBeat.RollBeatError->Reset();
        mAmp.RollAmplitude->Reset();
        mRate.Outlier.reset(); mBeat.Outlier.reset(); mAmp.Outlier.reset();   // [이상치] 재동기 → baseline 초기화
        mRate.LastOutlier = mBeat.LastOutlier = mAmp.LastOutlier = false;
    }
    if ((haveValidBPH) && (mRate.HaveStartTime)) {
        double InstTimingError, InstTimingErrorMs, ExpectedTimeTarget, TimeMeasured;
        int    TicOrToc;

        TimeMeasured = A_EventTime / ((double)mSampleRate);
        ExpectedTimeTarget = 3600.0f / BPH;

        mRate.TicTocBeatNumber++;
        TicOrToc = ((mRate.TicTocBeatNumber - 1) & 1);

        InstTimingError = (mRate.StartTime + mRate.TicTocBeatNumber * ExpectedTimeTarget) - TimeMeasured;
        InstTimingErrorMs = InstTimingError * 1000.00;
        if (!mRate.HaveZeroOffset) {
            mRate.HaveZeroOffset = true;
            mRate.ZeroOffsetValue = -InstTimingErrorMs;
        }
        InstTimingErrorMs = InstTimingErrorMs + mRate.ZeroOffsetValue;

        double WrappedRateError = wrapInToRange(InstTimingErrorMs, -ERROR_RATE_Y_SCALE, ERROR_RATE_Y_SCALE);
        // [이상치] 비트 타이밍오차 급변 → RLS(rate 회귀)에서 제외. (스코프 점은 표시 그대로 유지)
        // [이상치] 단기 추세(국소 직선)로 예측 → 잔차로 검출. 곡선 rate 에서도 곡률 오검을 피한다.
        //  (예측은 현재 점을 더하기 '전' 적합선 기준. 이상치는 추세에 안 넣어 추세를 안 오염시킨다.)
        RollingLeastSquares *detr = (TicOrToc == TIC) ? mRate.DetrendTic : mRate.DetrendToc;
        double predicted = 0.0;
        const bool haveResid = detr->Predict(TimeMeasured, predicted);
        const double residual = haveResid ? (InstTimingError - predicted) : 0.0;
        mRate.LastOutlier = kRateOutlierEnabled && haveResid && mRate.Outlier.push(residual);
        const double outMark = mRate.LastOutlier ? WrappedRateError : std::nan("");  // 이상치면 점 표식, 아니면 NaN
        if (!mRate.LastOutlier) detr->AddPoint(TimeMeasured, InstTimingError);        // 정상값만 추세에 반영
        if (TicOrToc == TIC) {
            if (!mRate.LastOutlier) mRate.RlsTicRate->AddPoint(TimeMeasured, InstTimingError);   // [이상치] 이상치는 회귀 제외
            addOrOverwrite(mRate.xTic, mRate.yTic, mRate.yTicOut, TimeMeasured, WrappedRateError, outMark,
                           mRate.MaxTicTocDataPoints, mRate.xTicIndex);
            upd = TicUpdated;
        } else {
            if (!mRate.LastOutlier) mRate.RlsTocRate->AddPoint(TimeMeasured, InstTimingError);
            addOrOverwrite(mRate.xToc, mRate.yToc, mRate.yTocOut, TimeMeasured, WrappedRateError, outMark,
                           mRate.MaxTicTocDataPoints, mRate.xTocIndex);
            upd = TocUpdated;
        }

        if (TicOrToc == TOC) {
            double SlopeTic, RlsTic, SlopeToc, RlsToc;
            if ((mRate.RlsTicRate->GetRate(SlopeTic)) &&
                (mRate.RlsTocRate->GetRate(SlopeToc))) {
                RlsTic = SlopeTic * 86400.00;
                RlsToc = SlopeToc * 86400.00;
                mRate.RlsRate = (RlsTic + RlsToc) / 2.0;
                mRate.RlsRateValid = true;
            } else {
                mRate.RlsRateValid = false;
            }
        }
    }
    return upd;
}

void MeasurementEngine::computeBeatError(double A_EventTime, bool /*haveValidBPH*/, double /*BPH*/)
{
    mBeat.BeatErrorTimes[mBeat.BeatErrorIdx] = A_EventTime;
    mBeat.BeatErrorIdx++;
    if (mBeat.BeatErrorIdx == 3) {
        double t1 = (mBeat.BeatErrorTimes[1] - mBeat.BeatErrorTimes[0]) / (double)mSampleRate;
        double t2 = (mBeat.BeatErrorTimes[2] - mBeat.BeatErrorTimes[1]) / (double)mSampleRate;

        mBeat.BeatErrorMs = qAbs(((t1 - t2) / 2.0) * 1000.0);
        mBeat.LastOutlier = kBeatOutlierEnabled && mBeat.Outlier.push(mBeat.BeatErrorMs);   // [이상치] 검출(현재 비활성)
        if (!mBeat.LastOutlier) mBeat.RollBeatError->Add(mBeat.BeatErrorMs);  // 이상치는 평균에서 제외
        mBeat.BeatErrorTimes[0] = mBeat.BeatErrorTimes[2];
        mBeat.BeatErrorIdx = 1;
    }
}

double MeasurementEngine::amplitude(double LiftAngle, double T1, double BPH)
{
    return LiftAngle / std::sin((2.0 * M_PI * T1) / (7200.0 / BPH));
}

void MeasurementEngine::computeAmplitude(double C_EventTime, bool /*haveValidBPH*/, double BPH)
{
    if ((mAmp.Have_A_Event) & (mRate.BPH_Valid)) {
        double Time, TempAmp;
        int    TicOrToc = ((mRate.TicTocBeatNumber - 1) & 1);

        Time = (C_EventTime - mAmp.Last_A_Event) / (double)mSampleRate;
        TempAmp = amplitude(mLiftAngle, Time, BPH);
        if (TempAmp < 360.00) {
            if (TicOrToc == TIC) {
                mAmp.Amplitude_Tic_Valid = true;
                mAmp.Amplitude_Tic = TempAmp;
            } else { // TOC
                mAmp.Amplitude_Toc = TempAmp;
                if (mAmp.Amplitude_Tic_Valid) {
                    double AverageAmplitudeTicToc = (mAmp.Amplitude_Tic + mAmp.Amplitude_Toc) / 2.0;
                    mAmp.LastOutlier = kAmpOutlierEnabled && mAmp.Outlier.push(AverageAmplitudeTicToc);   // [이상치] 검출(현재 비활성)
                    if (!mAmp.LastOutlier) mAmp.RollAmplitude->Add(AverageAmplitudeTicToc);  // 이상치는 평균에서 제외
                    mAmp.Amplitude_Tic_Valid = false;
                }
            }
        } else if (TicOrToc == TIC) {
            mAmp.Amplitude_Tic_Valid = false;
        }
    }
}

MeasurementEngine::SeriesUpdate
MeasurementEngine::onAEvent(double aEventSample, bool haveValidBph, double bph)
{
    SeriesUpdate upd = computeRateError(aEventSample, haveValidBph, bph);
    computeBeatError(aEventSample, haveValidBph, bph);
    mAmp.Have_A_Event = true;
    mAmp.Last_A_Event = aEventSample;
    return upd;
}

void MeasurementEngine::onCEvent(double cEventSample, bool haveValidBph, double bph)
{
    computeAmplitude(cEventSample, haveValidBph, bph);
}

MeasurementEngine::Results MeasurementEngine::results() const
{
    Results r;
    r.bphValid       = mRate.BPH_Valid;
    r.bph            = mRate.BPH;
    r.rateValid      = mRate.RlsRateValid;
    r.rateSecPerDay  = mRate.RlsRate;
    r.beatErrorValid = mBeat.RollBeatError->CurrentSize() > 0;
    r.beatErrorMs    = r.beatErrorValid ? mBeat.RollBeatError->GetAverage() : 0.0;
    r.amplitudeValid = mAmp.RollAmplitude->CurrentSize() > 0;
    r.amplitudeDeg   = r.amplitudeValid ? mAmp.RollAmplitude->GetAverage() : 0.0;
    r.rateOutlier      = mRate.LastOutlier;     // [이상치] 직전 이벤트 지표별 이상치 여부(평균엔 미반영)
    r.beatErrorOutlier = mBeat.LastOutlier;
    r.amplitudeOutlier = mAmp.LastOutlier;
    return r;
}
