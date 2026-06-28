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
            mCell[i]->setStyleSheet(QStringLiteral("background:#252530; border:1px solid #3a3a4a; border-radius:5px; padding:3px;"));
            mCell[i]->setMinimumWidth(120);
            lay->addWidget(mCell[i], 1);
        }
        update(MeasurementSnapshot{});
    }

    void update(const MeasurementSnapshot &s)
    {
        // 정상 범위 판정: 정상이면 수치를 green(#2ed573), 벗어나면 red(#ff4757),
        //  측정 전(invalid)이면 중립 gray(#9e9e9e) 로 표시한다. (✓/✗ 마커는 사용 안 함)
        static const QString kGreen = QStringLiteral("#2ed573");
        static const QString kRed   = QStringLiteral("#ff4757");
        static const QString kGray  = QStringLiteral("#9e9e9e");

        const bool rateOk = s.rateValid      && std::fabs(s.rate) <= 3.0;
        const bool beOk   = s.beatErrorValid && s.beatErrorMs <= 0.5;
        const bool ampOk  = s.amplitudeValid && s.amplitudeDeg >= 270.0 && s.amplitudeDeg <= 300.0;

        const QString rateColor = s.rateValid      ? (rateOk ? kGreen : kRed) : kGray;
        const QString beColor   = s.beatErrorValid ? (beOk   ? kGreen : kRed) : kGray;
        const QString ampColor  = s.amplitudeValid ? (ampOk  ? kGreen : kRed) : kGray;
        const QString bphColor  = s.bphValid ? kGreen : kGray;

        // 사양 readout 순서: RATE | BEAT ERROR | AMPLITUDE | BPH (Witschi Trace/Vario/Sequence 화면).
        set(0, QStringLiteral("RATE  s/d"),       s.rateValid      ? QString::asprintf("%+.1f", s.rate)       : QStringLiteral("--"),    rateColor, QString());
        set(1, QStringLiteral("BEAT ERROR  ms"),  s.beatErrorValid ? QString::number(s.beatErrorMs, 'f', 2)   : QStringLiteral("--"),    beColor,   QString());
        set(2, QStringLiteral("AMPLITUDE  °"),    s.amplitudeValid ? QString::number(s.amplitudeDeg, 'f', 0)  : QStringLiteral("--"),    ampColor,  QString());
        set(3, QStringLiteral("BPH"),             s.bphValid       ? QString::number(s.bph)                   : QStringLiteral("-----"), bphColor,  QString());
    }

private:
    QLabel *mCell[4] = {nullptr, nullptr, nullptr, nullptr};
    void set(int i, const QString &title, const QString &val, const QString &color, const QString &symbol)
    {
        mCell[i]->setText(QString("<div style='font-size:10px;color:#9e9e9e'>%1</div>"
                                  "<div style='font-size:20px;font-weight:bold;color:%2'>%3%4</div>")
                              .arg(title, color, val, symbol));
    }
};

#endif // READOUTBAR_H
