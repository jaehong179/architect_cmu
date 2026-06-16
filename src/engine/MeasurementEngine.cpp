#include "MeasurementEngine.h"
#include <QtMath>
#include <cmath>

// 측정 상수 (구 MainWindow.cpp 매크로와 동일 값).
static constexpr double ERROR_RATE_Y_SCALE      = 10.0;
static constexpr int    ERROR_RATE_X_DATA_POINTS = 250;
static constexpr int    RLS_WINDOW_INIT          = 100;
static constexpr int    TIC = 0;
static constexpr int    TOC = 1;

MeasurementEngine::MeasurementEngine()
{
    // 구 MainWindow::CreateEvents — 롤링 통계 객체 할당 + 초기 상태.
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
    mRate.RlsRateValid = false;
}

MeasurementEngine::~MeasurementEngine()
{
    delete mAmp.RollAmplitude;
    delete mBeat.RollBeatError;
    delete mRate.RlsTicRate;
    delete mRate.RlsTocRate;
}

void MeasurementEngine::setConfig(int sampleRateHz, int averagingPeriodSec, int liftAngleDeg)
{
    mSampleRate      = sampleRateHz;
    mAveragingPeriod = averagingPeriodSec;
    mLiftAngle       = liftAngleDeg;
}

void MeasurementEngine::reset()
{
    // 구 MainWindow::EventsReset 의 상태 비움 부분(UI/RatePlot 갱신은 호출측 담당).
    mAmp.Have_A_Event = false;
    mAmp.Amplitude_Tic_Valid = false;
    mAmp.RollAmplitude->Reset();

    mBeat.BeatErrorIdx = 0;
    mBeat.RollBeatError->Reset();

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
    mRate.RlsTicRate->Reset();
    mRate.RlsTocRate->Reset();
    mRate.RlsRateValid = false;
}

void MeasurementEngine::addOrOverwrite(QVector<double>& xvec, QVector<double>& yvec,
                                       double value, int maxS, int& index)
{
    if (yvec.size() < maxS) {
        yvec.append(value);   // Growing
        xvec.append(index);   // Never Changes once added
        index = (index + 1) % maxS;
    } else {
        yvec[index] = value;  // Overwriting
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

        mBeat.RollBeatError->Reset();
        mAmp.RollAmplitude->Reset();
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
        if (TicOrToc == TIC) {
            mRate.RlsTicRate->AddPoint(TimeMeasured, InstTimingError);
            addOrOverwrite(mRate.xTic, mRate.yTic, WrappedRateError, mRate.MaxTicTocDataPoints, mRate.xTicIndex);
            upd = TicUpdated;
        } else {
            mRate.RlsTocRate->AddPoint(TimeMeasured, InstTimingError);
            addOrOverwrite(mRate.xToc, mRate.yToc, WrappedRateError, mRate.MaxTicTocDataPoints, mRate.xTocIndex);
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
        mBeat.RollBeatError->Add(mBeat.BeatErrorMs);
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
                    mAmp.RollAmplitude->Add(AverageAmplitudeTicToc);
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
    return r;
}
