#ifndef ROLLINGAVERAGE_H
#define ROLLINGAVERAGE_H
#include <deque>
class RollingAverage
{

private:
    std::deque<double> window;
    size_t maxSize;
    double runningSum;

public:
    explicit RollingAverage(size_t size);
    double Add(double val);
    void Resize(size_t newSize);
    void Reset(void);
    int CurrentSize(void);
    double GetAverage(void) const;
};

#endif // ROLLINGAVERAGE_H
