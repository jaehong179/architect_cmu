#ifndef POSITIONCHANGEDIALOG_H
#define POSITIONCHANGEDIALOG_H

#include <QDialog>

class PositionChangeDialog : public QDialog
{
    Q_OBJECT
public:
    PositionChangeDialog(const QString &completedName, const QString &nextName, int completedIndex, QWidget *parent = nullptr);

private:
    void setupUi(const QString &completedName, const QString &nextName, int completedIndex);
    QString getShortCode(const QString &fullName) const;
    QString getKoreanName(const QString &fullName) const;
};

#endif // POSITIONCHANGEDIALOG_H
