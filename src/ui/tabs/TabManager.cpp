// =============================================================================
//  TabManager.cpp — 구현 (등록 · 브로드캐스트 · 탭별 성능 계측)
// =============================================================================
#include "TabManager.h"
#include "TabView.h"
#include <QTabWidget>
#include "PerfInstrumentation.h"   // [PERF 계측 · §F-1] 탭별 갱신시간 tab_update_ms

TabManager::TabManager(QTabWidget *host, QObject *parent)
    : QObject(parent), mHost(host)
{
}

void TabManager::registerTab(TabView *tab)
{
    if (!tab || !mHost) return;
    mTabs.push_back(tab);
    mHost->addTab(tab, tab->tabTitle());   // 부모가 QTabWidget 이 되어 수명 관리됨
}

void TabManager::broadcastMeasurement(const MeasurementSnapshot &snap)
{
    // 모든 탭에 스냅샷 전달. 탭별 onMeasurement 소요시간을 측정해 병목 후보를 드러낸다.
    //  (탭 내부에서 무거운 replot 은 isVisible() 가드를 권장 → 숨은 탭은 데이터만 누적)
    for (TabView *t : mTabs) {
        if (!t) continue;
        const double t0 = Perf::nowMs();
        t->onMeasurement(snap);
        // [§F-1 · QA-SC-01] extra 에 탭 제목을 남겨 grep 으로 탭별 비용 분리 가능.
        Perf::log("F-1", "QA-SC-01", "tab_update_ms", Perf::nowMs() - t0, "ms", t->tabTitle());
    }
}

void TabManager::broadcastWave(const WaveBlock &wave)
{
    // 고빈도(처리 슬라이스마다) → perf 로그는 생략(로그 폭주 방지). 탭이 자체적으로
    //  isVisible() 가드로 렌더 비용을 줄인다.
    for (TabView *t : mTabs)
        if (t) t->onWave(wave);
}

void TabManager::broadcastReset()
{
    for (TabView *t : mTabs)
        if (t) t->onResetSession();
}
