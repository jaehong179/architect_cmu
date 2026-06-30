#ifndef TABVIEW_H
#define TABVIEW_H
// =============================================================================
//  TabView — 모든 디스플레이 탭의 추상 베이스 (QWidget 파생)
// -----------------------------------------------------------------------------
//  [목적]  각 그래프/디스플레이 탭을 "자기 완결적 모듈"로 만들기 위한 공통 계약.
//          탭은 (1) 제목을 제공하고, (2) 측정 스냅샷을 받아 자신을 갱신하고,
//          (3) 세션 리셋 시 자기 상태를 비우면 된다. 그 외에는 서로/코어와 무관.
//          → TabManager 가 이 인터페이스만으로 모든 탭을 등록·갱신·리셋한다.
//            (QA-MOD-01 Extensibility / "Split module" 택틱)
//
//  [추가 방법]  새 탭 = TabView 를 상속한 클래스 1개 + TabManager::registerTab()
//               한 줄.  기존 탭이나 코어 DSP 는 건드릴 필요가 없다.
//
//  ※ Qt 에서 QObject 다중상속이 불가하므로 '순수 인터페이스 + QWidget' 대신
//    QWidget 파생 추상 클래스로 둔다(권장 패턴).
// =============================================================================

#include <QWidget>
#include <QString>
#include <QShowEvent>
#include <QElapsedTimer>
#include "MeasurementModel.h"

class TabView : public QWidget
{
    Q_OBJECT
public:
    explicit TabView(QWidget *parent = nullptr) : QWidget(parent) {}
    static constexpr int kCoalesceMs = 16;   // [§3] ~60fps 상한(디스플레이 리프레시) — 버스트 렌더 합치기
    ~TabView() override = default;

    // 탭 라벨(QTabWidget 에 표시될 짧은 제목). 각 탭이 반드시 제공.
    virtual QString tabTitle() const = 0;

    // 새 측정 스냅샷 도착 시 호출(메인 스레드). 기본 구현은 무시.
    //  [성능] 비용이 큰 작업(replot 등)은 가시 상태일 때만 수행 권장 → isVisible() 참고.
    virtual void onMeasurement(const MeasurementSnapshot & /*snap*/) {}

    // 새 파형(엔벨로프+이벤트) 블록 도착 시 호출(메인 스레드, 처리 슬라이스마다).
    //  스코프 계열 탭만 override. 포인터는 호출 동안만 유효 → 필요분을 자기 버퍼로 복사.
    virtual void onWave(const WaveBlock & /*wave*/) {}

    // 측정 세션 리셋(Start/모드 전환) 시 호출 — 누적 데이터·그래프를 비운다.
    virtual void onResetSession() {}

    // [8분 스크롤백/③] 시점 점프 — 트렌드 탭에서 선택한 '절대 샘플 인덱스'로 이동.
    //  정지 중에만 의미. 8분 이력 버퍼를 읽는 스코프 탭이 override 해 그 시점을 그린다. 기본 무시.
    virtual void onSeek(double /*absSample*/) {}

    // [③] 보이는 seek 커서/선택 표시를 숨긴다(데이터는 유지). resume 시 선택 리셋용 헬퍼.
    virtual void onSeekClear() {}

    // [③] 정지 해제(라이브 복귀) 시 호출 — 정지 중 seek 가 있었으면(seeked=true) seek 로 채운
    //  임시 버퍼를 비워 라이브로 깨끗이 복귀. seek 없이 정지만 했으면 버퍼가 연속이므로 그대로 둔다.
    //  (소스까지 멈추는 '전체 정지'라 resume 시 인덱스가 끊기지 않음 → 불필요한 리빌드 방지.)
    virtual void onResumeLive(bool /*seeked*/) {}

protected:
    // 탭이 (재)표시될 때 호출 — 측정이 멈춘 상태에서 탭을 전환해도 보관 중인
    //  데이터로 화면을 다시 그리도록 한다(빈 화면 방지). 갱신 트리거가 없는 idle 시 중요.
    virtual void onShown() {}
    void showEvent(QShowEvent *e) override { QWidget::showEvent(e); onShown(); }

    // [§3 렌더 코얼레스] 한 handleInputData 가 백로그로 여러 4096-슬라이스를 처리하면 onWave 가
    //  여러 번 불려 매 프레임 배열을 새로 빌드하는 탭은 중복 렌더가 된다(페인트는 rpQueuedReplot 이
    //  이미 1회로 합침). frameDue() 로 디스플레이보다 빠른 렌더를 스킵 → 'handleInputData 당 ~1 렌더'.
    //  정상 실시간(슬라이스 간격 ≫ kCoalesceMs)에선 항상 true(영향 없음), 버스트에서만 합쳐진다.
    bool frameDue()
    {
        if (mFrameTimer.isValid() && mFrameTimer.elapsed() < kCoalesceMs) return false;
        mFrameTimer.restart();
        return true;
    }
    void resetFrameThrottle() { mFrameTimer.invalidate(); }   // 탭 전환/seek 등 즉시 렌더 필요 시

private:
    QElapsedTimer mFrameTimer;   // [§3] frameDue() 코얼레스 타이머
};

#endif // TABVIEW_H
