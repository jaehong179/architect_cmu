#include "SequenceResultSaveDialog.h"

#include <QDateTime>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

SequenceResultSaveDialog::SequenceResultSaveDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Save Multi-Position Result"));
    setModal(true);
    resize(560, 180);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Save completed 6-position measurement"), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600; color: #e6e6e6;"));
    mainLayout->addWidget(title);

    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);

    mPathEdit = new QLineEdit(this);
    mPathEdit->setPlaceholderText(QStringLiteral("Select file path..."));

    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath();
    }
    const QString fileName = QStringLiteral("timegrapher_sequence_%1.csv")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    mPathEdit->setText(baseDir + QLatin1Char('/') + fileName);

    auto *browse = new QPushButton(QStringLiteral("Browse"), this);
    connect(browse, &QPushButton::clicked, this, &SequenceResultSaveDialog::browsePath);

    pathRow->addWidget(mPathEdit, 1);
    pathRow->addWidget(browse);
    mainLayout->addLayout(pathRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #181818; color: #f4f4f4; }"
        "QLineEdit { background-color: #222; color: #fff; border: 1px solid #444; padding: 6px; border-radius: 4px; }"
        "QPushButton { background-color: #2f2f2f; color: #fff; border: 1px solid #4a4a4a; padding: 6px 10px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a3a3a; }"
        "QPushButton:pressed { background-color: #252525; }"
    ));
}

QString SequenceResultSaveDialog::selectedPath() const
{
    return mPathEdit ? mPathEdit->text().trimmed() : QString();
}

void SequenceResultSaveDialog::browsePath()
{
    if (!mPathEdit) return;

    const QString current = mPathEdit->text().trimmed();
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save sequence result"),
        current,
        QStringLiteral("CSV Files (*.csv);;All Files (*)"));

    if (!path.isEmpty()) {
        mPathEdit->setText(path);
    }
}
