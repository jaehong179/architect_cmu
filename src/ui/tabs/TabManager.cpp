// =============================================================================
//  TabManager.cpp — 구현 (등록 · 브로드캐스트 · 탭별 성능 계측)
// =============================================================================
#include "TabManager.h"
#include "TabView.h"
#include "WaveSink.h"              // 비시각 청취자(8분 이력 버퍼 등)
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
    if (mPaused) return;   // [8분 스크롤백] 전역 정지 중엔 모든 탭 동결.
    // 모든 탭에 스냅샷 전달. 탭별 onMeasurement 소요시간을 측정해 병목 후보를 드러낸다.
    //  (탭 내부에서 무거운 replot 은 isVisible() 가드를 권장 → 숨은 탭은 데이터만 누적)
    for (TabView *t : mTabs) {
        if (!t) continue;
        const double t0 = PERF_NOW();
        t->onMeasurement(snap);
        // [§F-1 · QA-SC-01] extra 에 탭 제목을 남겨 grep 으로 탭별 비용 분리 가능.
        PERF_LOG("F-1", "QA-SC-01", "tab_update_ms", PERF_NOW() - t0, "ms", t->tabTitle());
    }
}

void TabManager::broadcastWave(const WaveBlock &wave)
{
    // 고빈도(처리 슬라이스마다) → perf 로그는 생략(로그 폭주 방지). 탭이 자체적으로
    //  isVisible() 가드로 렌더 비용을 줄인다.
    if (!mPaused)                       // [8분 스크롤백] 정지 중엔 탭 갱신만 동결.
        for (TabView *t : mTabs)
            if (t) t->onWave(wave);
    // 비시각 청취자(8분 이력 버퍼 등)에는 정지와 무관하게 항상 전달 → 이력은 계속 누적.
    for (WaveSink *s : mWaveSinks)
        if (s) s->onWave(wave);
}

void TabManager::addWaveSink(WaveSink *sink)
{
    if (sink) mWaveSinks.push_back(sink);
}

void TabManager::broadcastSeek(double absSample)
{
    // 정지와 무관하게 전파(정지 중에만 스코프가 반응). 트렌드 탭 클릭 → 모든 스코프 탭 점프.
    for (TabView *t : mTabs)
        if (t) t->onSeek(absSample);
}

void TabManager::broadcastReset()
{
    mPaused = false;   // 새 세션 시작 = 정지 해제(전역).
    for (TabView *t : mTabs)
        if (t) t->onResetSession();
}
