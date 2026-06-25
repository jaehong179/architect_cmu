#ifndef TABSCAFFOLD_H
#define TABSCAFFOLD_H
// =============================================================================
//  TabScaffold.h — 탭 스캐폴드 공통 헬퍼 (인프라, sibling 의존 아님)
// -----------------------------------------------------------------------------
//  각 탭이 동일한 모양의 "제목 + 설명 + 라이브 측정값 + (미구현 안내)" 레이아웃을
//  반복 작성하지 않도록 하는 인라인 헬퍼.  탭끼리 서로 의존하지 않으며(공통 인프라만
//  참조), 본 로직 구현 시 각 탭에서 자유롭게 위젯을 교체/확장하면 된다.
// =============================================================================

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QString>
#include "MeasurementModel.h"

// 표준 플레이스홀더 레이아웃을 self 위에 구성하고, 매 측정마다 갱신할 '상태 라벨'을 돌려준다.
//  title   : 굵은 탭 제목   subtitle: 요구사항 출처/설명   pendingNote: 아직 미구현인 부분 안내
static inline QLabel *tabScaffoldPlaceholder(QWidget *self,
                                             const QString &title,
                                             const QString &subtitle,
                                             const QString &pendingNote)
{
    auto *lay = new QVBoxLayout(self);
    auto *t = new QLabel(QString("<b>%1</b>").arg(title), self);
    auto *s = new QLabel(subtitle, self);
    s->setWordWrap(true);
    s->setStyleSheet("color:#aaaaaa;");
    auto *status = new QLabel("측정 대기 중…", self);
    status->setStyleSheet("font-family:monospace;");
    auto *note = new QLabel(QString("스캐폴드: %1").arg(pendingNote), self);
    note->setWordWrap(true);
    note->setStyleSheet("color:#ff7043; font-style:italic;");
    lay->addWidget(t);
    lay->addWidget(s);
    lay->addWidget(status);
    lay->addStretch(1);
    lay->addWidget(note);
    return status;
}

// 스냅샷을 표준 한 줄 문자열로 포맷(라이브 측정값 표시용 공통 포맷).
static inline QString tabFormatStatus(const MeasurementSnapshot &s)
{
    return QString("rate=%1 s/d   amp=%2°   beatErr=%3 ms   bph=%4   %5")
        .arg(s.rateValid      ? QString::number(s.rate, 'f', 1)        : QStringLiteral("---"))
        .arg(s.amplitudeValid ? QString::number(s.amplitudeDeg, 'f', 0): QStringLiteral("---"))
        .arg(s.beatErrorValid ? QString::number(s.beatErrorMs, 'f', 2) : QStringLiteral("---"))
        .arg(s.bphValid       ? QString::number(s.bph)                 : QStringLiteral("-----"))
        .arg(s.synced ? QStringLiteral("[synced]") : QStringLiteral("[no-sync]"));
}

#endif // TABSCAFFOLD_H
