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
        for (PlotCur &pc : mPlots) setCursorVisible(pc, false);
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

    // 플롯에 클릭 핸들러 + 롤리팝 커서(줄기+머리+툴팁) 부착. 클릭 시 onSeek(absSample) 호출.
    //  여러 번 호출해 같은 매핑을 여러 플롯에 공유 가능(각 플롯에 커서 1개).
    void attach(QCustomPlot *plot, std::function<void(double)> onSeek)
    {
        if (!plot) return;
        PlotCur pc; pc.plot = plot;
        makeLollipop(plot, pc.stem, pc.head, pc.tip);
        mPlots.push_back(pc);
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
        for (PlotCur &pc : mPlots) {
            setCursorVisible(pc, false);
            if (pc.plot) pc.plot->replot(QCustomPlot::rpQueuedReplot);
        }
    }

    // 밝은 청록 롤리팝(줄기 + 원형 머리 + 상단 툴팁) 생성 — 다른 탭에서도 동일 스타일 재사용 가능.
    //  줄기는 유한 선(바닥→머리)이라 머리 위로는 뻗지 않는다(머리 = 윗 끝).
    static void makeLollipop(QCustomPlot *plot, QCPItemLine *&stem,
                             QCPItemTracer *&head, QCPItemText *&tip)
    {
        const QColor cyan(0, 230, 240);
        stem = new QCPItemLine(plot);
        stem->setPen(QPen(cyan, 1.6));
        stem->start->setTypeX(QCPItemPosition::ptPlotCoords);
        stem->start->setTypeY(QCPItemPosition::ptAxisRectRatio);
        stem->end->setTypeX(QCPItemPosition::ptPlotCoords);
        stem->end->setTypeY(QCPItemPosition::ptAxisRectRatio);
        stem->setVisible(false);
        head = new QCPItemTracer(plot);
        head->setStyle(QCPItemTracer::tsCircle); head->setSize(9);
        head->setPen(QPen(cyan, 1.6)); head->setBrush(cyan);
        head->position->setTypeX(QCPItemPosition::ptPlotCoords);
        head->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
        head->setVisible(false);
        tip = new QCPItemText(plot);
        tip->position->setTypeX(QCPItemPosition::ptPlotCoords);
        tip->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
        tip->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);   // 머리 위로 박스
        tip->setColor(Qt::white); tip->setBrush(QColor(0, 0, 0, 190));
        tip->setPen(QPen(cyan)); tip->setPadding(QMargins(4, 2, 4, 2));
        tip->setFont(QFont(QStringLiteral("sans"), 8, QFont::Bold));
        tip->setVisible(false);
    }

    // 롤리팝 표시/숨김 — TrendSeek 미사용(자체 커서 보유) 탭도 동일 스타일·동작 재사용.
    static void showLollipop(QCPItemLine *stem, QCPItemTracer *head, QCPItemText *tip,
                             double x, const QString &label)
    {
        // 줄기: 바닥(ratio 1) → 머리(kHeadRatio). 머리 위로는 선이 없다.
        if (stem) { stem->start->setCoords(x, 1.0); stem->end->setCoords(x, kHeadRatio); stem->setVisible(true); }
        if (head) { head->position->setCoords(x, kHeadRatio); head->setVisible(true); }
        if (tip)  {
            tip->setText(label);
            // 툴팁을 머리 '옆'(기본 오른쪽, 우측 가장자리면 왼쪽으로 플립)에 둔다 → 선택 지점 위 데이터를 덜 가림.
            bool toLeft = false;
            if (QCustomPlot *p = tip->parentPlot()) {
                const QCPRange r = p->xAxis->range();
                if (r.size() > 0.0) toLeft = (x > r.lower + r.size() * 0.7);
            }
            tip->setPositionAlignment(Qt::AlignVCenter | (toLeft ? Qt::AlignRight : Qt::AlignLeft));
            tip->position->setCoords(x, kHeadRatio);
            tip->setVisible(true);
        }
    }
    static void hideLollipop(QCPItemLine *stem, QCPItemTracer *head, QCPItemText *tip)
    {
        if (stem) stem->setVisible(false);
        if (head) head->setVisible(false);
        if (tip)  tip->setVisible(false);
    }

private:
    static constexpr double kHeadRatio = 0.10;   // 머리 세로 위치(0=상단)

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
            // 선택 지점을 가장자리에 붙이지 않고 '뷰 가운데'로 패닝(현재 폭 유지). 정지 중에만 seek 가
            //  오므로 라이브 autoscale 과 충돌하지 않는다. → 선택 지점과 그 앞뒤 맥락이 함께 보인다.
            if (pc.plot) {
                const double w = pc.plot->xAxis->range().size();
                if (w > 0.0) pc.plot->xAxis->setRange(x - w * 0.5, x + w * 0.5);
            }
            showLollipop(pc.stem, pc.head, pc.tip, x, QString::number(x, 'f', 1));
            if (pc.plot) pc.plot->replot(QCustomPlot::rpQueuedReplot);
        }
    }

    struct PlotCur {
        QCustomPlot *plot = nullptr;
        QCPItemLine *stem = nullptr;
        QCPItemTracer *head = nullptr;
        QCPItemText *tip = nullptr;
    };
    static void setCursorVisible(PlotCur &pc, bool v)
    {
        if (v) return;                       // 표시는 showCursor 가 좌표와 함께 처리
        hideLollipop(pc.stem, pc.head, pc.tip);
    }

    QVector<QPair<double, double>> mMap;   // (x, 절대샘플)
    QVector<PlotCur>               mPlots;
    double                         mPurge = 0.0;
};

#endif // TRENDSEEK_H
