#ifndef ROLLINGOUTLIER_H
#define ROLLINGOUTLIER_H
// RollingOutlier — 로버스트 롤링 이상치 검출 (고정 임계 없음, 데이터 적응형).
//  최근 N개 값의 중앙값(median)과 MAD(median absolute deviation)로 modified z-score 를 계산,
//  |0.6745·(v−median)/MAD| > k 이면 이상치로 본다. 평균±σ 와 달리 median/MAD 는 이상치 자체에
//  휘둘리지 않아(robust) "그동안의 정상 패턴에서 갑자기 벗어난 값"을 안정적으로 잡는다.
//  · 롤링 윈도우라 baseline 이 서서히 변해도(드리프트) 따라가고, 급변만 튀어나온다.
//  · Qt 비의존(순수 C++) — 어느 모듈에서나 사용 가능.
#include <deque>
#include <vector>
#include <algorithm>
#include <cmath>

class RollingOutlier {
public:
    // madFloor: MAD 하한(같은 단위). baseline 이 비정상적으로 조용해(MAD≈0) 미세 잔차로 z 가
    //  폭주하는 가짜 양성을 막는다. 0 이면 비활성. 평소 MAD 보다 충분히 작게 두면 collapse 때만 작동.
    explicit RollingOutlier(int window = 120, double k = 3.5, int warmup = 30, double madFloor = 0.0)
        : mWin(window), mK(k), mWarmup(warmup), mMadFloor(madFloor) {}

    // 새 값 push. 직전까지 쌓인 baseline 기준 이상치면 true 반환.
    //  (판정은 v 를 넣기 '전' 분포로 하고, 그 뒤 v 를 baseline 에 추가 → 자기 자신에 안 휘둘림)
    bool push(double v)
    {
        const double z = (static_cast<int>(mBuf.size()) >= mWarmup) ? scoreOf(v) : 0.0;
        const bool out = (z > mK);
        mBuf.push_back(v);
        if (static_cast<int>(mBuf.size()) > mWin) mBuf.pop_front();
        return out;
    }

    void reset() { mBuf.clear(); }

private:
    double scoreOf(double v) const
    {
        std::vector<double> s(mBuf.begin(), mBuf.end());
        std::sort(s.begin(), s.end());
        const double med = s[s.size() / 2];

        std::vector<double> dev;
        dev.reserve(s.size());
        for (double x : s) dev.push_back(std::fabs(x - med));
        std::sort(dev.begin(), dev.end());
        const double mad = std::max(dev[dev.size() / 2], mMadFloor);  // [MAD floor] collapse 시 z 폭주 차단

        if (mad < 1e-12) return 0.0;       // 변동이 거의 0 → 판정 보류(노이즈 0 데이터 false-storm 방지)
        return 0.6745 * std::fabs(v - med) / mad;
    }

    std::deque<double> mBuf;
    int    mWin;
    double mK;
    int    mWarmup;
    double mMadFloor;
};

#endif // ROLLINGOUTLIER_H
