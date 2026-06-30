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
    // [측정 대기] 고정 10초 안정화 대신 warm-up 을 요청한다.
    //  warm-up 이 끝나면 MainWindow 가 beginMeasuringNow() 를 호출해 측정 시작.
    mPhase = Phase::Warmup;
    mTimer->start();
    emit warmupRequested(true);
}

void PositionSequenceController::stop()
{
    mTimer->stop();
    mPhase = Phase::Idle;
    mAwaitingConfirm = false;
    mRemainingSec = 0;
    emit phaseChanged(QString(), QStringLiteral("idle"), 0);
}

void PositionSequenceController::pause()
{
    if (mPhase == Phase::Idle || mAwaitingConfirm)
        return;
    mTimer->stop();
}

void PositionSequenceController::resume()
{
    if (mPhase == Phase::Idle || mAwaitingConfirm)
        return;
    if (!mTimer->isActive())
        mTimer->start();
}

void PositionSequenceController::confirmPositionChange(int measuredPositionCount,
                                                       int nextPositionIndex)
{
    if (!mAwaitingConfirm) return;
    mAwaitingConfirm = false;

    const int total = corePositionSequenceLength();
    mSequenceStep = qBound(0, measuredPositionCount, total);

    if (measuredPositionCount >= total || nextPositionIndex < 0) {
        mTimer->stop();
        mPhase = Phase::Idle;
        mRemainingSec = 0;
        emit phaseChanged(QString(), QStringLiteral("idle"), 0);
        return;
    }

    mCurrentPositionIndex = nextPositionIndex;
    emit currentPositionIndexChanged(mCurrentPositionIndex);
    // [측정 대기] 포지션 전환 시에도 고정 안정화 대신 warm-up 을 요청한다.
    mPhase = Phase::Warmup;
    if (!mTimer->isActive())
        mTimer->start();
    emit warmupRequested(false);
}

void PositionSequenceController::beginMeasuringNow()
{
    if (!mTiming) return;
    // [측정 대기] warm-up 종료 후 실제 측정 카운트다운 시작.
    beginMeasuring();
    if (!mTimer->isActive())
        mTimer->start();
}

int PositionSequenceController::currentTimingRow() const
{
    return mCurrentPositionIndex;
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

    emit measurementWindowEnded(mCurrentPositionIndex, posName, QString(), false);
}

void PositionSequenceController::tick()
{
    if (mAwaitingConfirm || !mTiming)
        return;

    // [측정 대기] warm-up 구간은 MainWindow 의 오버레이 카운트다운이 담당.
    //  컨트롤러는 'measuring' 구간만 카운트다운한다.
    if (mPhase != Phase::Measuring)
        return;

    if (mRemainingSec > 0)
        --mRemainingSec;

    const QString name = mTiming->entryAt(currentTimingRow()).name;

    if (mRemainingSec > 0) {
        emit phaseChanged(name, QStringLiteral("measuring"), mRemainingSec);
        return;
    }

    finishMeasurementWindow();
}
