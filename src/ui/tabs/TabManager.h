#ifndef TABMANAGER_H
#define TABMANAGER_H
// =============================================================================
//  TabManager — 디스플레이 탭 등록 · 갱신 브로드캐스트 · 탭별 성능 계측
// -----------------------------------------------------------------------------
//  [목적]  MainWindow 를 얇은 코디네이터로 유지하기 위해, "탭 묶음"을 한곳에서
//          관리한다.  MainWindow 는 측정 스냅샷 한 개를 만들어 broadcast 만 하면
//          되고, 어떤 탭이 몇 개 있는지 알 필요가 없다.
//            - registerTab():  QTabWidget 에 탭을 붙이고 목록에 보관
//            - broadcastMeasurement():  모든 탭에 스냅샷 전달 (+탭별 갱신시간 로깅)
//            - broadcastReset():  세션 리셋을 모든 탭에 전파
//
//  [성능 계측 · §F-1]  각 탭의 onMeasurement() 소요시간을 tab_update_ms 로 기록한다.
//          QA-AVAIL-01 시나리오의 "12개 탭을 순환하며 갱신"하는 부하에서, 어느 탭이
//          렌더 비용을 많이 쓰는지(병목 후보)를 정량화하기 위함.  (perf_log.csv)
//
//  ※ 현재는 모든 탭에 브로드캐스트한다(최악 부하 = perf 데이터 확보 목적).
//    추후 "보이는 탭만 갱신"(Reduce computational overhead 택틱)으로 최적화 예정 —
//    각 탭이 onMeasurement 안에서 isVisible() 로 무거운 replot 을 자체 가드한다.
// =============================================================================

#include <QObject>
#include <QVector>
#include <QPoint>
#include "MeasurementModel.h"

class QTabWidget;
class TabView;
class WaveSink;     // 엔벨로프 방송을 받는 비시각 청취자(예: 8분 이력 버퍼) — tabs/WaveSink.h

class TabManager : public QObject
{
    Q_OBJECT
public:
    // host = 탭이 부착될 기존 QTabWidget(ui->GraphicsTabWidget).
    explicit TabManager(QTabWidget *host, QObject *parent = nullptr);
    ~TabManager() override;

    // 탭을 QTabWidget 에 추가하고 갱신 대상 목록에 등록. tab 소유권은 QTabWidget(부모)로.
    void registerTab(TabView *tab);

    // 측정 스냅샷을 모든 탭에 전달(+탭별 갱신시간 §F-1 로깅).
    void broadcastMeasurement(const MeasurementSnapshot &snap);

    // 파형(엔벨로프+이벤트) 블록을 모든 탭 + 등록된 WaveSink 에 전달. (고빈도 → per-tab 로깅 생략)
    void broadcastWave(const WaveBlock &wave);

    // 세션 리셋을 모든 탭에 전파.
    void broadcastReset();

    // 세션 리셋을 skip 탭만 제외하고 전파. (MPS 포지션 전환 시 시퀀스 누적표는 보존)
    void broadcastResetExcept(TabView *skip);

    // [③ 시점 동기화] 선택한 절대 샘플 인덱스를 모든 탭에 전파(정지 중 트렌드 클릭 → 스코프 점프).
    void broadcastSeek(double absSample);

    // 비시각 청취자 등록 — 탭이 아닌 수집기(8분 이력 버퍼 등)가 엔벨로프 방송을 받도록.
    //  소유권은 호출 측(보통 MainWindow)이 가진다. (OCP: 방송 로직 수정 없이 확장)
    void addWaveSink(WaveSink *sink);

    // [8분 스크롤백] 전역 정지 — 켜면 모든 탭 방송을 멈춰 전체 동결(이력 버퍼는 계속 채움).
    //  Rate/Scope 의 Pause 버튼이 MainWindow 경유로 토글한다.
    void setPaused(bool p);   // 정지 ON/OFF — 해제 시 각 탭에 onResumeLive() 통지
    bool isPaused() const  { return mPaused; }

    // [측정 대기] 워밍업 게이트 — 활성화 시 broadcastWave/broadcastMeasurement 차단
    //  (엔진은 내부적으로 계속 계산하지만 탭·이력 버퍼에는 아무것도 전달되지 않음)
    void setWarmup(bool inWarmup) { mInWarmup = inWarmup; }
    bool isWarmup() const  { return mInWarmup; }

    int count() const { return mTabs.size(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTabWidget         *mHost = nullptr;
    QVector<TabView *>  mTabs;
    QVector<WaveSink *> mWaveSinks;   // broadcastWave 시 함께 통지(소유 안 함)
    bool                mPaused = false;
    bool                mSeekedSincePause = false;   // 이번 정지 구간에서 seek 발생 여부(resume 정리용)
    bool                mInWarmup = false;           // [측정 대기] 워밍업 게이트 활성 여부

    // Swipe/Gesture state variables
    bool                mDragActive = false;
    QPoint              mStartPos;
    QPoint              mStartTabWidgetPos;
};

#endif // TABMANAGER_H
