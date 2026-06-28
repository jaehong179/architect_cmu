// DiagPreprocess.cpp — inference_diag.py 전처리(make_signal/compute_features/표준화) C++ 포팅.
//   numpy 동작을 최대한 그대로 재현한다(median/percentile 보간, population std, 부호 0 처리,
//   Hilbert 포락선·rfft 스펙트럼, robust z(MAD) 기반 knock 통계, 정규화 교차상관 최대값).
#include "DiagPreprocess.h"
#include "DiagConfig.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace diag {
namespace {

constexpr double kEps = 1e-8;

double mean(const std::vector<double> &x)
{
    if (x.empty()) return 0.0;
    double s = 0.0;
    for (double v : x) s += v;
    return s / static_cast<double>(x.size());
}

// numpy.median — 정렬 후 중앙(짝수면 두 중앙값 평균).
double median(std::vector<double> x)
{
    if (x.empty()) return 0.0;
    std::sort(x.begin(), x.end());
    const std::size_t n = x.size();
    if (n % 2 == 1) return x[n / 2];
    return 0.5 * (x[n / 2 - 1] + x[n / 2]);
}

// numpy.percentile(., p, interpolation='linear').
double percentile(std::vector<double> x, double p)
{
    if (x.empty()) return 0.0;
    std::sort(x.begin(), x.end());
    const std::size_t n = x.size();
    if (n == 1) return x[0];
    const double rank = (p / 100.0) * static_cast<double>(n - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(rank));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(rank));
    const double frac = rank - static_cast<double>(lo);
    return x[lo] + (x[hi] - x[lo]) * frac;
}

// population 표준편차(numpy 기본 ddof=0).
double stddevPop(const std::vector<double> &x)
{
    if (x.empty()) return 0.0;
    const double m = mean(x);
    double s = 0.0;
    for (double v : x) s += (v - m) * (v - m);
    return std::sqrt(s / static_cast<double>(x.size()));
}

int signOf(double v) { return (v > 0.0) ? 1 : (v < 0.0 ? -1 : 0); }

double maxAbs(const double *x, int n)
{
    double m = 0.0;
    for (int i = 0; i < n; ++i) m = std::max(m, std::abs(x[i]));
    return m;
}

// 직접 DFT (길이 64 고정 → 충분히 빠르고 power-of-2 제약 없음).
void dft(const std::vector<std::complex<double>> &in,
         std::vector<std::complex<double>> &out, bool inverse)
{
    const int n = static_cast<int>(in.size());
    out.assign(n, std::complex<double>(0.0, 0.0));
    const double sgn = inverse ? 1.0 : -1.0;
    for (int k = 0; k < n; ++k) {
        std::complex<double> acc(0.0, 0.0);
        for (int j = 0; j < n; ++j) {
            const double ang = sgn * 2.0 * M_PI * static_cast<double>(k) *
                               static_cast<double>(j) / static_cast<double>(n);
            acc += in[j] * std::complex<double>(std::cos(ang), std::sin(ang));
        }
        out[k] = inverse ? acc / static_cast<double>(n) : acc;
    }
}

// numpy: np.abs(hilbert analytic) 포락선. Xf=fft(x); h-멀티플라이; abs(ifft).
std::vector<double> hilbertEnvelope(const double *x, int n)
{
    std::vector<std::complex<double>> xc(n);
    for (int i = 0; i < n; ++i) xc[i] = std::complex<double>(x[i], 0.0);
    std::vector<std::complex<double>> Xf;
    dft(xc, Xf, /*inverse=*/false);

    std::vector<double> h(n, 0.0);
    if (n % 2 == 0) {
        h[0] = 1.0;
        h[n / 2] = 1.0;
        for (int i = 1; i < n / 2; ++i) h[i] = 2.0;
    } else {
        h[0] = 1.0;
        for (int i = 1; i < (n + 1) / 2; ++i) h[i] = 2.0;
    }
    for (int i = 0; i < n; ++i) Xf[i] *= h[i];

    std::vector<std::complex<double>> analytic;
    dft(Xf, analytic, /*inverse=*/true);
    std::vector<double> env(n);
    for (int i = 0; i < n; ++i) env[i] = std::abs(analytic[i]);
    return env;
}

