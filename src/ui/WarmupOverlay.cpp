#include "WarmupOverlay.h"
#include <QPainter>
#include <QEvent>
#include <QResizeEvent>

WarmupOverlay::WarmupOverlay(QWidget *parent)
    : QWidget(parent)
{
    // 마우스/키보드 이벤트를 아래로 통과 → 탭 전환 등 사용자 조작 유지
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);

    mTimer = new QTimer(this);
    mTimer->setInterval(1000);
    mTimer->setSingleShot(false);
    connect(mTimer, &QTimer::timeout, this, &WarmupOverlay::onTick);

    mAnim = new QPropertyAnimation(this, "animPhase", this);
    mAnim->setStartValue(0.0);
    mAnim->setEndValue(1.0);
    mAnim->setDuration(900);                      // 0.9초 동안 줄어들며 사라짐
    mAnim->setEasingCurve(QEasingCurve::InQuad);  // 처음엔 천천히, 나중에 빠르게

    if (parent)
        parent->installEventFilter(this);

    hide();
}

void WarmupOverlay::startCountdown(int totalSeconds)
{
    mRemaining = totalSeconds;
    fitToParent();
    raise();
    show();
    startAnimation();
    mTimer->start();
}

void WarmupOverlay::cancel()
{
    mTimer->stop();
    mAnim->stop();
    mRemaining = 0;
    hide();
}

void WarmupOverlay::setAnimPhase(qreal v)
{
    mAnimPhase = v;
    update();
}

void WarmupOverlay::onTick()
{
    --mRemaining;
    if (mRemaining <= 0) {
        mTimer->stop();
        mAnim->stop();
        hide();
        emit warmupFinished();
        return;
    }
    startAnimation();
}

void WarmupOverlay::startAnimation()
{
    mAnim->stop();
    mAnimPhase = 0.0;
    mAnim->start();
}

void WarmupOverlay::fitToParent()
{
    if (parentWidget())
        setGeometry(parentWidget()->rect());
}

bool WarmupOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        fitToParent();
    return QWidget::eventFilter(watched, event);
}

void WarmupOverlay::paintEvent(QPaintEvent *)
{
    if (mRemaining <= 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // animPhase: 0 = 막 나타남(크고 불투명), 1 = 사라짐(작고 투명)
    const qreal phase   = mAnimPhase;
    const qreal opacity = 1.0 - phase;            // 1 → 0 (투명해짐)
    const qreal scale   = 1.0 - phase * 0.55;     // 1 → 0.45 (줄어듦)

    if (opacity < 0.01) return;

    // 반투명 어두운 배경
    p.fillRect(rect(), QColor(0, 0, 0, static_cast<int>(140 * opacity)));

    // ─── 큰 숫자 (최대 80% 불투명) ───────────────────────────────────────
    const int basePx = qMin(width(), height()) / 3;
    const int fontPx = qMax(32, static_cast<int>(basePx * scale));

    QFont numFont;
    numFont.setPixelSize(fontPx);
    numFont.setBold(true);
    p.setFont(numFont);
    p.setPen(QColor(255, 255, 255, static_cast<int>(204 * opacity)));  // 80% max opacity
    p.drawText(rect(), Qt::AlignCenter, QString::number(mRemaining));
}
