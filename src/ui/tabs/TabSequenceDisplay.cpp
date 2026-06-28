#include "TabSequenceDisplay.h"
#include "PositionNames.h"
#include "PositionTimingModel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QDebug>
#include <algorithm>
#include <cmath>

bool TabSequenceDisplay::isHorizontal(const QString &pos)
{
    return pos.startsWith(QStringLiteral("CH")) || pos.startsWith(QStringLiteral("CB"));
}

int TabSequenceDisplay::getRowIndexForPosition(const QString &posName) const
{
    const QString key = canonicalCorePositionKey(posName);
    if (key == QStringLiteral("CH")) return 0;
    if (key == QStringLiteral("CB")) return 1;
    if (key == QStringLiteral("9H")) return 2;
    if (key == QStringLiteral("6H")) return 3;
    if (key == QStringLiteral("3H")) return 4;
    if (key == QStringLiteral("12H")) return 5;
    return -1;
}

TabSequenceDisplay::TabSequenceDisplay(QWidget *parent) : TabView(parent)
{
    // 전체 다크 테마 적용
    this->setStyleSheet(QStringLiteral("background-color: #141414; color: #FFFFFF;"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // 상단 제어 바
    auto *ctl = new QHBoxLayout();
    ctl->setContentsMargins(0, 0, 0, 0);

    auto *posLabel = new QLabel(QStringLiteral("Position:"), this);
    posLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #888888;"));
    ctl->addWidget(posLabel);

    mPos = new QComboBox(this);
    mPos->addItems(standardPositionNames());
    mPos->setStyleSheet(QStringLiteral(
        "QComboBox { background-color: #222222; color: #FFFFFF; border: 1px solid #333333; padding: 4px 8px; border-radius: 4px; min-width: 120px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #222222; color: #FFFFFF; selection-background-color: #007acc; }"
    ));
    ctl->addWidget(mPos);

    mCapture = new QPushButton(QStringLiteral("Capture current"), this);
    mCapture->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #007acc; color: #FFFFFF; border: none; padding: 6px 14px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0098ff; }"
        "QPushButton:pressed { background-color: #005999; }"
    ));
    ctl->addWidget(mCapture);

    mClear = new QPushButton(QStringLiteral("Reset"), this);
    mClear->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #333333; color: #FFFFFF; border: none; padding: 6px 14px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #444444; }"
        "QPushButton:pressed { background-color: #222222; }"
    ));
    ctl->addWidget(mClear);

    mComplete = new QLabel(this);
    mComplete->setStyleSheet(QStringLiteral("font-weight:bold; padding:2px 8px;"));
    ctl->addWidget(mComplete);

    auto *timeLabel = new QLabel(QStringLiteral("Meas Time:"), this);
    timeLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #888888; margin-left: 15px;"));
    ctl->addWidget(timeLabel);

    mMeasTimeCombo = new QComboBox(this);
    mMeasTimeCombo->addItems({
        QStringLiteral("10 s"), QStringLiteral("15 s"), QStringLiteral("20 s"),
        QStringLiteral("30 s"), QStringLiteral("45 s"), QStringLiteral("60 s"),
        QStringLiteral("90 s"), QStringLiteral("120 s"), QStringLiteral("180 s"),
        QStringLiteral("300 s")
    });
    mMeasTimeCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background-color: #222222; color: #FFFFFF; border: 1px solid #333333; padding: 4px 8px; border-radius: 4px; min-width: 80px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #222222; color: #FFFFFF; selection-background-color: #007acc; }"
    ));
    ctl->addWidget(mMeasTimeCombo);

    connect(mMeasTimeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!mTiming) return;
        QString txt = mMeasTimeCombo->itemText(idx);
        int seconds = txt.split(QLatin1Char(' ')).first().toInt();
        for (int i = 0; i < 10; ++i) {
            mTiming->setMeasurementSec(i, seconds);
        }
    });

    ctl->addStretch(1);
    lay->addLayout(ctl);

    // 실시간 상태 라벨
    mLive = new QLabel(QStringLiteral("Current: Waiting for signal…"), this);
    mLive->setStyleSheet(QStringLiteral("font-family: monospace; color: #AAAAAA; padding: 2px 0px;"));
    lay->addWidget(mLive);

    // 메인 하단 영역 (좌우 2분할)
    auto *mainLay = new QHBoxLayout();
    mainLay->setSpacing(15);

    // 좌측: 단일 통합 테이블 위젯
    mTable = new QTableWidget(8, 4, this);
    mTable->setHorizontalHeaderLabels({QStringLiteral(""), QStringLiteral("Rate"),
                                       QStringLiteral("Amplitude"), QStringLiteral("Beat error")});
    mTable->verticalHeader()->setVisible(false);
    mTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mTable->horizontalHeader()->setStyleSheet(QStringLiteral(
        "QHeaderView::section { background-color: #1e1e1e; color: #888888; border: none; font-weight: bold; padding: 6px; }"
    ));
    mTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background-color: #141414; color: #E0E0E0; gridline-color: #252525; border: 1px solid #252525; }"
        "QTableWidget::item { padding: 4px; border-bottom: 1px solid #252525; }"
    ));

    // 테이블 초기값 세팅 (행별 타이틀 고정 및 읽기전용)
    const QString rowNames[8] = {
        QStringLiteral("Dial Up"), QStringLiteral("Dial Down"), QStringLiteral("Crown Right"),
        QStringLiteral("Crown Left"), QStringLiteral("Crown Up"), QStringLiteral("Crown Down"),
        QStringLiteral("Average"), QStringLiteral("Deviation")
    };
    for (int r = 0; r < 8; ++r) {
        auto *it = new QTableWidgetItem(rowNames[r]);
        it->setFlags(Qt::ItemIsEnabled);
        if (r >= 6) {
            // 통계 행은 스타일을 다르게 (연한 회색 글씨)
            it->setForeground(QBrush(QColor(150, 150, 150)));
        } else {
            it->setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::Bold));
        }
        mTable->setItem(r, 0, it);

        // 나머지 셀들도 빈 값으로 아이템 채우기
        for (int c = 1; c < 4; ++c) {
            auto *cell = new QTableWidgetItem(QStringLiteral("--"));
            cell->setFlags(Qt::ItemIsEnabled);
            cell->setTextAlignment(Qt::AlignCenter);
            if (r >= 6) {
                cell->setForeground(QBrush(QColor(150, 150, 150)));
            }
            mTable->setItem(r, c, cell);
        }
    }
    mainLay->addWidget(mTable, 6); // 비율 6

    // 우측: 레이더 차트 위젯
    mRadar = new RadarChartWidget(this);
    mainLay->addWidget(mRadar, 5); // 비율 5

    lay->addLayout(mainLay, 1);

    connect(mCapture, &QPushButton::clicked, this, &TabSequenceDisplay::capture);
    connect(mClear,   &QPushButton::clicked, this, &TabSequenceDisplay::onResetSession);
    connect(mPos, &QComboBox::currentIndexChanged, this, &TabSequenceDisplay::onPositionComboChanged);

    mPrevPos = mPos->currentText();
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

    // 실시간 측정 중인 포지션 행에 수치 실시간 업데이트
    int r = getRowIndexForPosition(mPos->currentText());
    if (r >= 0 && r < 6) {
        mTable->item(r, 1)->setText(s.rateValid ? QString::asprintf("%+.1f", s.rate) : QStringLiteral("--"));
        mTable->item(r, 2)->setText(s.amplitudeValid ? QString::number(s.amplitudeDeg, 'f', 0) : QStringLiteral("--"));
        mTable->item(r, 3)->setText(s.beatErrorValid ? QString::number(s.beatErrorMs, 'f', 2) : QStringLiteral("--"));

        // 오차 한도 초과 시 실시간 빨간색 텍스트 경고
        bool isCritical = false;
        if (s.rateValid && std::abs(s.rate) > 20.0) isCritical = true;
        if (s.beatErrorValid && s.beatErrorMs > 0.8) isCritical = true;
        if (s.amplitudeValid && s.amplitudeDeg < 220.0) isCritical = true;

        QColor textColor = isCritical ? QColor(255, 77, 77) : QColor(224, 224, 224);
        for (int c = 0; c < 4; ++c) {
            if (mTable->item(r, c)) {
                mTable->item(r, c)->setForeground(QBrush(textColor));
            }
        }
    }

    recomputeSummary();
    updateRadarChart();
}