// np.abs(np.fft.rfft(x)) — 첫 n/2+1 빈 크기.
std::vector<double> rfftMag(const double *x, int n)
{
    std::vector<std::complex<double>> xc(n);
    for (int i = 0; i < n; ++i) xc[i] = std::complex<double>(x[i], 0.0);
    std::vector<std::complex<double>> Xf;
    dft(xc, Xf, /*inverse=*/false);
    const int half = n / 2 + 1;
    std::vector<double> mag(half);
    for (int i = 0; i < half; ++i) mag[i] = std::abs(Xf[i]);
    return mag;
}

// 정규화 교차상관의 최대값(inference_diag.py: max(correlate(a,b,'full'))/denom).
double crossCorrMax(const double *t1, const double *t3, int n)
{
    std::vector<double> a(n), b(n);
    double ma = 0.0, mb = 0.0;
    for (int i = 0; i < n; ++i) { ma += t1[i]; mb += t3[i]; }
    ma /= n; mb /= n;
    double sa = 0.0, sb = 0.0;
    for (int i = 0; i < n; ++i) {
        a[i] = t1[i] - ma; b[i] = t3[i] - mb;
        sa += a[i] * a[i]; sb += b[i] * b[i];
    }
    const double denom = std::sqrt(sa * sb) + kEps;

    double best = -std::numeric_limits<double>::infinity();
    for (int shift = -(n - 1); shift <= (n - 1); ++shift) {
        double s = 0.0;
        for (int l = 0; l < n; ++l) {
            const int j = l - shift;
            if (j >= 0 && j < n) s += a[l] * b[j];
        }
        best = std::max(best, s);
    }
    return best / denom;
}

// knock 통계(spike_ratio, max_jump, excess_kurtosis, outlier_run) — build_dataset.py 동일.
void knockStats(const double *t1, const double *t3, int n,
                double &spikeRatio, double &maxJump, double &kurtosis, double &outlierRun)
{
    auto robustZ = [n](const double *x, std::vector<double> &z, double &mad) {
        std::vector<double> v(x, x + n);
        const double med = median(v);
        std::vector<double> dev(n);
        for (int i = 0; i < n; ++i) dev[i] = std::abs(x[i] - med);
        mad = median(dev) * 1.4826 + kEps;
        z.resize(n);
        for (int i = 0; i < n; ++i) z[i] = (x[i] - med) / mad;
    };

    std::vector<double> z1, z3;
    double mad1 = 0.0, mad3 = 0.0;
    robustZ(t1, z1, mad1);
    robustZ(t3, z3, mad3);

    std::vector<char> out(n, 0);
    int outCount = 0;
    for (int i = 0; i < n; ++i) {
        out[i] = (std::abs(z1[i]) > 3.0 || std::abs(z3[i]) > 3.0) ? 1 : 0;
        outCount += out[i];
    }
    spikeRatio = static_cast<double>(outCount) / static_cast<double>(n);

    auto maxAbsDiff = [n](const double *x) {
        double m = 0.0;
        for (int i = 1; i < n; ++i) m = std::max(m, std::abs(x[i] - x[i - 1]));
        return m;
    };
    const double jump1 = (n > 1) ? maxAbsDiff(t1) / mad1 : 0.0;
    const double jump3 = (n > 1) ? maxAbsDiff(t3) / mad3 : 0.0;
    maxJump = std::max(jump1, jump3);

    auto excessKurtosis = [n](const double *x) {
        std::vector<double> v(x, x + n);
        const double m = mean(v);
        const double s = stddevPop(v) + kEps;
        double acc = 0.0;
        for (int i = 0; i < n; ++i) {
            const double z = (x[i] - m) / s;
            acc += z * z * z * z;
        }
        return acc / static_cast<double>(n) - 3.0;
    };
    kurtosis = std::max(excessKurtosis(t1), excessKurtosis(t3));

    int longest = 0, run = 0;
    for (int i = 0; i < n; ++i) {
        run = out[i] ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    outlierRun = static_cast<double>(longest) / static_cast<double>(std::max(1, n));
}

// norm_unit(x) = x / (max|x| + 1e-8). (윈도우 길이 == kSequenceLength 이므로 resample 은 항등.)
void normUnitInto(const double *x, int n, std::vector<float> &dst, int stride, int offset)
{
    const double scale = maxAbs(x, n) + kEps;
    for (int i = 0; i < n; ++i)
        dst[i * stride + offset] = static_cast<float>(x[i] / scale);
}

} // namespace

