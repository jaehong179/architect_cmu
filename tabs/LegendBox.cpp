#include "LegendBox.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>

QWidget *makeLegendBox(const QString &tableHtml, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0); v->setSpacing(2);

    auto *btn = new QPushButton(QStringLiteral("▾ 범례 (접기)"), box);
    btn->setCheckable(true); btn->setChecked(true);              // 기본 펼침
    btn->setStyleSheet(QStringLiteral("QPushButton{ text-align:left; border:none; font-weight:bold; padding:2px; }"));

    auto *key = new QLabel(tableHtml, box);
    key->setTextFormat(Qt::RichText);
    key->setWordWrap(true);
    key->setStyleSheet(QStringLiteral("QLabel{ background:#f6f6f6; border:1px solid #c4c4c4; border-radius:4px; padding:5px; }"));

    QObject::connect(btn, &QPushButton::toggled, key, [btn, key](bool on){
        key->setVisible(on);
        btn->setText(on ? QStringLiteral("▾ 범례 (접기)") : QStringLiteral("▸ 범례 (펼치기)"));
    });

    v->addWidget(btn);
    v->addWidget(key);
    return box;
}
