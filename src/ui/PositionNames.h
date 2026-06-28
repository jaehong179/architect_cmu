#ifndef POSITIONNAMES_H
#define POSITIONNAMES_H

#include <QString>
#include <QStringList>

// Chronoscope X1 G3 / NIHS 95-10 표준 10 포지션 (TabSequenceDisplay·타이밍 설정 공통).
inline QStringList standardPositionNames()
{
    return {
        QStringLiteral("Dial Up"),
        QStringLiteral("Dial Down"),
        QStringLiteral("Crown Right"),
        QStringLiteral("Crown Left"),
        QStringLiteral("Crown Up"),
        QStringLiteral("Crown Down")
        // QStringLiteral("10H30 (int.)"),
        // QStringLiteral("7H30 (int.)"),
        // QStringLiteral("4H30 (int.)"),
        // QStringLiteral("1H30 (int.)")
    };
}

inline QString canonicalCorePositionKey(const QString &name)
{
    const QString n = name.trimmed();
    if (n.startsWith(QStringLiteral("CH"), Qt::CaseInsensitive) || n.compare(QStringLiteral("Dial Up"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("CH");
    if (n.startsWith(QStringLiteral("CB"), Qt::CaseInsensitive) || n.compare(QStringLiteral("Dial Down"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("CB");
    if (n.startsWith(QStringLiteral("9H"), Qt::CaseInsensitive) || n.compare(QStringLiteral("Crown Right"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("9H");
    if (n.startsWith(QStringLiteral("6H"), Qt::CaseInsensitive) || n.compare(QStringLiteral("Crown Left"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("6H");
    if (n.startsWith(QStringLiteral("3H"), Qt::CaseInsensitive) || n.compare(QStringLiteral("Crown Up"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("3H");
    if (n.startsWith(QStringLiteral("12H"), Qt::CaseInsensitive) || n.compare(QStringLiteral("Crown Down"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("12H");
    return QString();
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