void makeSignal(const double *t1, const double *t3, int n, std::vector<float> &signalOut)
{
    signalOut.assign(static_cast<std::size_t>(n) * 3, 0.0f);
    std::vector<double> diff(n);
    for (int i = 0; i < n; ++i) diff[i] = t1[i] - t3[i];
    normUnitInto(t1, n, signalOut, /*stride=*/3, /*offset=*/0);
    normUnitInto(t3, n, signalOut, /*stride=*/3, /*offset=*/1);
    normUnitInto(diff.data(), n, signalOut, /*stride=*/3, /*offset=*/2);
}

void computeFeaturesStandardized(const double *t1, const double *t3, int n,
                                 std::vector<float> &featOut)
{
    const std::vector<double> env = hilbertEnvelope(t1, n);

    std::vector<double> t1v(t1, t1 + n), t3v(t3, t3 + n);
    double sq1 = 0.0, sq3 = 0.0;
    for (int i = 0; i < n; ++i) { sq1 += t1[i] * t1[i]; sq3 += t3[i] * t3[i]; }
    const double rms_t1 = std::sqrt(sq1 / n) + kEps;
    const double rms_t3 = std::sqrt(sq3 / n) + kEps;
    const double peak_t1 = maxAbs(t1, n) + kEps;
    const double crest_t1 = peak_t1 / rms_t1;

    int signChanges = 0;
    for (int i = 1; i < n; ++i)
        if (signOf(t1[i]) - signOf(t1[i - 1]) != 0) ++signChanges;
    const double zcr_t1 = (n > 1)
        ? static_cast<double>(signChanges) / static_cast<double>(n - 1) : 0.0;

    const double noise_floor = percentile(env, 10.0);

    const std::vector<double> spec = rfftMag(t1, n);  // 길이 n/2+1, 이미 +1e-8 미포함
    const int half = static_cast<int>(spec.size());
    double specSum = 0.0;
    for (int i = 0; i < half; ++i) specSum += (spec[i] + kEps);
    auto freqOf = [n](int k) { return static_cast<double>(k) * kSampleRate / static_cast<double>(n); };
    double centroid = 0.0, domFreq = 0.0, domMag = -1.0;
    for (int i = 0; i < half; ++i) {
        const double s = spec[i] + kEps;
        const double f = freqOf(i);
        centroid += f * s;
        if (s > domMag) { domMag = s; domFreq = f; }
    }
    centroid /= specSum;
    double bandwidth = 0.0;
    for (int i = 0; i < half; ++i) {
        const double s = spec[i] + kEps;
        const double f = freqOf(i);
        bandwidth += (f - centroid) * (f - centroid) * s;
    }
    bandwidth = std::sqrt(bandwidth / specSum);

    double spikeRatio = 0.0, maxJump = 0.0, kurtosis = 0.0, outlierRun = 0.0;
    knockStats(t1, t3, n, spikeRatio, maxJump, kurtosis, outlierRun);

    const double xcorr = crossCorrMax(t1, t3, n);

    const double feats[14] = {
        rms_t1, rms_t3, peak_t1, crest_t1, zcr_t1, noise_floor,
        centroid, bandwidth, domFreq,
        spikeRatio, maxJump, kurtosis, outlierRun, xcorr,
    };

    featOut.resize(kNumFeatures);
    for (int i = 0; i < kNumFeatures; ++i)
        featOut[i] = static_cast<float>((feats[i] - kFeatureMean[i]) / kFeatureStd[i]);
}

} // namespace diag
