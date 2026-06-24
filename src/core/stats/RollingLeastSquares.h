#ifndef ROLLINGLEASTSQUARES_H
#define ROLLINGLEASTSQUARES_H

class RollingLeastSquares {
private:
    double *x_buf, *y_buf;
    int capacity;
    int size;
    int head;
    double sumX, sumY, sumXY, sumX2;

    void Deallocate();
    void Allocate(int new_capacity);

public:
    RollingLeastSquares(int window_size);
    ~RollingLeastSquares();
    void Reset();
    void Resize(int new_size);
    void AddPoint(double x, double y);
    bool GetRate(double &slope);
    bool Predict(double x, double &y);   // 현재 적합선의 예측값(추세) — 잔차(이상치) 계산용
};

#endif // ROLLINGLEASTSQUARES_H
