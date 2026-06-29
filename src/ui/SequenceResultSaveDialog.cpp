#include "SequenceResultSaveDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SequenceResultSaveDialog::SequenceResultSaveDialog(const QString &watchId,
                                                   const QString &engineer,
                                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Upload Multi-Position Result"));
    setModal(true);
    resize(480, 200);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Upload completed 6-position measurement"), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600; color: #e6e6e6;"));
    mainLayout->addWidget(title);

    auto *watchRow = new QHBoxLayout();
    watchRow->setSpacing(8);
    auto *watchCaption = new QLabel(QStringLiteral("Watch ID:"), this);
    watchCaption->setStyleSheet(QStringLiteral("color: #aaaaaa;"));
    mWatchIdLabel = new QLabel(watchId.trimmed(), this);
    mWatchIdLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: #ffffff;"));
    watchRow->addWidget(watchCaption);
    watchRow->addWidget(mWatchIdLabel, 1);
    mainLayout->addLayout(watchRow);

    auto *engineerRow = new QHBoxLayout();
    engineerRow->setSpacing(8);
    auto *engineerCaption = new QLabel(QStringLiteral("Engineer:"), this);
    engineerCaption->setStyleSheet(QStringLiteral("color: #aaaaaa;"));
    mEngineerLabel = new QLabel(engineer.trimmed(), this);
    mEngineerLabel->setStyleSheet(QStringLiteral("font-weight: 600; color: #ffffff;"));
    engineerRow->addWidget(engineerCaption);
    engineerRow->addWidget(mEngineerLabel, 1);
    mainLayout->addLayout(engineerRow);

    auto *uploadNote = new QLabel(QStringLiteral("Results will be uploaded to the cloud."), this);
    uploadNote->setStyleSheet(QStringLiteral("color: #9e9e9e; font-size: 11px;"));
    mainLayout->addWidget(uploadNote);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    if (QPushButton *uploadBtn = buttons->button(QDialogButtonBox::Save)) {
        uploadBtn->setText(QStringLiteral("Upload to Cloud"));
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #181818; color: #f4f4f4; }"
        "QPushButton { background-color: #2f2f2f; color: #fff; border: 1px solid #4a4a4a; padding: 6px 10px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a3a3a; }"
        "QPushButton:pressed { background-color: #252525; }"
    ));
}
