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

// API v1.6 position codes (DU/DD/CU/CD/CL/CR) — see documents/timegrapher_api_spec_en.md
inline QString toApiPositionCode(const QString &internalKey)
{
    const QString k = internalKey.trimmed();
    if (k == QStringLiteral("CH"))  return QStringLiteral("DU");
    if (k == QStringLiteral("CB"))  return QStringLiteral("DD");
    if (k == QStringLiteral("9H"))  return QStringLiteral("CR");
    if (k == QStringLiteral("6H"))  return QStringLiteral("CL");
    if (k == QStringLiteral("3H"))  return QStringLiteral("CU");
    if (k == QStringLiteral("12H")) return QStringLiteral("CD");
    return QString();
}

inline QString internalKeyForCoreRow(int row)
{
    switch (row) {
    case 0: return QStringLiteral("CH");
    case 1: return QStringLiteral("CB");
    case 2: return QStringLiteral("9H");
    case 3: return QStringLiteral("6H");
    case 4: return QStringLiteral("3H");
    case 5: return QStringLiteral("12H");
    default: return QString();
    }
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

inline int defaultStabilizationSec() { return 10; }
inline int defaultMeasurementSec()    { return 10; }

#endif // POSITIONNAMES_H
