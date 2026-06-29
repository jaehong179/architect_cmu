#ifndef SEQUENCERESULTSAVEDIALOG_H
#define SEQUENCERESULTSAVEDIALOG_H

#include <QDialog>

class QLineEdit;
class QLabel;

class SequenceResultSaveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SequenceResultSaveDialog(const QString &watchId,
                                      const QString &engineer,
                                      QWidget *parent = nullptr);

    QString selectedPath() const;

private slots:
    void browsePath();

private:
    QString sanitizeWatchIdForFileName(const QString &watchId) const;

    QLabel *mWatchIdLabel = nullptr;
    QLabel *mEngineerLabel = nullptr;
    QLineEdit *mPathEdit = nullptr;
};

#endif // SEQUENCERESULTSAVEDIALOG_H
