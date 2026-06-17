#include "ScopeFilters.h"
#include <algorithm>
#include <cmath>

// Project Plan §Scope Function with Multiple Filter Views (FR-SFM) 사양 그대로:
//  F0 = |raw-avg|            평균 기준 미러(±대칭). 가장 실제에 가까운 raw 표현.
//  F1 = MA(F0)               이동평균 → 포락선 평활·배경잡음↓·읽기 쉬움.
//                             (주의: 저진폭 신호 성분은 이 모드에서 덜 보일 수 있음.)
//  F2 = F1 상승강조+지수감쇠  국소 상승 후 감쇠 → 날카로운 상승(T3·T2) 부각, 하강 억제.
//  F3 = F1 정류+상승에지+포물선감쇠  상승 에지 강조, 하강 포물선 감쇠 → T1·특히 T3 식별.

static double meanOf(const QVector<double> &x)
{
    double s = 0; for (double v : x) s += v; return x.isEmpty() ? 0.0 : s / x.size();
}

// 이동평균(박스카, K 샘플) — 엔벨로프 평활.
static void movingAverage(const QVector<double> &in, int K, QVector<double> &out)
{
    const int n = in.size();
    out.resize(n);
    if (K < 1) K = 1;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += in[i];
        if (i >= K) acc -= in[i - K];
        out[i] = acc / qMin(i + 1, K);
    }
}

// F2: 상승 시 신호를 따라가고(피크홀드), 하강 시 직전 피크에서 지수 감쇠. decayTauSamp = 감쇠 시정수(샘플).
static void filterRiseExpDecay(const QVector<double> &input, QVector<double> &out, double decayTauSamp)
{
    const int n = input.size();
    out.resize(n);
    if (decayTauSamp < 1.0) decayTauSamp = 1.0;
    double peakVal = 0, prevVal = 0; int peakIdx = 0;
    for (int i = 0; i < n; ++i) {
        const double upper = input[i] > 0 ? input[i] : 0.0;
        if (i == 0) { out[i] = upper; peakVal = upper; peakIdx = 0; prevVal = upper; continue; }
        if (upper >= prevVal) { out[i] = upper; peakVal = upper; peakIdx = i; }   // 상승: 따라감
        else                  { out[i] = peakVal * std::exp(-((double)(i - peakIdx)) / decayTauSamp); }  // 하강: 지수 감쇠
        prevVal = upper;
    }
}

// F3: 정류 후 상승 에지 강조 + 하강 포물선 감쇠. parabolaWidthSamp = 포물선 폭(샘플).
static void filterRiseParabolicDecay(const QVector<double> &input, QVector<double> &out, double parabolaWidthSamp)
{
    const int n = input.size();
    out.resize(n);
    if (parabolaWidthSamp < 1.0) parabolaWidthSamp = 1.0;
    double peakVal = 0; int peakIdx = 0;
    for (int i = 0; i < n; ++i) {
        const double mag = std::fabs(input[i]);                     // 정류(아래쪽을 위로)
        if (i == 0) { out[i] = mag; peakVal = mag; peakIdx = 0; continue; }
        const double prevMag = std::fabs(input[i - 1]);
        if (mag >= prevMag) { out[i] = mag; peakVal = mag; peakIdx = i; }   // 상승 에지: 강조(따라감)
        else {
            const double dt = (double)(i - peakIdx) / parabolaWidthSamp;    // 하강: 포물선 감쇠
            double weight = 1.0 - dt * dt; if (weight < 0) weight = 0;
            out[i] = peakVal * weight;
        }
    }
}

// T1/T2/T3 검출(엔벨로프 기준): T3=전역 최대, T1=0.12·peak 첫 상향교차, T2=둘 사이 국소 최대.
static void detectPulses(const QVector<double> &env, int &t1, int &t2, int &t3)
{
    const int n = env.size();
    double peakVal = 0; int peakIdx = 0;
    for (int i = 0; i < n; ++i) if (env[i] > peakVal) { peakVal = env[i]; peakIdx = i; }
    t3 = (n > 0) ? peakIdx : -1;
    const double crossThresh = 0.12 * peakVal; t1 = -1;
    for (int i = 1; i < n; ++i) if (env[i - 1] < crossThresh && env[i] >= crossThresh) { t1 = i; break; }
    t2 = -1; double bestVal = 0;
    const int searchLo = (t1 < 0 ? 0 : t1) + 5, searchHi = peakIdx - 5;
    for (int i = searchLo; i < searchHi; ++i)
        if (i > 0 && i + 1 < n && env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > bestVal) { bestVal = env[i]; t2 = i; }
}

void computeScopeFilters(const QVector<double> &rawFull, int sr, QVector<double> out[4], int pulses[3])
{
    const int n = rawFull.size();
    if (sr <= 0) sr = 48000;
    const int    maWindowSamp     = std::max(1,   (int)(0.0010 * sr));   // F1 이동평균 ≈ 1.0 ms
    const double expDecayTauSamp  = std::max(1.0,       0.0010 * sr);    // F2 지수감쇠 ≈ 1.0 ms
    const double parabolaWidthSamp = std::max(1.0,      0.0020 * sr);    // F3 포물선폭 ≈ 2.0 ms

    // F0 = |raw - avg| (평균 기준 미러용 엔벨로프 — 평활 없음).
    const double avg = meanOf(rawFull);
    out[0].resize(n);
    for (int i = 0; i < n; ++i) out[0][i] = std::fabs(rawFull[i] - avg);

    movingAverage(out[0], maWindowSamp, out[1]);                  // F1 = MA(F0)
    filterRiseExpDecay(out[1], out[2], expDecayTauSamp);         // F2 = F1 상승강조+지수감쇠
    filterRiseParabolicDecay(out[1], out[3], parabolaWidthSamp); // F3 = F1 정류+상승에지+포물선감쇠

    detectPulses(out[2], pulses[0], pulses[1], pulses[2]);   // T1/T2/T3 (F2 기준)
}

const bool  kScopeMirror[4] = { true, true, true, false };   // F0·F1·F2 미러, F3만 upper(정류)
const char *kScopeFilterShort[4] = { "F0 Raw (mirror)", "F1 Moving avg", "F2 Rising-emph", "F3 Rectify·edge" };
