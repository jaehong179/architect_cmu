// =============================================================================
//  DiagBanner 구현
// =============================================================================
#include "DiagBanner.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEvent>

DiagBanner::DiagBanner(QWidget *parent)
    : QFrame(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFrameShape(QFrame::NoFrame);
    // 둥근 어두운 카드 스타일(시스템 알림 느낌).
    setStyleSheet(QStringLiteral(
        "DiagBanner{background-color:#2b2f36;border:2px solid #4a5563;border-radius:14px;}"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(22, 16, 26, 16);
    row->setSpacing(18);

    mIcon = new QLabel(this);
    mIcon->setFixedSize(48, 48);
    mIcon->setAlignment(Qt::AlignCenter);
    mIcon->setStyleSheet(QStringLiteral(
        "background-color:#3a3f47;border-radius:24px;color:white;font-size:26px;font-weight:bold;"));
    row->addWidget(mIcon);

    auto *col = new QVBoxLayout();
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    mTitle = new QLabel(this);
    mTitle->setStyleSheet(QStringLiteral("color:white;font-size:20px;font-weight:bold;"));
    col->addWidget(mTitle);

    mHint = new QLabel(QStringLiteral("Tap to view detailed diagnosis"), this);
    mHint->setStyleSheet(QStringLiteral("color:#b8bdc4;font-size:15px;"));
    col->addWidget(mHint);

    row->addLayout(col);

    setMinimumWidth(420);
    hide();

    if (parent)
        parent->installEventFilter(this);
}

void DiagBanner::showResult(const QString &title, bool healthy, float confidence)
{
    Q_UNUSED(confidence);
    mIcon->setText(healthy ? QStringLiteral("✓") : QStringLiteral("!"));
    mIcon->setStyleSheet(QStringLiteral(
        "background-color:%1;border-radius:24px;color:white;font-size:26px;font-weight:bold;")
        .arg(healthy ? QStringLiteral("#2ed573") : QStringLiteral("#ff6b6b")));
    mTitle->setText(QStringLiteral("Diagnosis complete · %1").arg(title));

    adjustSize();
    reposition();
    raise();
    show();
}

void DiagBanner::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        hide();             // 클릭 시 배너는 사라지고 상세 창으로 전환.
        emit clicked();
    }
    QFrame::mousePressEvent(e);
}

bool DiagBanner::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QFrame::eventFilter(watched, event);
}

void DiagBanner::reposition()
{
    if (!parentWidget()) return;
    const int margin = 20;
    const QSize sz = sizeHint();
    const int w = qMax(sz.width(), minimumWidth());
    const int h = sz.height();
    resize(w, h);
    // 우측 하단 정렬(라즈베리파이 8인치 디스플레이 가시성).
    move(parentWidget()->width() - w - margin,
         parentWidget()->height() - h - margin);
}
