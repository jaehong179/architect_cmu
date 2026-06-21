#include "LegendBox.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

QWidget *makeLegendBox(const QString &tableHtml, QWidget *parent, bool startExpanded)
{
    auto *box = new QWidget(parent);
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0); v->setSpacing(2);

    auto *btn = new QPushButton(box);
    btn->setCheckable(true);
    btn->setStyleSheet(QStringLiteral("QPushButton{ text-align:left; border:none; font-weight:bold; padding:2px; }"));

    // RichText QLabel 은 stylesheet color 를 무시하는 경우가 있어 body 에 글자색을 명시한다.
    const QString html = QStringLiteral(
        "<html><body style=\"color:#222;\">%1</body></html>").arg(tableHtml);
    auto *key = new QLabel(html, box);
    key->setTextFormat(Qt::RichText);
    key->setWordWrap(true);
    key->setStyleSheet(QStringLiteral("QLabel{ background:#f6f6f6; border:1px solid #c4c4c4; border-radius:4px; padding:5px; color:#222; }"));

    // 초기 상태 적용
    btn->setChecked(startExpanded);
    key->setVisible(startExpanded);
    btn->setText(startExpanded ? QStringLiteral("▾ Legend (collapse)") : QStringLiteral("▸ Legend (expand)"));

    QObject::connect(btn, &QPushButton::toggled, key, [btn, key](bool on){
        key->setVisible(on);
        btn->setText(on ? QStringLiteral("▾ Legend (collapse)") : QStringLiteral("▸ Legend (expand)"));
    });

    v->addWidget(btn);
    v->addWidget(key);
    return box;
}
