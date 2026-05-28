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
};

#endif // ROLLINGLEASTSQUARES_H
