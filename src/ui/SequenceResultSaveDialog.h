#ifndef SEQUENCERESULTSAVEDIALOG_H
#define SEQUENCERESULTSAVEDIALOG_H

#include <QDialog>

class QLabel;

class SequenceResultSaveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SequenceResultSaveDialog(const QString &watchId,
                                      const QString &engineer,
                                      QWidget *parent = nullptr);

private:
    QLabel *mWatchIdLabel = nullptr;
    QLabel *mEngineerLabel = nullptr;
};

#endif // SEQUENCERESULTSAVEDIALOG_H
