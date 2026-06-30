#ifndef POSITIONSEQUENCECONTROLLER_H
#define POSITIONSEQUENCECONTROLLER_H

#include <QObject>

class PositionTimingModel;
class QTimer;

class PositionSequenceController : public QObject
{
    Q_OBJECT
public:
    enum class Phase { Idle, Warmup, Measuring };

    explicit PositionSequenceController(QObject *parent = nullptr);

    void setTimingModel(PositionTimingModel *model);

    int currentPositionIndex() const { return mCurrentPositionIndex; }
    Phase phase() const { return mPhase; }

public slots:
    void start();
    void stop();
    void pause();
    void resume();
    void confirmPositionChange(int measuredPositionCount, int nextPositionIndex);
    // [측정 대기] warm-up 종료 후 MainWindow 가 호출 → 실제 측정 카운트다운 시작.
    void beginMeasuringNow();

signals:
    void phaseChanged(const QString &positionName, const QString &phaseLabel, int remainingSec);
    void measurementWindowEnded(int positionIndex, const QString &positionName,
                                const QString &nextPositionName, bool sequenceComplete);
    void currentPositionIndexChanged(int index);
    // [측정 대기] 새 포지션이 활성화되어 측정 전 warm-up 이 필요함을 알린다.
    //  firstPosition=true 면 세션 시작(첫 포지션), false 면 포지션 전환.
    void warmupRequested(bool firstPosition);

private slots:
    void tick();

private:
    void beginMeasuring();
    void finishMeasurementWindow();
    int currentTimingRow() const;

    PositionTimingModel *mTiming = nullptr;
    QTimer              *mTimer  = nullptr;
    Phase                mPhase    = Phase::Idle;
    int                  mSequenceStep = 0;   // uniquely measured position count (0..6)
    int                  mCurrentPositionIndex = 0;
    int                  mRemainingSec = 0;
    bool                 mAwaitingConfirm = false;
};

#endif // POSITIONSEQUENCECONTROLLER_H
