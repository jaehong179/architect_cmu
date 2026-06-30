#ifndef PLOTHELPERS_H
#define PLOTHELPERS_H
// QCustomPlot 셋업 공용 헬퍼 — 여러 탭이 복붙하던 그래프 생성/초기화 관용구를 모은다(DRY).
//  의도적으로 '시각에 영향 없는' 동작만 제공한다: 펜 생성은 기존 호출과 비트 단위로 동일하게 재현하고
//  (QPen(color) vs QPen(color,width) 구분 유지), 데이터 clear 는 스타일을 건드리지 않는다.
//  특수 스타일(스캐터·채움·축 라벨 등)은 각 탭 고유이므로 그대로 탭에 남긴다(과도한 일반화 회피).
#include "qcustomplot.h"
#include <QColor>
#include <QString>

namespace PlotHelpers {

// 라인 그래프 1개를 추가하고 펜을 설정한 뒤 그 그래프를 반환.
//  width < 0 이면 QPen(color)(폭 기본=1), width >= 0 이면 QPen(color, width) — 기존 코드와 동일한 QPen.
inline QCPGraph *addLineGraph(QCustomPlot *plot, const QColor &color,
                              double width = -1.0, const QString &name = QString())
{
    QCPGraph *g = plot->addGraph();
    g->setPen(width >= 0.0 ? QPen(color, width) : QPen(color));
    if (!name.isEmpty()) g->setName(name);
    return g;
}

// [PERF] 페인트 비용 절감 — PERF 측정상 병목은 DSP가 아니라 QCustomPlot 래스터(페인트 ~32ms)였다.
//  비싼 요소(선·채움·스캐터·아이템)의 안티앨리어싱만 끄고(SW 래스터에서 AA·채움이 가장 비쌈),
//  빠른 폴리라인 힌트 + 적응 샘플링(화면폭 초과 표본 솎음)을 켠다. 텍스트·축·그리드는 가독성 위해 AA 유지.
//  시각은 거의 동일(선이 살짝 또렷/딱딱), 페인트만 가벼워진다. 그래프 추가 '후' 호출.
inline void applyFastPaint(QCustomPlot *plot)
{
    if (!plot) return;
    plot->setNotAntialiasedElements(QCP::aeFills | QCP::aePlottables | QCP::aeScatters | QCP::aeItems);
    plot->setPlottingHint(QCP::phFastPolylines, true);
    for (int i = 0; i < plot->graphCount(); ++i)
        plot->graph(i)->setAdaptiveSampling(true);
}

// 그래프 gi 에서 키 x 에 가장 가까운 점의 값(y)을 outY 로. (seek 툴팁에 '그 지점 값' 표시용; 선형 탐색)
inline bool nearestValue(QCustomPlot *plot, int gi, double x, double &outY)
{
    if (!plot || gi < 0 || gi >= plot->graphCount()) return false;
    auto data = plot->graph(gi)->data();
    if (!data || data->isEmpty()) return false;
    double best = 1e300; bool found = false;
    for (auto it = data->constBegin(); it != data->constEnd(); ++it) {
        const double d = qAbs(it->key - x);
        if (d < best) { best = d; outY = it->value; found = true; }
    }
    return found;
}

// 플롯의 모든 그래프 데이터를 비운다(스타일/아이템/축은 불변). replot 은 호출측 책임.
inline void clearAllGraphs(QCustomPlot *plot)
{
    if (!plot) return;
    for (int i = 0; i < plot->graphCount(); ++i)
        plot->graph(i)->data()->clear();
}

} // namespace PlotHelpers

#endif // PLOTHELPERS_H
