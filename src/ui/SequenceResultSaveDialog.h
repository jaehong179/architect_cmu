#ifndef SEQUENCERESULTSAVEDIALOG_H
#define SEQUENCERESULTSAVEDIALOG_H

#include <QDialog>

class QLineEdit;

class SequenceResultSaveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SequenceResultSaveDialog(QWidget *parent = nullptr);

    QString selectedPath() const;

private slots:
    void browsePath();

private:
    QLineEdit *mPathEdit = nullptr;
};

#endif // SEQUENCERESULTSAVEDIALOG_H
