// =============================================================================
//  TabManager.cpp — 구현 (등록 · 브로드캐스트 · 탭별 성능 계측)
// =============================================================================
#include "TabManager.h"
#include "TabView.h"
#include "WaveSink.h"              // 비시각 청취자(8분 이력 버퍼 등)
#include <QTabWidget>
#include <QApplication>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QTabBar>
#include "PerfInstrumentation.h"   // [PERF 계측 · §F-1] 탭별 갱신시간 tab_update_ms

TabManager::TabManager(QTabWidget *host, QObject *parent)
    : QObject(parent), mHost(host)
{
    if (mHost) {
        qApp->installEventFilter(this);
    }
}

TabManager::~TabManager()
{
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

void TabManager::registerTab(TabView *tab)
{
    if (!tab || !mHost) return;
    mTabs.push_back(tab);
    mHost->addTab(tab, tab->tabTitle());   // 부모가 QTabWidget 이 되어 수명 관리됨
}

void TabManager::broadcastMeasurement(const MeasurementSnapshot &snap)
{
    if (mInWarmup) return;   // [측정 대기] 워밍업 중 스냅샷 전달 차단.
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
    if (mInWarmup) return;   // [측정 대기] 워밍업 중 파형과 WaveSink 모두 차단.
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

void TabManager::setPaused(bool p)
{
    const bool resuming = (mPaused && !p);
    if (p && !mPaused) mSeekedSincePause = false;   // 새 정지 구간 시작 → seek 추적 초기화
    mPaused = p;
    if (resuming) {                     // 라이브 복귀 → 정지 중 seek 가 있었던 경우에만 임시 버퍼 정리
        const bool seeked = mSeekedSincePause;
        for (TabView *t : mTabs)
            if (t) t->onResumeLive(seeked);
    }
}

void TabManager::broadcastSeek(double absSample)
{
    if (!mPaused) return;   // seek 는 정지(동결) 중에만 의미 — 라이브 클릭은 무시.
    mSeekedSincePause = true;   // 이번 정지 구간에서 seek 발생 → resume 시 해당 버퍼 정리 필요
    for (TabView *t : mTabs)
        if (t) t->onSeek(absSample);
}

void TabManager::broadcastReset()
{
    mPaused = false;   // 새 세션 시작 = 정지 해제(전역).
    for (TabView *t : mTabs)
        if (t) t->onResetSession();
}

bool TabManager::eventFilter(QObject *watched, QEvent *event)
{
    if (!mHost) return QObject::eventFilter(watched, event);

    // 1. Mouse Wheel Scroll on Tab Bar to switch tabs
    if (event->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent *>(event);
        QTabBar *bar = mHost->findChild<QTabBar *>();
        if (bar) {
            QPoint localToBar = bar->mapFromGlobal(we->globalPosition().toPoint());
            if (bar->rect().contains(localToBar)) {
                int delta = we->angleDelta().y();
                if (delta == 0) {
                    delta = we->angleDelta().x();
                }
                if (delta > 0) {
                    // Scroll up/left: previous tab (clamped to 0)
                    int prevIndex = mHost->currentIndex() - 1;
                    if (prevIndex >= 0) {
                        mHost->setCurrentIndex(prevIndex);
                    }
                } else if (delta < 0) {
                    // Scroll down/right: next tab (clamped to count - 1)
                    int nextIndex = mHost->currentIndex() + 1;
                    if (nextIndex < mHost->count()) {
                        mHost->setCurrentIndex(nextIndex);
                    }
                }
                event->accept();
                return true;
            }
        }
    }

    // 2. Touch or Mouse Swipe/Drag Gesture to switch tabs
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin) {
        QPoint pos;
        if (event->type() == QEvent::MouseButtonPress) {
            pos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        } else {
            QTouchEvent *te = static_cast<QTouchEvent *>(event);
            if (!te->points().isEmpty()) {
                pos = te->points().first().globalPosition().toPoint();
            } else {
                return QObject::eventFilter(watched, event);
            }
        }

        QPoint localPos = mHost->mapFromGlobal(pos);
        if (mHost->rect().contains(localPos)) {
            mDragActive = true;
            mStartPos = pos;
            mStartTabWidgetPos = localPos;
        } else {
            mDragActive = false;
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::TouchEnd) {
        if (mDragActive) {
            mDragActive = false;
            QPoint pos;
            if (event->type() == QEvent::MouseButtonRelease) {
                pos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            } else {
                QTouchEvent *te = static_cast<QTouchEvent *>(event);
                if (!te->points().isEmpty()) {
                    pos = te->points().first().globalPosition().toPoint();
                } else {
                    return QObject::eventFilter(watched, event);
                }
            }

            int dx = pos.x() - mStartPos.x();
            int dy = pos.y() - mStartPos.y();

            // Thresholds for swipe: Horizontal distance > 100px, Vertical deviation < 80px
            if (qAbs(dx) > 100 && qAbs(dy) < 80) {
                int w = mHost->width();
                int edgeThreshold = 60; // Start swipe within 60px of the edge

                bool isLeftEdge = (mStartTabWidgetPos.x() <= edgeThreshold);
                bool isRightEdge = (mStartTabWidgetPos.x() >= w - edgeThreshold);

                // Check if start position is in the tab bar area
                QTabBar *bar = mHost->findChild<QTabBar *>();
                bool isTabBar = false;
                if (bar) {
                    QPoint localToBar = bar->mapFromGlobal(mStartPos);
                    isTabBar = bar->rect().contains(localToBar);
                }

                if (isLeftEdge && dx > 0) {
                    // Swipe right from left edge -> previous tab
                    int prevIndex = mHost->currentIndex() - 1;
                    if (prevIndex < 0) prevIndex = mHost->count() - 1;
                    mHost->setCurrentIndex(prevIndex);
                    event->accept();
                    return true;
                }
                else if (isRightEdge && dx < 0) {
                    // Swipe left from right edge -> next tab
                    int nextIndex = mHost->currentIndex() + 1;
                    if (nextIndex >= mHost->count()) nextIndex = 0;
                    mHost->setCurrentIndex(nextIndex);
                    event->accept();
                    return true;
                }
                else if (isTabBar) {
                    // Drag/Swipe on the tab bar directly -> switch tab
                    if (dx > 0) {
                        int prevIndex = mHost->currentIndex() - 1;
                        if (prevIndex < 0) prevIndex = mHost->count() - 1;
                        mHost->setCurrentIndex(prevIndex);
                    } else {
                        int nextIndex = mHost->currentIndex() + 1;
                        if (nextIndex >= mHost->count()) nextIndex = 0;
                        mHost->setCurrentIndex(nextIndex);
                    }
                    event->accept();
                    return true;
                }
            }
        }
    }

    return QObject::eventFilter(watched, event);
}