void TabSequenceDisplay::capture()
{
    if (!mHaveLast) return;
    
    int r = getRowIndexForPosition(mPos->currentText());
    if (r < 0 || r >= 6) return;

    // 수동 캡처: 현재 실시간 측정값(mLast)을 고정 기록
    mTable->item(r, 1)->setText(mLast.rateValid ? QString::asprintf("%+.1f", mLast.rate) : QStringLiteral("--"));
    mTable->item(r, 2)->setText(mLast.amplitudeValid ? QString::number(mLast.amplitudeDeg, 'f', 0) : QStringLiteral("--"));
    mTable->item(r, 3)->setText(mLast.beatErrorValid ? QString::number(mLast.beatErrorMs, 'f', 2) : QStringLiteral("--"));

    bool isCritical = false;
    if (mLast.rateValid && std::abs(mLast.rate) > 20.0) isCritical = true;
    if (mLast.beatErrorValid && mLast.beatErrorMs > 0.8) isCritical = true;
    if (mLast.amplitudeValid && mLast.amplitudeDeg < 220.0) isCritical = true;

    QColor textColor = isCritical ? QColor(255, 77, 77) : QColor(224, 224, 224);
    for (int c = 0; c < 4; ++c) {
        if (mTable->item(r, c)) {
            mTable->item(r, c)->setForeground(QBrush(textColor));
        }
    }

    recomputeSummary();
    updateRadarChart();
}

