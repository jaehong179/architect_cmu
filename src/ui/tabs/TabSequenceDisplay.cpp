#include "TabSequenceDisplay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <algorithm>
#include <cmath>

bool TabSequenceDisplay::isHorizontal(const QString &pos)
{
    // Plan §Test Positions: 수평 = CH(다이얼 업) / CB(다이얼 다운), 나머지는 수직(크라운 방향).
    return pos.startsWith(QStringLiteral("CH")) || pos.startsWith(QStringLiteral("CB"));
}

TabSequenceDisplay::TabSequenceDisplay(QWidget *parent) : TabView(parent)
{
    auto *lay = new QVBoxLayout(this);

    auto *ctl = new QHBoxLayout();
    ctl->addWidget(new QLabel(QStringLiteral("Position:"), this));
    mPos = new QComboBox(this);
    // Plan §Test Positions (Chronoscope X1 G3 / NIHS 95-10): 수평 CH·CB + 수직 9H·6H·3H·12H,
    //  중간(intermediate) 포지션 4개 포함 → 최대 10 포지션 시퀀스.
    mPos->addItems({QStringLiteral("CH (dial up)"), QStringLiteral("CB (dial down)"),
                    QStringLiteral("9H"), QStringLiteral("6H"),
                    QStringLiteral("3H"), QStringLiteral("12H"),
                    QStringLiteral("10H30 (int.)"), QStringLiteral("7H30 (int.)"),
                    QStringLiteral("4H30 (int.)"), QStringLiteral("1H30 (int.)")});
    ctl->addWidget(mPos);
    mCapture = new QPushButton(QStringLiteral("Capture current"), this);
    mClear   = new QPushButton(QStringLiteral("Reset"), this);
    ctl->addWidget(mCapture); ctl->addWidget(mClear);
    mComplete = new QLabel(this);   // 완료 인디케이터(녹색 Ok)
    mComplete->setStyleSheet(QStringLiteral("font-weight:bold; padding:2px 8px;"));
    ctl->addWidget(mComplete); ctl->addStretch(1);
    lay->addLayout(ctl);

    mLive = new QLabel(QStringLiteral("Current: Waiting for signal…"), this);
    mLive->setStyleSheet(QStringLiteral("font-family:monospace;"));
    lay->addWidget(mLive);

    mTable = new QTableWidget(0, 4, this);
    mTable->setHorizontalHeaderLabels({QStringLiteral("Position"), QStringLiteral("Rate s/d"),
                                       QStringLiteral("Beat ms"), QStringLiteral("Ampl °")});
    mTable->horizontalHeader()->setStretchLastSection(true);
    lay->addWidget(mTable, 1);

    // 요약 표: X / D / DVH / Di 행.
    mSummary = new QTableWidget(4, 4, this);
    mSummary->setHorizontalHeaderLabels({QStringLiteral(""), QStringLiteral("Rate"),
                                         QStringLiteral("Beat"), QStringLiteral("Ampl")});
    mSummary->verticalHeader()->setVisible(false);
    mSummary->horizontalHeader()->setStretchLastSection(true);
    const char *rn[4] = {"X (mean)", "D (max-min)", "DVH (V-H)", "Di (V unbal.)"};
    for (int r = 0; r < 4; ++r) {
        auto *it = new QTableWidgetItem(QString::fromLatin1(rn[r]));
        it->setFlags(Qt::ItemIsEnabled);
        mSummary->setItem(r, 0, it);
    }
    mSummary->setMaximumHeight(160);
    lay->addWidget(mSummary);

    connect(mCapture, &QPushButton::clicked, this, &TabSequenceDisplay::capture);
    connect(mClear,   &QPushButton::clicked, this, &TabSequenceDisplay::onResetSession);
    recomputeSummary();
}

void TabSequenceDisplay::onMeasurement(const MeasurementSnapshot &s)
{
    mLast = s; mHaveLast = true;
    mLive->setText(QString("Current[%1]:  rate=%2 s/d   beat=%3 ms   amp=%4°   bph=%5")
        .arg(mPos->currentText())
        .arg(s.rateValid ? QString::asprintf("%+.1f", s.rate) : QStringLiteral("--"))
        .arg(s.beatErrorValid ? QString::number(s.beatErrorMs,'f',2) : QStringLiteral("--"))
        .arg(s.amplitudeValid ? QString::number(s.amplitudeDeg,'f',0) : QStringLiteral("--"))
        .arg(s.bphValid ? QString::number(s.bph) : QStringLiteral("--")));
}

