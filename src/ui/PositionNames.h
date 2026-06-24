#ifndef POSITIONNAMES_H
#define POSITIONNAMES_H

#include <QString>
#include <QStringList>

// Chronoscope X1 G3 / NIHS 95-10 표준 10 포지션 (TabSequenceDisplay·타이밍 설정 공통).
inline QStringList standardPositionNames()
{
    return {
        QStringLiteral("CH (dial up)"),
        QStringLiteral("CB (dial down)"),
        QStringLiteral("9H"),
        QStringLiteral("6H"),
        QStringLiteral("3H"),
        QStringLiteral("12H"),
        QStringLiteral("10H30 (int.)"),
        QStringLiteral("7H30 (int.)"),
        QStringLiteral("4H30 (int.)"),
        QStringLiteral("1H30 (int.)")
    };
}

// ADR-004: 6개 핵심 포지션 측정 시퀀스 (CH→CB→9H→6H→3H→12H).
inline const int *corePositionSequenceIndices()
{
    static const int seq[] = {0, 1, 2, 3, 4, 5};
    return seq;
}

inline int corePositionSequenceLength()
{
    return 6;
}

inline int defaultStabilizationSec() { return 15; }
inline int defaultMeasurementSec()    { return 60; }

#endif // POSITIONNAMES_H
