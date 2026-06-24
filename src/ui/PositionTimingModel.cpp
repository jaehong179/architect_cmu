#include "PositionTimingModel.h"
#include "PositionNames.h"

PositionTimingModel::PositionTimingModel(QObject *parent)
    : QAbstractListModel(parent)
{
    resetToDefaults();
}

int PositionTimingModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mEntries.size();
}

QVariant PositionTimingModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= mEntries.size())
        return {};
    const auto &e = mEntries.at(index.row());
    switch (role) {
    case NameRole:              return e.name;
    case StabilizationSecRole:  return e.stabilizationSec;
    case MeasurementSecRole:    return e.measurementSec;
    default:                    return {};
    }
}

bool PositionTimingModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= mEntries.size())
        return false;
    auto &e = mEntries[index.row()];
    switch (role) {
    case StabilizationSecRole:
        e.stabilizationSec = qMax(0, value.toInt());
        emit dataChanged(index, index, {StabilizationSecRole});
        return true;
    case MeasurementSecRole:
        e.measurementSec = qMax(1, value.toInt());
        emit dataChanged(index, index, {MeasurementSecRole});
        return true;
    default:
        return false;
    }
}

QHash<int, QByteArray> PositionTimingModel::roleNames() const
{
    return {
        {NameRole,             "name"},
        {StabilizationSecRole, "stabilizationSec"},
        {MeasurementSecRole,   "measurementSec"}
    };
}

const PositionTimingEntry &PositionTimingModel::entryAt(int row) const
{
    return mEntries.at(row);
}

int PositionTimingModel::stabilizationSecAt(int row) const
{
    return mEntries.at(row).stabilizationSec;
}

int PositionTimingModel::measurementSecAt(int row) const
{
    return mEntries.at(row).measurementSec;
}

void PositionTimingModel::setStabilizationSec(int row, int sec)
{
    setData(index(row, 0), qMax(0, sec), StabilizationSecRole);
}

void PositionTimingModel::setMeasurementSec(int row, int sec)
{
    setData(index(row, 0), qMax(1, sec), MeasurementSecRole);
}

void PositionTimingModel::resetToDefaults()
{
    beginResetModel();
    mEntries.clear();
    for (const QString &name : standardPositionNames()) {
        PositionTimingEntry e;
        e.name = name;
        e.stabilizationSec = defaultStabilizationSec();
        e.measurementSec   = defaultMeasurementSec();
        mEntries.push_back(e);
    }
    endResetModel();
}
