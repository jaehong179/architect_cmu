#pragma once
// =============================================================================
//  DiagBanner — 진단 완료 알림 배너(토스트)
// -----------------------------------------------------------------------------
//  부모 위젯(GraphicsTabWidget) 우측 상단에 떠서 진단 결과 제목을 보여준다.
//  클릭하면 clicked() 를 emit 하여 상세 창을 띄우도록 한다.
//  의존성: Qt Widgets 뿐. 다른 앱 모듈에 의존하지 않는다(부모 위젯만 참조).
// =============================================================================
#include <QFrame>

class QLabel;

class DiagBanner : public QFrame
{
    Q_OBJECT
public:
    explicit DiagBanner(QWidget *parent = nullptr);

    // 진단 결과 표시(우측 상단에 슬라이드 없이 즉시 노출).
    //  title: 사람이 읽는 제목, healthy: 정상 여부(색 구분), confidence: 0~1.
    void showResult(const QString &title, bool healthy, float confidence);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reposition();

    QLabel *mIcon  = nullptr;
    QLabel *mTitle = nullptr;
    QLabel *mHint  = nullptr;
};
