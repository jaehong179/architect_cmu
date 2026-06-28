#include "PositionChangeDialog.h"
#include "PositionNames.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

PositionChangeDialog::PositionChangeDialog(const QString &completedName, const QString &nextName, int completedIndex, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    // Disable semi-transparent background if desired, or keep it solid
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedSize(500, 320);
    setupUi(completedName, nextName, completedIndex);
}

QString PositionChangeDialog::getShortCode(const QString &fullName) const
{
    const QString key = canonicalCorePositionKey(fullName);
    if (!key.isEmpty()) return key;
    if (fullName.contains(QLatin1Char(' '))) return fullName.split(QLatin1Char(' ')).first();
    return fullName;
}

QString PositionChangeDialog::getKoreanName(const QString &fullName) const
{
    const QString key = canonicalCorePositionKey(fullName);
    if (key == QStringLiteral("CH")) return QStringLiteral("윗면");
    if (key == QStringLiteral("CB")) return QStringLiteral("아랫면");
    if (key == QStringLiteral("9H")) return QStringLiteral("9시 위");
    if (key == QStringLiteral("6H")) return QStringLiteral("6시 위");
    if (key == QStringLiteral("3H")) return QStringLiteral("3시 위");
    if (key == QStringLiteral("12H")) return QStringLiteral("12시 위");
    return fullName;
}

