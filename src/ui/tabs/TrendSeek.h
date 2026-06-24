#ifndef TRENDSEEK_H
#define TRENDSEEK_H
// =============================================================================
//  TrendSeek — "정지 중 트렌드 그래프 클릭 → 그 시각(절대 샘플)으로 seek" 재사용 헬퍼
// -----------------------------------------------------------------------------
//  [목적]  지속적으로 데이터를 누적해 보여주는 모든 그래프(Trace·Long-Term·Beat Error·
//          Vario·… )에서 동일하게 "클릭 지점 → 절대 샘플 방출 + 자홍 커서선" 을 제공.
//          탭이 멤버로 소유하고:
//            - 생성 시 attach(plot, onSeek) 로 한/여러 플롯에 클릭+커서 부착(매핑 공유)
//            - onMeasurement 에서 addPoint(x, snap.totalSamples) 로 x→절대샘플 매핑 누적
//            - onResetSession 에서 clear()
//          클릭 시 onSeek(absSample) 콜백(보통 emit seekRequested) 이 불린다.
//
//  ※ Q_OBJECT 없이(헤더 온리) std::function 콜백 사용 — MOC 불필요. mousePress 연결의
//    컨텍스트는 plot(QObject) 라 plot 수명과 함께 정리된다.
// =============================================================================

#include "qcustomplot.h"
#include <QVector>
#include <QPair>
#include <functional>

class TrendSeek
{
public:
    // x 보관 폭(가로 단위). 0 = 무제한. (예: 시간축 초 단위면 480 = 최근 8분만)
    void setPurgeWindow(double w) { mPurge = w; }

    // onMeasurement 에서: 플롯 x좌표 ↔ 그 시점의 절대 샘플(totalSamples) 매핑 누적.
    void addPoint(double x, double absSample)
    {
        mMap.push_back({ x, absSample });
        if (mPurge > 0.0)
            while (!mMap.isEmpty() && mMap.first().first < x - mPurge) mMap.removeFirst();
    }

    void clear()
    {
        mMap.clear();
        for (PlotCur &pc : mPlots) if (pc.cur) pc.cur->setVisible(false);
    }

    // [③] 다른 탭에서 온 seek(절대 샘플) → 가장 가까운 보관 지점의 x 로 커서를 옮긴다.
    //  → 모든 트렌드 탭의 커서가 같은 시점을 가리키도록 동기화. 매핑이 없으면 무시.
    void showCursorAtSample(double absSample)
    {
        if (mMap.isEmpty()) return;
        double bestX = mMap.first().first, bestErr = qAbs(mMap.first().second - absSample);
        for (const auto &p : mMap) {
            const double err = qAbs(p.second - absSample);
            if (err < bestErr) { bestErr = err; bestX = p.first; }
        }
        showCursor(bestX);
    }

    // 플롯에 클릭 핸들러 + 세로 커서선 부착. 클릭 시 onSeek(absSample) 호출.
    //  여러 번 호출해 같은 매핑을 여러 플롯에 공유 가능(각 플롯에 커서 1개).
    void attach(QCustomPlot *plot, std::function<void(double)> onSeek)
    {
        if (!plot) return;
        auto *cur = new QCPItemStraightLine(plot);
        cur->setPen(QPen(QColor(200, 0, 200), 1, Qt::DashLine));
        cur->setVisible(false);
        mPlots.push_back({ plot, cur });
        QObject::connect(plot, &QCustomPlot::mousePress, plot,
            [this, plot, onSeek](QMouseEvent *e) {
                if (mMap.isEmpty()) return;
                const double x = plot->xAxis->pixelToCoord(e->position().x());
                // 커서는 onSeek 왕복(broadcastSeek 가 pause 게이트)으로만 표시 → 선택은 정지 중에만.
                onSeek(sampleAtX(x));
            });
    }

    // [③] 선택 해제 — 커서만 숨긴다(매핑 데이터는 유지, clear() 와 다름).
    void hideCursor()
    {
        for (PlotCur &pc : mPlots) if (pc.cur) {
            pc.cur->setVisible(false);
            if (pc.plot) pc.plot->replot(QCustomPlot::rpQueuedReplot);
        }
    }

private:
    double sampleAtX(double x) const
    {
        if (mMap.isEmpty()) return 0.0;
        double best = mMap.first().second, bestDx = qAbs(mMap.first().first - x);
        for (const auto &p : mMap) {
            const double dx = qAbs(p.first - x);
            if (dx < bestDx) { bestDx = dx; best = p.second; }
        }
        return best;
    }
    void showCursor(double x)
    {
        for (PlotCur &pc : mPlots) {
            if (!pc.cur) continue;
            pc.cur->point1->setCoords(x, 0); pc.cur->point2->setCoords(x, 1);
            pc.cur->setVisible(true);
            if (pc.plot) pc.plot->replot(QCustomPlot::rpQueuedReplot);
        }
    }

    struct PlotCur { QCustomPlot *plot = nullptr; QCPItemStraightLine *cur = nullptr; };
    QVector<QPair<double, double>> mMap;   // (x, 절대샘플)
    QVector<PlotCur>               mPlots;
    double                         mPurge = 0.0;
};

#endif // TRENDSEEK_H
