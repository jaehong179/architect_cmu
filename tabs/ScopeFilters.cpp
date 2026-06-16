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

// F2: 상승 시 신호를 따라가고(피크홀드), 하강 시 직전 피크에서 지수 감쇠. A = 감쇠 시정수(샘플).
static void filterRiseExpDecay(const QVector<double> &f1, QVector<double> &out, double A)
{
    const int n = f1.size();
    out.resize(n);
    if (A < 1.0) A = 1.0;
    double peak = 0, prev = 0; int T = 0;
    for (int i = 0; i < n; ++i) {
        const double u = f1[i] > 0 ? f1[i] : 0.0;
        if (i == 0) { out[i] = u; peak = u; T = 0; prev = u; continue; }
        if (u >= prev) { out[i] = u; peak = u; T = i; }              // 상승: 따라감
        else           { out[i] = peak * std::exp(-((double)(i - T)) / A); }  // 하강: 지수 감쇠
        prev = u;
    }
}

// F3: 정류 후 상승 에지 강조 + 하강 포물선 감쇠. B = 포물선 폭(샘플).
static void filterRiseParabolicDecay(const QVector<double> &f1, QVector<double> &out, double B)
{
    const int n = f1.size();
    out.resize(n);
    if (B < 1.0) B = 1.0;
    double peak = 0; int T = 0;
    for (int i = 0; i < n; ++i) {
        const double g = std::fabs(f1[i]);                          // 정류(아래쪽을 위로)
        if (i == 0) { out[i] = g; peak = g; T = 0; continue; }
        const double gp = std::fabs(f1[i - 1]);
        if (g >= gp) { out[i] = g; peak = g; T = i; }                // 상승 에지: 강조(따라감)
        else {
            const double dt = (double)(i - T) / B;                  // 하강: 포물선 감쇠
            double w = 1.0 - dt * dt; if (w < 0) w = 0;
            out[i] = peak * w;
        }
    }
}

// T1/T2/T3 검출(엔벨로프 기준): T3=전역 최대, T1=0.12·peak 첫 상향교차, T2=둘 사이 국소 최대.
static void detectPulses(const QVector<double> &env, int &t1, int &t2, int &t3)
{
    const int n = env.size();
    double peak = 0; int idx = 0;
    for (int i = 0; i < n; ++i) if (env[i] > peak) { peak = env[i]; idx = i; }
    t3 = (n > 0) ? idx : -1;
    const double thr = 0.12 * peak; t1 = -1;
    for (int i = 1; i < n; ++i) if (env[i - 1] < thr && env[i] >= thr) { t1 = i; break; }
    t2 = -1; double m = 0;
    const int a = (t1 < 0 ? 0 : t1) + 5, b = idx - 5;
    for (int i = a; i < b; ++i)
        if (i > 0 && i + 1 < n && env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > m) { m = env[i]; t2 = i; }
}

void computeScopeFilters(const QVector<double> &rawFull, int sr, QVector<double> out[4], int pulses[3])
{
    const int n = rawFull.size();
    if (sr <= 0) sr = 48000;
    const int    K1 = std::max(1, (int)(0.0010 * sr));   // F1 이동평균 ≈ 1.0 ms
    const double A  = std::max(1.0, 0.0010 * sr);        // F2 지수감쇠 ≈ 1.0 ms
    const double B  = std::max(1.0, 0.0020 * sr);        // F3 포물선폭 ≈ 2.0 ms

    // F0 = |raw - avg| (평균 기준 미러용 엔벨로프 — 평활 없음).
    const double avg = meanOf(rawFull);
    out[0].resize(n);
    for (int i = 0; i < n; ++i) out[0][i] = std::fabs(rawFull[i] - avg);

    movingAverage(out[0], K1, out[1]);                   // F1 = MA(F0)
    filterRiseExpDecay(out[1], out[2], A);               // F2 = F1 상승강조+지수감쇠
    filterRiseParabolicDecay(out[1], out[3], B);         // F3 = F1 정류+상승에지+포물선감쇠

    detectPulses(out[2], pulses[0], pulses[1], pulses[2]);   // T1/T2/T3 (F2 기준)
}

const bool  kScopeMirror[4] = { true, true, true, false };   // F0·F1·F2 미러, F3만 upper(정류)
const char *kScopeFilterShort[4] = { "F0 Raw(미러)", "F1 이동평균", "F2 상승강조", "F3 정류·에지" };
