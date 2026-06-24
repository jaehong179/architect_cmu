#include "PositionSequenceController.h"
#include "PositionTimingModel.h"
#include "PositionNames.h"
#include <QTimer>

PositionSequenceController::PositionSequenceController(QObject *parent)
    : QObject(parent)
{
    mTimer = new QTimer(this);
    mTimer->setInterval(1000);
    connect(mTimer, &QTimer::timeout, this, &PositionSequenceController::tick);
}

void PositionSequenceController::setTimingModel(PositionTimingModel *model)
{
    mTiming = model;
}

void PositionSequenceController::start()
{
    if (!mTiming) return;
    mAwaitingConfirm = false;
    mSequenceStep = 0;
    mCurrentPositionIndex = corePositionSequenceIndices()[0];
    emit currentPositionIndexChanged(mCurrentPositionIndex);
    beginStabilizing();
    mTimer->start();
}

void PositionSequenceController::stop()
{
    mTimer->stop();
    mPhase = Phase::Idle;
    mAwaitingConfirm = false;
    mRemainingSec = 0;
    emit phaseChanged(QString(), QStringLiteral("idle"), 0);
}

void PositionSequenceController::confirmPositionChange()
{
    if (!mAwaitingConfirm) return;
    mAwaitingConfirm = false;

    if (mSequenceStep >= corePositionSequenceLength() - 1)
        return;

    ++mSequenceStep;
    mCurrentPositionIndex = corePositionSequenceIndices()[mSequenceStep];
    emit currentPositionIndexChanged(mCurrentPositionIndex);
    beginStabilizing();
    if (!mTimer->isActive())
        mTimer->start();
}

int PositionSequenceController::currentTimingRow() const
{
    return mCurrentPositionIndex;
}

void PositionSequenceController::beginStabilizing()
{
    if (!mTiming) return;
    mPhase = Phase::Stabilizing;
    mRemainingSec = mTiming->stabilizationSecAt(currentTimingRow());
    const QString name = mTiming->entryAt(currentTimingRow()).name;
    emit phaseChanged(name, QStringLiteral("stabilizing"), mRemainingSec);
}

void PositionSequenceController::beginMeasuring()
{
    if (!mTiming) return;
    mPhase = Phase::Measuring;
    mRemainingSec = mTiming->measurementSecAt(currentTimingRow());
    const QString name = mTiming->entryAt(currentTimingRow()).name;
    emit phaseChanged(name, QStringLiteral("measuring"), mRemainingSec);
}

void PositionSequenceController::finishMeasurementWindow()
{
    mTimer->stop();
    mAwaitingConfirm = true;

    const QString posName = mTiming->entryAt(currentTimingRow()).name;
    const bool complete = (mSequenceStep >= corePositionSequenceLength() - 1);
    QString nextName;
    if (!complete)
        nextName = mTiming->entryAt(corePositionSequenceIndices()[mSequenceStep + 1]).name;

    emit measurementWindowEnded(mCurrentPositionIndex, posName, nextName, complete);
}

void PositionSequenceController::tick()
{
    if (mAwaitingConfirm || !mTiming)
        return;

    if (mRemainingSec > 0)
        --mRemainingSec;

    const QString name = mTiming->entryAt(currentTimingRow()).name;
    const QString phaseLabel = (mPhase == Phase::Stabilizing)
        ? QStringLiteral("stabilizing") : QStringLiteral("measuring");

    if (mRemainingSec > 0) {
        emit phaseChanged(name, phaseLabel, mRemainingSec);
        return;
    }

    if (mPhase == Phase::Stabilizing) {
        beginMeasuring();
    } else if (mPhase == Phase::Measuring) {
        finishMeasurementWindow();
    }
}
