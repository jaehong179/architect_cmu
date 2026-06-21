#ifndef READOUTBAR_H
#define READOUTBAR_H
// =============================================================================
//  ReadoutBar — 측정 readout 헤더 (Witschi/Watch-O-Scope 스타일)
// -----------------------------------------------------------------------------
//  RATE(s/d) · AMPLITUDE(°) · BEAT ERROR(ms) · BPH 를 큰 컬러 숫자로 보여주는 상단 바.
//  여러 탭이 동일한 헤더를 공유하도록 하는 재사용 위젯(헤더 온리, Q_OBJECT 불필요).
// =============================================================================

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <cmath>
#include "MeasurementModel.h"

class ReadoutBar : public QWidget
{
public:
    explicit ReadoutBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(4, 2, 4, 2);
        for (int i = 0; i < 4; ++i) {
            mCell[i] = new QLabel(this);
            mCell[i]->setAlignment(Qt::AlignCenter);
            mCell[i]->setStyleSheet(QStringLiteral("background:#eef6ee; border:1px solid #bcd; border-radius:5px; padding:3px;"));
            mCell[i]->setMinimumWidth(120);
            lay->addWidget(mCell[i], 1);
        }
        update(MeasurementSnapshot{});
    }

    void update(const MeasurementSnapshot &s)
    {
        // 정상범위 판정(다른 탭의 밴드 상수와 일치): rate ±7 s/d, amplitude 270~300°, beat error ≤1.0 ms.
        const bool rateOk = s.rateValid      && std::fabs(s.rate) <= 7.0;
        const bool ampOk  = s.amplitudeValid && s.amplitudeDeg >= 270.0 && s.amplitudeDeg <= 300.0;
        const bool beOk   = s.beatErrorValid && s.beatErrorMs <= 0.5;

        QString rateSymbol;
        if (s.rateValid) {
            if (std::fabs(s.rate) > 3.0) {
                rateSymbol = QStringLiteral(" <span style='font-size:16px;color:#d01515'>&#10007;</span>");
            } else if (rateOk) {
                rateSymbol = QStringLiteral(" <span style='font-size:16px;color:#1a9c1a'>&#10003;</span>");
            }
        }

        QString beSymbol;
        if (s.beatErrorValid) {
            if (s.beatErrorMs > 0.5) {
                beSymbol = QStringLiteral(" <span style='font-size:16px;color:#d01515'>&#10007;</span>");
            } else if (beOk) {
                beSymbol = QStringLiteral(" <span style='font-size:16px;color:#1a9c1a'>&#10003;</span>");
            }
        }

        const QString ampSymbol = ampOk ? QStringLiteral(" <span style='font-size:16px;color:#1a9c1a'>&#10003;</span>") : QString();
        const QString bphSymbol = s.bphValid ? QStringLiteral(" <span style='font-size:16px;color:#1a9c1a'>&#10003;</span>") : QString();

        // 사양 readout 순서: RATE | BEAT ERROR | AMPLITUDE | BPH (Witschi Trace/Vario/Sequence 화면).
        set(0, QStringLiteral("RATE  s/d"),       s.rateValid      ? QString::asprintf("%+.1f", s.rate)       : QStringLiteral("--"),    QStringLiteral("#1560d0"), rateSymbol);
        set(1, QStringLiteral("BEAT ERROR  ms"),  s.beatErrorValid ? QString::number(s.beatErrorMs, 'f', 2)   : QStringLiteral("--"),    QStringLiteral("#108040"), beSymbol);
        set(2, QStringLiteral("AMPLITUDE  °"),    s.amplitudeValid ? QString::number(s.amplitudeDeg, 'f', 0)  : QStringLiteral("--"),    QStringLiteral("#108040"), ampSymbol);
        set(3, QStringLiteral("BPH"),             s.bphValid       ? QString::number(s.bph)                   : QStringLiteral("-----"), QStringLiteral("#333333"), bphSymbol);
    }

private:
    QLabel *mCell[4] = {nullptr, nullptr, nullptr, nullptr};
    void set(int i, const QString &title, const QString &val, const QString &color, const QString &symbol)
    {
        mCell[i]->setText(QString("<div style='font-size:10px;color:#666'>%1</div>"
                                  "<div style='font-size:20px;font-weight:bold;color:%2'>%3%4</div>")
                              .arg(title, color, val, symbol));
    }
};

#endif // READOUTBAR_H