void PositionChangeDialog::setupUi(const QString &completedName, const QString &nextName, int completedIndex)
{
    setObjectName(QStringLiteral("PositionChangeDialog"));
    Q_UNUSED(nextName);

    // Main layout
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Header (Title bar)
    auto *headerWidget = new QWidget(this);
    headerWidget->setObjectName(QStringLiteral("HeaderWidget"));
    headerWidget->setFixedHeight(50);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto *titleIcon = new QLabel(QStringLiteral("⟳"), this);
    titleIcon->setObjectName(QStringLiteral("TitleIcon"));
    auto *titleLabel = new QLabel(QStringLiteral("CHANGE POSITION"), this);
    titleLabel->setObjectName(QStringLiteral("TitleLabel"));

    headerLayout->addWidget(titleIcon);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    mainLayout->addWidget(headerWidget);

    // 2. Body Area (Transition details)
    auto *bodyWidget = new QWidget(this);
    bodyWidget->setObjectName(QStringLiteral("BodyWidget"));
    auto *bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(30, 25, 30, 25);
    bodyLayout->setSpacing(25);

    // Large horizontal codes
    auto *codeLayout = new QHBoxLayout();
    codeLayout->setSpacing(20);
    codeLayout->setAlignment(Qt::AlignCenter);

    // Left position (Completed)
    auto *leftCol = new QVBoxLayout();
    leftCol->setAlignment(Qt::AlignCenter);
    auto *leftCode = new QLabel(getShortCode(completedName), this);
    leftCode->setObjectName(QStringLiteral("CompletedCode"));
    leftCode->setAlignment(Qt::AlignCenter);
    auto *leftStatus = new QLabel(QStringLiteral("완료"), this);
    leftStatus->setObjectName(QStringLiteral("CompletedStatus"));
    leftStatus->setAlignment(Qt::AlignCenter);
    leftCol->addWidget(leftCode);
    leftCol->addWidget(leftStatus);

    // Arrow in-between
    auto *arrowLabel = new QLabel(QStringLiteral("→"), this);
    arrowLabel->setObjectName(QStringLiteral("ArrowIcon"));
    arrowLabel->setAlignment(Qt::AlignCenter);

    // Right position (generic instruction)
    auto *rightCol = new QVBoxLayout();
    rightCol->setAlignment(Qt::AlignCenter);
    auto *rightCode = new QLabel(QStringLiteral("OTHER"), this);
    rightCode->setObjectName(QStringLiteral("NextCode"));
    rightCode->setAlignment(Qt::AlignCenter);
    auto *rightDesc = new QLabel(QStringLiteral("다른 위치로 변경"), this);
    rightDesc->setObjectName(QStringLiteral("NextDesc"));
    rightDesc->setAlignment(Qt::AlignCenter);
    rightCol->addWidget(rightCode);
    rightCol->addWidget(rightDesc);

    codeLayout->addLayout(leftCol);
    codeLayout->addWidget(arrowLabel);
    codeLayout->addLayout(rightCol);
    bodyLayout->addLayout(codeLayout);

    // 3. Progress Segments (6 horizontal lines)
    auto *progressLayout = new QHBoxLayout();
    progressLayout->setSpacing(8);
    progressLayout->setAlignment(Qt::AlignCenter);

    int numCompleted = completedIndex + 1; // e.g., index 1 (CB) done means 2 steps completed
    for (int i = 0; i < 6; ++i) {
        auto *segment = new QFrame(this);
        segment->setFixedHeight(4);
        segment->setFixedWidth(60);
        if (i < numCompleted) {
            segment->setObjectName(QStringLiteral("SegmentActive"));
        } else {
            segment->setObjectName(QStringLiteral("SegmentInactive"));
        }
        progressLayout->addWidget(segment);
    }
    bodyLayout->addLayout(progressLayout);

    // 4. Confirm Button
    auto *confirmButton = new QPushButton(QStringLiteral("✓ 확인  –  다른 위치로 변경"), this);
    confirmButton->setObjectName(QStringLiteral("ConfirmButton"));
    confirmButton->setFixedHeight(48);
    connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    bodyLayout->addWidget(confirmButton);

    mainLayout->addWidget(bodyWidget);

    // Style Sheets (QSS)
    setStyleSheet(QStringLiteral(
        "QDialog#PositionChangeDialog {"
        "  background-color: #0d0b0a;"
        "  border: 1px solid #33261a;"
        "}"
        "QWidget#HeaderWidget {"
        "  background-color: #1e1714;"
        "  border-bottom: 1px solid #281e16;"
        "}"
        "QLabel#TitleIcon {"
        "  color: #f78f1e;"
        "  font-size: 18px;"
        "  font-weight: bold;"
        "  margin-right: 5px;"
        "}"
        "QLabel#TitleLabel {"
        "  color: #f78f1e;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  font-family: 'Outfit', 'Inter', 'Segoe UI', sans-serif;"
        "}"
        "QWidget#BodyWidget {"
        "  background-color: #0d0b0a;"
        "}"
        "QLabel#CompletedCode {"
        "  color: #5b4437;"
        "  font-size: 38px;"
        "  font-weight: bold;"
        "  font-family: 'Outfit', 'Inter', 'Segoe UI', sans-serif;"
        "}"
        "QLabel#CompletedStatus {"
        "  color: #4a382e;"
        "  font-size: 11px;"
        "}"
        "QLabel#ArrowIcon {"
        "  color: #f78f1e;"
        "  font-size: 28px;"
        "  margin-left: 10px;"
        "  margin-right: 10px;"
        "}"
        "QLabel#NextCode {"
        "  color: #f78f1e;"
        "  font-size: 38px;"
        "  font-weight: bold;"
        "  font-family: 'Outfit', 'Inter', 'Segoe UI', sans-serif;"
        "}"
        "QLabel#NextDesc {"
        "  color: #a3958d;"
        "  font-size: 11px;"
        "}"
        "QFrame#SegmentActive {"
        "  background-color: #f78f1e;"
        "  border: none;"
        "}"
        "QFrame#SegmentInactive {"
        "  background-color: #261f1b;"
        "  border: none;"
        "}"
        "QPushButton#ConfirmButton {"
        "  color: #f78f1e;"
        "  border: 1px solid #543f32;"
        "  border-radius: 4px;"
        "  background-color: #17120f;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  font-family: 'Segoe UI', sans-serif;"
        "}"
        "QPushButton#ConfirmButton:hover {"
        "  background-color: #241a15;"
        "  border-color: #f78f1e;"
        "}"
        "QPushButton#ConfirmButton:pressed {"
        "  background-color: #110c0a;"
        "}"
    ));
}
