#ifndef POSITIONSEQUENCECONTROLLER_H
#define POSITIONSEQUENCECONTROLLER_H

#include <QObject>

class PositionTimingModel;
class QTimer;

class PositionSequenceController : public QObject
{
    Q_OBJECT
public:
    enum class Phase { Idle, Stabilizing, Measuring };

    explicit PositionSequenceController(QObject *parent = nullptr);

    void setTimingModel(PositionTimingModel *model);

    int currentPositionIndex() const { return mCurrentPositionIndex; }
    Phase phase() const { return mPhase; }

public slots:
    void start();
    void stop();
    void confirmPositionChange();

signals:
    void phaseChanged(const QString &positionName, const QString &phaseLabel, int remainingSec);
    void measurementWindowEnded(int positionIndex, const QString &positionName,
                                const QString &nextPositionName, bool sequenceComplete);
    void currentPositionIndexChanged(int index);

private slots:
    void tick();

private:
    void beginStabilizing();
    void beginMeasuring();
    void finishMeasurementWindow();
    int currentTimingRow() const;

    PositionTimingModel *mTiming = nullptr;
    QTimer              *mTimer  = nullptr;
    Phase                mPhase    = Phase::Idle;
    int                  mSequenceStep = 0;   // 0..5 in core sequence
    int                  mCurrentPositionIndex = 0;
    int                  mRemainingSec = 0;
    bool                 mAwaitingConfirm = false;
};

#endif // POSITIONSEQUENCECONTROLLER_H
