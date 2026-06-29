#ifndef POSITIONCHANGEDIALOG_H
#define POSITIONCHANGEDIALOG_H

#include <QDialog>
#include <QList>

class QWidget;

class PositionChangeDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode {
        ChangePosition,
        SequenceComplete
    };

    PositionChangeDialog(const QList<int> &measuredIndices,
                         const QList<int> &remainingIndices,
                         Mode mode = Mode::ChangePosition,
                         QWidget *parent = nullptr);

private:
    void setupUi(const QList<int> &measuredIndices,
                 const QList<int> &remainingIndices,
                 Mode mode);
    QWidget *createPositionRow(const QString &name, bool measured, QWidget *parent) const;
    QWidget *createListPanel(const QString &title, const QList<int> &positionIndices, bool measured, QWidget *parent) const;
    void applyStyleSheet(Mode mode);
    QString getShortCode(const QString &fullName) const;
    QString getDisplayName(const QString &fullName) const;
};

#endif // POSITIONCHANGEDIALOG_H
