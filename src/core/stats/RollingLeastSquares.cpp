#include "RollingLeastSquares.h"
#include <cmath>


void RollingLeastSquares::Deallocate()
{
    delete[] x_buf;
    delete[] y_buf;
}

void RollingLeastSquares::Allocate(int new_capacity)
{
    capacity = new_capacity;
    x_buf = new double[capacity];
    y_buf = new double[capacity];
    Reset();
}

RollingLeastSquares::RollingLeastSquares(int window_size) : size(0), head(0)
  {
   Allocate(window_size);
  }

RollingLeastSquares::~RollingLeastSquares()
{
 Deallocate();
}

void RollingLeastSquares::Reset()
{
 size = 0;
 head = 0;
 sumX = sumY = sumXY = sumX2 = 0.0;
}

void RollingLeastSquares::Resize(int new_size)
{
 Deallocate();
 Allocate(new_size);
}

void RollingLeastSquares::AddPoint(double x, double y)
{
 if (capacity == 0) return;

 // If buffer full, remove oldest data from sums
 if (size == capacity)
  {
   double oldX = x_buf[head];
   double oldY = y_buf[head];
   sumX -= oldX;
   sumY -= oldY;
   sumXY -= oldX * oldY;
   sumX2 -= oldX * oldX;
  }
 else
  {
   size++;
  }

  // Add new data
  x_buf[head] = x;
  y_buf[head] = y;
  sumX += x;
  sumY += y;
  sumXY += x * y;
  sumX2 += x * x;

  head = (head + 1) % capacity; // Circular buffer
 }

 bool RollingLeastSquares::GetRate(double &slope)
 {
  if (size < 2) return false;

  double n = static_cast<double>(size);
  double denominator = (n * sumX2 - sumX * sumX);

  if (std::abs(denominator) < 1e-10) return false; // Singular matrix

  slope = (n * sumXY - sumX * sumY) / denominator;
  return true;
}

