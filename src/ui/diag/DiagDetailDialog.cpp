// =============================================================================
//  DiagDetailDialog 구현
// =============================================================================
#include "DiagDetailDialog.h"
#include "DiagCatalog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QPainter>
#include <QFrame>

DiagDetailDialog::DiagDetailDialog(const QString &labelKey, float confidence, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Diagnosis Detail"));
    setModal(true);
    setMinimumWidth(560);

    const diagui::DiagEntry *e = diagui::lookup(labelKey);
    const int pct = static_cast<int>(confidence * 100.0f + 0.5f);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    // ── 제목 ─────────────────────────────────────────────────────────────
    auto *title = new QLabel(this);
    title->setText(QStringLiteral("%1").arg(e ? e->title : labelKey));
    title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:bold;"));
    title->setWordWrap(true);
    root->addWidget(title);

    // ── 예시 그래프 ──────────────────────────────────────────────────────
    auto *exLabel = new QLabel(QStringLiteral("Example trace"), this);
    exLabel->setStyleSheet(QStringLiteral("color:#888;font-size:12px;"));
    root->addWidget(exLabel);

    const QSize imgSize(520, 150);
    QPixmap pm(imgSize);
    pm.fill(Qt::white);
    {
        QPainter p(&pm);
        diagui::paintExample(p, QRect(QPoint(0, 0), imgSize),
                             e ? e->pattern : diagui::PatternNormal);
    }
    auto *graph = new QLabel(this);
    graph->setPixmap(pm);
    graph->setFixedSize(imgSize);
    graph->setStyleSheet(QStringLiteral("border:1px solid #525c6b;"));
    auto *graphRow = new QHBoxLayout();
    graphRow->addStretch();
    graphRow->addWidget(graph);
    graphRow->addStretch();
    root->addLayout(graphRow);

    // ── 원인 ─────────────────────────────────────────────────────────────
    auto *causeHdr = new QLabel(QStringLiteral("Cause / Description"), this);
    causeHdr->setStyleSheet(QStringLiteral("color:#ffffff;font-weight:bold;font-size:15px;margin-top:4px;"));
    root->addWidget(causeHdr);

    auto *cause = new QLabel(e ? e->cause : QStringLiteral("No information"), this);
    cause->setWordWrap(true);
    cause->setStyleSheet(QStringLiteral("font-size:15px;color:#d6d9dd;"));
    root->addWidget(cause);

    // ── 조치 사항 ────────────────────────────────────────────────────────
    auto *actionHdr = new QLabel(QStringLiteral("Action"), this);
    actionHdr->setStyleSheet(QStringLiteral("color:#ffffff;font-weight:bold;font-size:15px;margin-top:4px;"));
    root->addWidget(actionHdr);

    auto *action = new QLabel(e ? e->action : QStringLiteral("No information"), this);
    action->setWordWrap(true);
    action->setStyleSheet(QStringLiteral(
        "font-size:16px;font-weight:bold;color:#aef5c0;background-color:#1f3b27;"
        "border:1px solid #2e6b3e;border-radius:6px;padding:10px;"));
    root->addWidget(action);

    // ── 닫기 버튼 ────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setMinimumWidth(96);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
}