void TabSequenceDisplay::capture()
{
    if (!mHaveLast || mTable->rowCount() >= 10) return;
    const int r = mTable->rowCount();
    mTable->insertRow(r);
    mTable->setItem(r, 0, new QTableWidgetItem(mPos->currentText()));
    mTable->setItem(r, 1, new QTableWidgetItem(mLast.rateValid ? QString::asprintf("%+.1f", mLast.rate) : QStringLiteral("--")));
    mTable->setItem(r, 2, new QTableWidgetItem(mLast.beatErrorValid ? QString::number(mLast.beatErrorMs,'f',2) : QStringLiteral("--")));
    mTable->setItem(r, 3, new QTableWidgetItem(mLast.amplitudeValid ? QString::number(mLast.amplitudeDeg,'f',0) : QStringLiteral("--")));
    recomputeSummary();
}

void TabSequenceDisplay::recomputeSummary()
{
    // 컬럼별(Rate=1, Beat=2, Ampl=3) 통계 수집 + 수직/수평 분리.
    auto setCell = [&](int row, int col, const QString &t){
        auto *it = new QTableWidgetItem(t); it->setFlags(Qt::ItemIsEnabled);
        mSummary->setItem(row, col, it);
    };
    for (int c = 1; c <= 3; ++c) {
        QVector<double> all, vert, horiz; bool any=false;
        for (int r = 0; r < mTable->rowCount(); ++r) {
            bool ok=false; const double v = mTable->item(r,c) ? mTable->item(r,c)->text().toDouble(&ok) : 0.0;
            if (!ok) continue; any=true; all.push_back(v);
            const QString pos = mTable->item(r,0) ? mTable->item(r,0)->text() : QString();
            (isHorizontal(pos) ? horiz : vert).push_back(v);
        }
        if (!any) { setCell(0,c,"--"); setCell(1,c,"--"); setCell(2,c,"--"); setCell(3,c,"--"); continue; }
        const int prec = (c==1?1:(c==2?2:0));
        double mn=all[0],mx=all[0],sum=0; for(double v:all){mn=std::min(mn,v);mx=std::max(mx,v);sum+=v;}
        setCell(0,c, QString::number(sum/all.size(),'f',prec));   // X
        setCell(1,c, QString::number(mx-mn,'f',prec));            // D
        // DVH = 평균(수직) − 평균(수평) — Rate/Ampl 만 의미.
        if (c != 2 && !vert.isEmpty() && !horiz.isEmpty()) {
            double sv=0; for(double v:vert)sv+=v; double sh=0; for(double v:horiz)sh+=v;
            setCell(2,c, QString::number(sv/vert.size()-sh/horiz.size(),'f',prec));
        } else setCell(2,c,"--");
        // Di = 수직 포지션 rate 산포(max-min) — 불균형 지표(Rate 만).
        //  Plan: "indicators that help reveal possible balance-wheel unbalance" → 임계 초과 시 빨간 강조.
        if (c == 1 && vert.size() >= 2) {
            double vmn=vert[0],vmx=vert[0]; for(double v:vert){vmn=std::min(vmn,v);vmx=std::max(vmx,v);}
            const double di = vmx - vmn;
            setCell(3,c, QString::number(di,'f',prec));
            if (auto *it = mSummary->item(3, c)) {
                const bool unbal = di > kUnbalanceSd;
                it->setBackground(unbal ? QBrush(QColor(255, 120, 120)) : QBrush());
                if (unbal) it->setToolTip(QStringLiteral("excessive vertical-position rate spread → suspected balance-wheel unbalance"));
            }
        } else setCell(3,c,"--");
    }
    updateComplete();
}

void TabSequenceDisplay::updateComplete()
{
    // 6개 핵심 포지션(CH·CB·9H·6H·3H·12H)이 모두 캡처되면 녹색 "Ok" 표시.
    static const char *core[6] = {"CH", "CB", "9H", "6H", "3H", "12H"};
    bool have[6] = {false,false,false,false,false,false};
    for (int r = 0; mTable && r < mTable->rowCount(); ++r) {
        const QString pos = mTable->item(r,0) ? mTable->item(r,0)->text() : QString();
        for (int k = 0; k < 6; ++k)
            if (pos.startsWith(QString::fromLatin1(core[k]))) have[k] = true;
    }
    int n = 0; for (bool b : have) if (b) ++n;
    if (!mComplete) return;
    if (n >= 6) {
        mComplete->setText(QStringLiteral("✓ Sequence complete (Ok)"));
        mComplete->setStyleSheet(QStringLiteral("font-weight:bold; padding:2px 8px; color:#0a8a0a;"));
    } else {
        mComplete->setText(QStringLiteral("Progress %1/6 positions").arg(n));
        mComplete->setStyleSheet(QStringLiteral("font-weight:bold; padding:2px 8px; color:#888;"));
    }
}

void TabSequenceDisplay::onResetSession()
{
    if (mTable) mTable->setRowCount(0);
    recomputeSummary();
}