void TabSequenceDisplay::recomputeSummary()
{
    // 각 열(Rate=1, Ampl=2, Beat=3) 통계 계산
    for (int c = 1; c < 4; ++c) {
        QVector<double> values;
        for (int r = 0; r < 6; ++r) {
            QTableWidgetItem *it = mTable->item(r, c);
            if (it && it->text() != QStringLiteral("--")) {
                bool ok = false;
                double v = it->text().toDouble(&ok);
                if (ok) values.push_back(v);
            }
        }

        int prec = (c == 1 ? 1 : (c == 2 ? 0 : 2));

        if (values.isEmpty()) {
            mTable->item(6, c)->setText(QStringLiteral("--"));
            mTable->item(7, c)->setText(QStringLiteral("--"));
            continue;
        }

        // 1. 평균(Average) 계산
        double sum = 0.0;
        for (double v : values) sum += v;
        double avg = sum / values.size();
        mTable->item(6, c)->setText(QString::number(avg, 'f', prec));

        // 2. 표본 표준편차(Deviation) 계산
        if (values.size() >= 2) {
            double varSum = 0.0;
            for (double v : values) {
                varSum += (v - avg) * (v - avg);
            }
            double sd = std::sqrt(varSum / (values.size() - 1));
            mTable->item(7, c)->setText(QString::number(sd, 'f', prec));
        } else {
            mTable->item(7, c)->setText(QStringLiteral("--"));
        }
    }
    updateComplete();
}

void TabSequenceDisplay::updateComplete()
{
    // 6개 핵심 포지션이 모두 채워졌는지 검사
    bool haveAll = true;
    for (int r = 0; r < 6; ++r) {
        if (mTable->item(r, 1)->text() == QStringLiteral("--")) {
            haveAll = false;
            break;
        }
    }

    int n = 0;
    for (int r = 0; r < 6; ++r) {
        if (mTable->item(r, 1)->text() != QStringLiteral("--")) {
            n++;
        }
    }

    if (!mComplete) return;

    if (haveAll) {
        mComplete->setText(QStringLiteral("✓ Sequence complete (Ok)"));
        mComplete->setStyleSheet(QStringLiteral("font-weight:bold; padding:2px 8px; color:#00FF66;"));
    } else {
        mComplete->setText(QStringLiteral("Progress %1/6 positions").arg(n));
        mComplete->setStyleSheet(QStringLiteral("font-weight:bold; padding:2px 8px; color:#888888;"));
    }
}

void TabSequenceDisplay::setCurrentPositionByIndex(int index)
{
    if (!mPos || index < 0 || index >= mPos->count())
        return;
    if (mPos->currentIndex() == index)
        return;

    qInfo().noquote() << QStringLiteral("[pos-sync] source=programmatic targetIndex=%1 targetName=%2")
                             .arg(index)
                             .arg(mPos->itemText(index));

    mProgrammaticPositionChange = true;
    mPos->setCurrentIndex(index);
    mProgrammaticPositionChange = false;
}

void TabSequenceDisplay::setPhaseStatus(const QString &phaseLabel, int remainingSec)
{
    if (!mLive) return;
    if (phaseLabel.isEmpty() || phaseLabel == QStringLiteral("idle")) {
        if (mHaveLast)
            onMeasurement(mLast);
        else
            mLive->setText(QStringLiteral("Current: Waiting for signal…"));
        return;
    }
    const QString phaseText = (phaseLabel == QStringLiteral("stabilizing"))
        ? QStringLiteral("Stabilizing") : QStringLiteral("Measuring");
    mLive->setText(QStringLiteral("%1 [%2]: %3 s remaining")
        .arg(phaseText, mPos->currentText())
        .arg(remainingSec));
}

void TabSequenceDisplay::onResetSession()
{
    // 0~5행 데이터 초기화 및 텍스트 색상 복원
    for (int r = 0; r < 6; ++r) {
        for (int c = 1; c < 4; ++c) {
            if (mTable->item(r, c)) {
                mTable->item(r, c)->setText(QStringLiteral("--"));
            }
        }
        // 기본 텍스트 색상으로 복원
        for (int c = 0; c < 4; ++c) {
            if (mTable->item(r, c)) {
                mTable->item(r, c)->setForeground(QBrush(QColor(224, 224, 224)));
            }
        }
    }

    if (mRadar) {
        mRadar->clearData();
    }

    mPrevPos = mPos->currentText(); // 포지션 리셋
    mHaveLast = false;

    recomputeSummary();
    updateRadarChart();
}

