#pragma once
#include <QWidget>
#include <QTimer>
#include <QPropertyAnimation>

// =============================================================================
//  WarmupOverlay — 측정 대기(warm-up) 카운트다운 오버레이 위젯
// -----------------------------------------------------------------------------
//  부모 QWidget(GraphicsTabWidget) 위에 올라앉아 남은 초를 크게 표시하고,
//  매 초마다 숫자가 크게 나타났다가 줄어들며 사라지는 애니메이션을 실행한다.
//
//  - WA_TransparentForMouseEvents=true → 오버레이가 마우스/키보드 이벤트를 통과,
//    아래의 탭 위젯은 카운트다운 중에도 정상 조작 가능(탭 전환 등).
//  - 부모 위젯 리사이즈 이벤트를 감지해 자동으로 geometry 동기화.
// =============================================================================

class WarmupOverlay : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal animPhase READ animPhase WRITE setAnimPhase)

public:
    explicit WarmupOverlay(QWidget *parent = nullptr);

    // 카운트다운 시작. totalSeconds > 0 이어야 함.
    void startCountdown(int totalSeconds);

    // 외부에서 강제 취소 (stopSession / pause 시 호출).
    void cancel();

    qreal animPhase() const     { return mAnimPhase; }
    void  setAnimPhase(qreal v);

signals:
    void warmupFinished();   // 카운트다운 0 도달 시 emit

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onTick();

private:
    void fitToParent();
    void startAnimation();

    int    mRemaining = 0;
    qreal  mAnimPhase = 0.0;
    QTimer            *mTimer = nullptr;
    QPropertyAnimation *mAnim = nullptr;
};
