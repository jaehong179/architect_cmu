#include "PositionChangeDialog.h"
#include "PositionNames.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QWidget>

PositionChangeDialog::PositionChangeDialog(const QList<int> &measuredIndices,
                                           const QList<int> &remainingIndices,
                                           Mode mode,
                                           QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedSize(500, 440);
    setupUi(measuredIndices, remainingIndices, mode);
}

QString PositionChangeDialog::getShortCode(const QString &fullName) const
{
    const QString key = canonicalCorePositionKey(fullName);
    if (!key.isEmpty()) return key;
    if (fullName.contains(QLatin1Char(' '))) return fullName.split(QLatin1Char(' ')).first();
    return fullName;
}

QString PositionChangeDialog::getDisplayName(const QString &fullName) const
{
    const QString key = canonicalCorePositionKey(fullName);
    if (key == QStringLiteral("CH")) return QStringLiteral("Dial Up");
    if (key == QStringLiteral("CB")) return QStringLiteral("Dial Down");
    if (key == QStringLiteral("9H")) return QStringLiteral("Crown Right");
    if (key == QStringLiteral("6H")) return QStringLiteral("Crown Left");
    if (key == QStringLiteral("3H")) return QStringLiteral("Crown Up");
    if (key == QStringLiteral("12H")) return QStringLiteral("Crown Down");
    return fullName;
}

QWidget *PositionChangeDialog::createPositionRow(const QString &name, bool measured, QWidget *parent) const
{
    auto *row = new QWidget(parent);
    row->setObjectName(measured ? QStringLiteral("MeasuredRow") : QStringLiteral("RemainingRow"));
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(10, 5, 10, 5);
    rowLayout->setSpacing(8);

    auto *icon = new QLabel(measured ? QStringLiteral("\u2713") : QStringLiteral("\u25CB"), row);
    icon->setObjectName(measured ? QStringLiteral("MeasuredIcon") : QStringLiteral("RemainingIcon"));
    icon->setFixedWidth(14);
    icon->setAlignment(Qt::AlignCenter);

    auto *code = new QLabel(getShortCode(name), row);
    code->setObjectName(measured ? QStringLiteral("MeasuredCode") : QStringLiteral("RemainingCode"));
    code->setFixedWidth(34);
    code->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *display = new QLabel(getDisplayName(name), row);
    display->setObjectName(measured ? QStringLiteral("MeasuredName") : QStringLiteral("RemainingName"));
    display->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    rowLayout->addWidget(icon);
    rowLayout->addWidget(code);
    rowLayout->addWidget(display, 1);
    return row;
}

QWidget *PositionChangeDialog::createListPanel(const QString &title,
                                               const QList<int> &positionIndices,
                                               bool measured,
                                               QWidget *parent) const
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(measured ? QStringLiteral("MeasuredPanel") : QStringLiteral("RemainingPanel"));
    panel->setFrameShape(QFrame::NoFrame);

    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(12, 10, 12, 10);
    panelLayout->setSpacing(6);

    auto *header = new QLabel(title, panel);
    header->setObjectName(measured ? QStringLiteral("MeasuredHeader") : QStringLiteral("RemainingHeader"));
    panelLayout->addWidget(header);

    const QStringList names = standardPositionNames();
    for (int idx : positionIndices) {
        if (idx < 0 || idx >= names.size())
            continue;
        panelLayout->addWidget(createPositionRow(names.at(idx), measured, panel));
    }

    panelLayout->addStretch(1);
    return panel;
}

void PositionChangeDialog::applyStyleSheet(Mode mode)
{
    const bool complete = (mode == Mode::SequenceComplete);
    setStyleSheet(QStringLiteral(
        "QDialog#PositionChangeDialog {"
        "  background-color: #18181f;"
        "  border: 1px solid #2e2e3a;"
        "}"
        "QWidget#HeaderWidget {"
        "  background-color: #252530;"
        "  border-bottom: 1px solid #2e2e3a;"
        "}"
        "QLabel#TitleIcon {"
        "  color: %1;"
        "  font-size: 18px;"
        "  font-weight: bold;"
        "  margin-right: 5px;"
        "}"
        "QLabel#TitleLabel {"
        "  color: %2;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QLabel#StepLabel {"
        "  color: #b0b0b0;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QWidget#BodyWidget {"
        "  background-color: #18181f;"
        "}"
        "QLabel#CountLabel {"
        "  color: %2;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QFrame#MeasuredPanel, QFrame#RemainingPanel {"
        "  background-color: #1e1e26;"
        "  border: 1px solid #2e2e3a;"
        "  border-radius: 6px;"
        "}"
        "QLabel#MeasuredHeader {"
        "  color: #00ff66;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  letter-spacing: 0.5px;"
        "}"
        "QLabel#RemainingHeader {"
        "  color: #ab47bc;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  letter-spacing: 0.5px;"
        "}"
        "QLabel#MeasuredIcon {"
        "  color: #00ff66;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "}"
        "QLabel#RemainingIcon {"
        "  color: #888888;"
        "  font-size: 12px;"
        "}"
        "QLabel#MeasuredCode {"
        "  color: #b0b0b0;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QLabel#MeasuredName {"
        "  color: #888888;"
        "  font-size: 12px;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QLabel#RemainingCode {"
        "  color: #df78ef;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QLabel#RemainingName {"
        "  color: #e0e0e0;"
        "  font-size: 12px;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QLabel#HintLabel {"
        "  color: #888888;"
        "  font-size: 11px;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QFrame#SegmentActive {"
        "  background-color: %3;"
        "  border: none;"
        "  border-radius: 2px;"
        "}"
        "QFrame#SegmentInactive {"
        "  background-color: #323242;"
        "  border: none;"
        "  border-radius: 2px;"
        "}"
        "QPushButton#ConfirmButton {"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  background-color: %4;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QPushButton#ConfirmButton:hover {"
        "  background-color: %5;"
        "}"
        "QPushButton#ConfirmButton:pressed {"
        "  background-color: %6;"
        "}"
    ).arg(complete ? QStringLiteral("#00ff66") : QStringLiteral("#ab47bc"),
          complete ? QStringLiteral("#00ff66") : QStringLiteral("#df78ef"),
          complete ? QStringLiteral("#00ff66") : QStringLiteral("#ab47bc"),
          complete ? QStringLiteral("#1e6f46") : QStringLiteral("#007acc"),
          complete ? QStringLiteral("#258b58") : QStringLiteral("#0098ff"),
          complete ? QStringLiteral("#175637") : QStringLiteral("#005999")));
}