void TabSequenceDisplay::onPositionComboChanged(int index)
{
    const QString source = mProgrammaticPositionChange ? QStringLiteral("programmatic") : QStringLiteral("manual");
    const QString newPos = (mPos && index >= 0 && index < mPos->count()) ? mPos->itemText(index) : QStringLiteral("<invalid>");
    qInfo().noquote() << QStringLiteral("[pos-sync] source=%1 prev=%2 new=%3 index=%4")
                             .arg(source, mPrevPos, newPos)
                             .arg(index);

    // 1. 포지션이 변경되기 직전, 이전 포지션(mPrevPos)에 대하여 자동 캡처(고정) 수행
    if (mHaveLast && !mPrevPos.isEmpty()) {
        int r = getRowIndexForPosition(mPrevPos);
        if (r >= 0 && r < 6) {
            mTable->item(r, 1)->setText(mLast.rateValid ? QString::asprintf("%+.1f", mLast.rate) : QStringLiteral("--"));
            mTable->item(r, 2)->setText(mLast.amplitudeValid ? QString::number(mLast.amplitudeDeg, 'f', 0) : QStringLiteral("--"));
            mTable->item(r, 3)->setText(mLast.beatErrorValid ? QString::number(mLast.beatErrorMs, 'f', 2) : QStringLiteral("--"));

            // 이전 포지션의 색상 하이라이트 확정
            bool isCritical = false;
            if (mLast.rateValid && std::abs(mLast.rate) > 20.0) isCritical = true;
            if (mLast.beatErrorValid && mLast.beatErrorMs > 0.8) isCritical = true;
            if (mLast.amplitudeValid && mLast.amplitudeDeg < 220.0) isCritical = true;

            QColor textColor = isCritical ? QColor(255, 77, 77) : QColor(224, 224, 224);
            for (int c = 0; c < 4; ++c) {
                if (mTable->item(r, c)) {
                    mTable->item(r, c)->setForeground(QBrush(textColor));
                }
            }
        }
    }

    // 2. 실시간 측정값 상태 초기화 (새로운 신호를 대기)
    mHaveLast = false;
    mPrevPos = mPos->currentText(); // 현재 선택된 포지션으로 전환

    recomputeSummary();
    updateRadarChart();
}

void TabSequenceDisplay::updateRadarChart()
{
    if (!mRadar) return;

    // 테이블에 표시된 데이터를 정직하게 읽어서 레이더 차트 갱신
    for (int r = 0; r < 6; ++r) {
        QString posName = mTable->item(r, 0)->text();
        QTableWidgetItem *rateItem = mTable->item(r, 1);
        if (rateItem && rateItem->text() != QStringLiteral("--")) {
            bool ok = false;
            double rateVal = rateItem->text().toDouble(&ok);
            if (ok) {
                mRadar->setPositionRate(posName, rateVal, true);
                continue;
            }
        }
        // 미입력/유효하지 않은 데이터는 invalid 처리
        mRadar->setPositionRate(posName, 0.0, false);
    }
}

void TabSequenceDisplay::setTimingModel(PositionTimingModel *model)
{
    if (mTiming) {
        disconnect(mTiming, nullptr, this, nullptr);
    }
    mTiming = model;
    if (mTiming) {
        connect(mTiming, &QAbstractItemModel::dataChanged, this, &TabSequenceDisplay::onTimingDataChanged);
        connect(mTiming, &QAbstractItemModel::modelReset, this, &TabSequenceDisplay::onTimingModelReset);
        onTimingModelReset(); // Populate initial values
    }
}

void TabSequenceDisplay::onTimingDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    Q_UNUSED(topLeft);
    Q_UNUSED(bottomRight);
    if (!mTiming) return;
    onTimingModelReset();
}

void TabSequenceDisplay::onTimingModelReset()
{
    if (!mTiming || !mMeasTimeCombo) return;
    int currentSec = mTiming->measurementSecAt(0); // Use index 0 as representative
    QString txt = QString::number(currentSec) + QStringLiteral(" s");
    int idx = mMeasTimeCombo->findText(txt);
    if (idx >= 0) {
        mMeasTimeCombo->blockSignals(true);
        mMeasTimeCombo->setCurrentIndex(idx);
        mMeasTimeCombo->blockSignals(false);
    }
}

void TabSequenceDisplay::onRunningStateChanged(bool isRunning)
{
    if (mMeasTimeCombo) {
        mMeasTimeCombo->setEnabled(!isRunning);
    }
}

