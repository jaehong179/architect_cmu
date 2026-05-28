#include "RollingAverage.h"
#include <deque>

RollingAverage::RollingAverage(size_t size) : maxSize(size), runningSum(0.0)
{

}

double RollingAverage::Add(double val)
{
 if (maxSize == 0) return 0.0;

 runningSum += val;
 window.push_back(val);

 if (window.size() > maxSize)
   {
    runningSum -= window.front();
    window.pop_front();
   }

  return runningSum / window.size();
}

int RollingAverage::CurrentSize(void)
{
   return  window.size();
}

void RollingAverage::Resize(size_t newSize)
{
 maxSize = newSize;
 while (window.size() > maxSize)
   {
    runningSum -= window.front();
    window.pop_front();
   }
}
void RollingAverage::Reset(void)
{
 window.clear();
 runningSum = 0;
}

double RollingAverage::GetAverage(void) const
{
 if (window.empty()) return 0.0;
 return runningSum / window.size();
}