void PositionChangeDialog::setupUi(const QList<int> &measuredIndices,
                                   const QList<int> &remainingIndices,
                                   Mode mode)
{
    setObjectName(QStringLiteral("PositionChangeDialog"));

    const bool complete = (mode == Mode::SequenceComplete);
    const int totalSteps = corePositionSequenceLength();
    const int measuredCount = measuredIndices.size();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *headerWidget = new QWidget(this);
    headerWidget->setObjectName(QStringLiteral("HeaderWidget"));
    headerWidget->setFixedHeight(50);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto *titleIcon = new QLabel(complete ? QStringLiteral("\u2713") : QStringLiteral("\u27F3"), this);
    titleIcon->setObjectName(QStringLiteral("TitleIcon"));
    auto *titleLabel = new QLabel(complete ? QStringLiteral("SEQUENCE COMPLETE")
                                           : QStringLiteral("CHANGE POSITION"),
                                  this);
    titleLabel->setObjectName(QStringLiteral("TitleLabel"));
    auto *stepLabel = new QLabel(QStringLiteral("%1 / %2 measured").arg(measuredCount).arg(totalSteps), this);
    stepLabel->setObjectName(QStringLiteral("StepLabel"));

    headerLayout->addWidget(titleIcon);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(stepLabel);

    mainLayout->addWidget(headerWidget);

    auto *bodyWidget = new QWidget(this);
    bodyWidget->setObjectName(QStringLiteral("BodyWidget"));
    auto *bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(24, 18, 24, 20);
    bodyLayout->setSpacing(16);

    auto *progressRow = new QHBoxLayout();
    progressRow->setSpacing(12);

    auto *progressLayout = new QHBoxLayout();
    progressLayout->setSpacing(6);
    for (int i = 0; i < totalSteps; ++i) {
        auto *segment = new QFrame(this);
        segment->setFixedHeight(4);
        segment->setFixedWidth(56);
        const bool active = complete ? true : (i < measuredCount);
        segment->setObjectName(active ? QStringLiteral("SegmentActive") : QStringLiteral("SegmentInactive"));
        progressLayout->addWidget(segment);
    }
    progressRow->addLayout(progressLayout, 1);

    auto *countLabel = new QLabel(QStringLiteral("%1 / %2 measured").arg(measuredCount).arg(totalSteps), this);
    countLabel->setObjectName(QStringLiteral("CountLabel"));
    countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(countLabel);

    bodyLayout->addLayout(progressRow);

    if (complete) {
        bodyLayout->addWidget(
            createListPanel(QStringLiteral("ALL POSITIONS MEASURED (%1)").arg(measuredIndices.size()),
                            measuredIndices,
                            true,
                            this),
            1);
    } else {
        auto *listsLayout = new QHBoxLayout();
        listsLayout->setSpacing(12);

        listsLayout->addWidget(
            createListPanel(QStringLiteral("MEASURED (%1)").arg(measuredIndices.size()),
                            measuredIndices,
                            true,
                            this),
            1);
        listsLayout->addWidget(
            createListPanel(QStringLiteral("REMAINING (%1)").arg(remainingIndices.size()),
                            remainingIndices,
                            false,
                            this),
            1);

        bodyLayout->addLayout(listsLayout, 1);
    }

    auto *hintLabel = new QLabel(complete
        ? QStringLiteral("All core positions in the sequence are done.")
        : QStringLiteral("Rotate the watch to any remaining position, then confirm."),
        this);
    hintLabel->setObjectName(QStringLiteral("HintLabel"));
    hintLabel->setWordWrap(true);
    hintLabel->setAlignment(Qt::AlignCenter);
    bodyLayout->addWidget(hintLabel);

    auto *confirmButton = new QPushButton(complete
        ? QStringLiteral("\u2713 Close")
        : QStringLiteral("\u2713 Confirm \u2013 Continue to next position"),
        this);
    confirmButton->setObjectName(QStringLiteral("ConfirmButton"));
    confirmButton->setFixedHeight(48);
    confirmButton->setDefault(true);
    confirmButton->setAutoDefault(true);
    connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    bodyLayout->addWidget(confirmButton);

    mainLayout->addWidget(bodyWidget);

    applyStyleSheet(mode);
    confirmButton->setFocus();
}
