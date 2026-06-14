#ifndef SCOPERENDER_H
#define SCOPERENDER_H
// =============================================================================
//  ScopeRender.h — 스코프 계열 탭 공용 렌더 헬퍼 (인프라)
// -----------------------------------------------------------------------------
//  WaveBuffer 의 절대 인덱스 구간 [absFrom, absFrom+count) 엔벨로프를 ms 시간축으로
//  graph(0) 에 그리고, 구간 내 A(녹색)/C(빨강) 이벤트를 수직 마커로 표시한다.
//  연속된 A→C 사이에는 ms 라벨을 단다.  (Beat Scope·Escapement 등 공용)
// =============================================================================

#include "qcustomplot.h"
#include "WaveBuffer.h"

static inline void scopePlotWindow(QCustomPlot *p, const WaveBuffer &buf,
                                   uint64_t absFrom, int count, bool drawMs = true)
{
    if (count <= 0) return;
    const int sr = buf.sampleRate() > 0 ? buf.sampleRate() : 48000;
    QVector<double> y; buf.copyRange(absFrom, count, y);
    QVector<double> x(count);
    for (int i = 0; i < count; ++i) x[i] = 1000.0 * i / sr;     // ms (구간 시작 기준)
    p->graph(0)->setData(x, y, true);

    double ymax = 0.0; for (double v : y) if (v > ymax) ymax = v;
    if (ymax <= 0.0) ymax = 1.0;

    p->clearItems();
    const QVector<WaveEvent> evs = buf.eventsInRange(absFrom, absFrom + (uint64_t)count);
    uint64_t lastA = 0; bool haveA = false;
    for (const WaveEvent &e : evs) {
        const double xm = 1000.0 * (double)(e.sample - absFrom) / sr;
        const QColor c = (e.type == 1) ? QColor(0, 170, 0) : QColor(220, 0, 0);
        auto *ln = new QCPItemLine(p);
        ln->start->setCoords(xm, 0); ln->end->setCoords(xm, ymax);
        ln->setPen(QPen(c, 1, Qt::DashLine));
        if (e.type == 1) { lastA = e.sample; haveA = true; }
        else if (haveA && drawMs) {
            const double ms = 1000.0 * (double)(e.sample - lastA) / sr;
            auto *t = new QCPItemText(p);
            t->position->setCoords(xm, ymax * 0.92);
            t->setText(QString(" %1 ms").arg(ms, 0, 'f', 2));
            t->setColor(Qt::black);
            t->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    p->xAxis->setRange(0, 1000.0 * count / sr);
    p->yAxis->setRange(0, ymax * 1.12);
}

#endif // SCOPERENDER_H
