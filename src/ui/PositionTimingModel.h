#ifndef POSITIONTIMINGMODEL_H
#define POSITIONTIMINGMODEL_H

#include <QAbstractListModel>

struct PositionTimingEntry {
    QString name;
    int stabilizationSec = 15;
    int measurementSec   = 60;
};

class PositionTimingModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        StabilizationSecRole,
        MeasurementSecRole
    };

    explicit PositionTimingModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QHash<int, QByteArray> roleNames() const override;

    const PositionTimingEntry &entryAt(int row) const;
    int stabilizationSecAt(int row) const;
    int measurementSecAt(int row) const;

    Q_INVOKABLE void setStabilizationSec(int row, int sec);
    Q_INVOKABLE void setMeasurementSec(int row, int sec);
    Q_INVOKABLE void resetToDefaults();

private:
    QVector<PositionTimingEntry> mEntries;
};

#endif // POSITIONTIMINGMODEL_H
