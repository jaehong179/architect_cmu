#include "PerfLogWindow.h"
#include "qcustomplot.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QCloseEvent>
#include <QShowEvent>
#include <QThread>
#include <QFont>
#include <algorithm>
#include <cmath>

// 자원(CPU/메모리) 플랫폼 API — Qt 뒤에 포함(매크로 충돌 방지).
#if defined(Q_OS_WIN)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#elif defined(Q_OS_LINUX)
#  include <unistd.h>
#  include <cstdio>
#  include <cstring>
#  include <QDateTime>
#endif

namespace {
// 정수에 가까우면 정수로, 아니면 소수 3자리 — 카운트/샘플수와 ms 값이 함께 예쁘게 정렬되도록.
QString fmtNum(double v)
{
    if (std::fabs(v) < 1e15 && std::fabs(v - std::llround(v)) < 1e-9)
        return QString::number((long long)std::llround(v));
    return QString::number(v, 'f', 3);
}

// 다크 테마 자원 그래프 하나 만들기(제목 없음 — 라벨은 위젯이 별도로 표시).
QCustomPlot *makeResPlot(QWidget *parent, const QColor &line, const QString &yLabel)
{
    auto *p = new QCustomPlot(parent);
    p->setBackground(QColor(20, 20, 26));
    p->addGraph();
    p->graph(0)->setPen(QPen(line, 1.6));
    p->graph(0)->setBrush(QBrush(QColor(line.red(), line.green(), line.blue(), 50)));
    for (QCPAxis *ax : { p->xAxis, p->yAxis }) {
        ax->setBasePen(QPen(QColor(120, 120, 140)));
        ax->setTickPen(QPen(QColor(120, 120, 140)));
        ax->setSubTickPen(QPen(QColor(70, 70, 88)));
        ax->setTickLabelColor(QColor(180, 180, 195));
        ax->setLabelColor(QColor(180, 180, 195));
        ax->grid()->setPen(QPen(QColor(50, 50, 62), 1, Qt::DotLine));
    }
    p->yAxis->setLabel(yLabel);
    p->xAxis->setLabel(QStringLiteral("time (s)"));
    p->setMinimumHeight(150);
    return p;
}
} // namespace

PerfLogWindow::PerfLogWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)   // 부모는 MainWindow 지만 Qt::Window 라 독립 최상위 창(자유 이동)
{
    setWindowTitle(QStringLiteral("Performance Log (perf)"));
    setAttribute(Qt::WA_QuitOnClose, false);   // 이 창을 닫아도 앱은 종료되지 않음
    resize(880, 560);

    mNumCores = qMax(1, QThread::idealThreadCount());

    // 표시 대상 6개 항목(고정 순서) + 표시명.
    struct Def { const char *metric; const char *label; };
    static const Def defs[] = {
        { "backlog_samples",      "캡처 backlog" },
        { "dsp_total_ms",         "DSP" },
        { "tab_update_ms",        "측정게시" },
        { "proc2disp_latency_ms", "렌더요청" },
        { "disp_paint_ms",        "페인트" },
        { "ui_loop_lag_ms",       "전반(UI랙)" },
    };
    for (const Def &d : defs) {
        mOrder << QLatin1String(d.metric);
        mLabel.insert(QLatin1String(d.metric), QString::fromUtf8(d.label));
    }

    // 앱 다크 테마에 맞춘 스타일.
    setStyleSheet(QStringLiteral(
        "QWidget{background:#18181f;color:#e0e0e6;}"
        "QTabWidget::pane{border:1px solid #3a3a4a;}"
        "QTabBar::tab{background:#20202a;color:#9e9eb0;padding:5px 14px;border:1px solid #2c2c38;}"
        "QTabBar::tab:selected{background:#2f2f3a;color:#ffffff;}"
        "QTableWidget{background:#1e1e26;gridline-color:#33333f;"
        "  alternate-background-color:#23232e;selection-background-color:#3a3a5a;selection-color:#fff;}"
        "QHeaderView::section{background:#2a2a34;color:#df78ef;font-weight:bold;border:0;"
        "  border-right:1px solid #33333f;padding:5px;}"
        "QPlainTextEdit{background:#141419;color:#c8c8d2;border:1px solid #2c2c38;}"
        "QPushButton{background:#2f2f3a;border:1px solid #3a3a4a;border-radius:4px;padding:4px 12px;color:#fff;}"
        "QPushButton:hover{background:#3a3a48;}"
        "QCheckBox{color:#d0d0d8;}"
        "QLabel{color:#c0c0cc;}"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // 상단 바: 설명 + 자동 스크롤 + Reset(통계) + Clear(원본)
    auto *bar = new QHBoxLayout();
    auto *hint = new QLabel(QStringLiteral("Live perf metrics"), this);
    hint->setStyleSheet(QStringLiteral("color:#8a8a98; font-style:italic;"));
    bar->addWidget(hint);
    bar->addStretch(1);
    mAutoScroll = new QCheckBox(QStringLiteral("Auto-scroll"), this);
    mAutoScroll->setChecked(true);
    auto *resetBtn = new QPushButton(QStringLiteral("Reset stats"), this);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear log"), this);
    bar->addWidget(mAutoScroll);
    bar->addWidget(resetBtn);
    bar->addWidget(clearBtn);
    lay->addLayout(bar);

    auto *tabs = new QTabWidget(this);
    lay->addWidget(tabs, 1);

    // ── Summary 탭: 지정 항목 통계 표 ─────────────────────────────────────────
    mTable = new QTableWidget(this);
    const QStringList headers = { QStringLiteral("항목"),  QStringLiteral("단위"),
                                  QStringLiteral("Count"), QStringLiteral("Last"),
                                  QStringLiteral("Avg"),   QStringLiteral("Min"),
                                  QStringLiteral("Max") };
    mTable->setColumnCount(headers.size());
    mTable->setHorizontalHeaderLabels(headers);
    mTable->verticalHeader()->setVisible(false);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setAlternatingRowColors(true);
    mTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mTable->horizontalHeader()->setHighlightSections(false);
    tabs->addTab(mTable, QStringLiteral("Summary"));

    // ── Resources 탭: CPU/메모리 현재값 + 그래프 ──────────────────────────────
    auto *resBox = new QWidget(this);
    auto *resLay = new QVBoxLayout(resBox);
    resLay->setContentsMargins(4, 4, 4, 4);
    resLay->setSpacing(6);
    auto *valRow = new QHBoxLayout();
    mCpuValue = new QLabel(QStringLiteral("CPU  —"), resBox);
    mMemValue = new QLabel(QStringLiteral("Mem  —"), resBox);
    QFont big(QStringLiteral("sans"), 13, QFont::Bold);
    mCpuValue->setFont(big); mCpuValue->setStyleSheet(QStringLiteral("color:#4fc3f7;"));
    mMemValue->setFont(big); mMemValue->setStyleSheet(QStringLiteral("color:#ffb74d;"));
    valRow->addWidget(mCpuValue);
    valRow->addSpacing(24);
    valRow->addWidget(mMemValue);
    valRow->addStretch(1);
    resLay->addLayout(valRow);
    mCpuPlot = makeResPlot(resBox, QColor(79, 195, 247),  QStringLiteral("CPU %"));
    mMemPlot = makeResPlot(resBox, QColor(255, 183, 77),  QStringLiteral("Mem (MB)"));
    resLay->addWidget(mCpuPlot, 1);
    resLay->addWidget(mMemPlot, 1);
    tabs->addTab(resBox, QStringLiteral("Resources"));

    // ── Raw Log 탭: 원본 한 줄씩(지정 항목만) ─────────────────────────────────
    mView = new QPlainTextEdit(this);
    mView->setReadOnly(true);
    mView->setMaximumBlockCount(5000);                 // 링버퍼: 오래된 줄 자동 제거
    mView->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"), 9);
    mono.setStyleHint(QFont::TypeWriter);
    mView->setFont(mono);
    tabs->addTab(mView, QStringLiteral("Raw Log"));

    connect(clearBtn, &QPushButton::clicked, mView, &QPlainTextEdit::clear);
    connect(resetBtn, &QPushButton::clicked, this, &PerfLogWindow::resetStats);

    // 표는 매 레코드가 아니라 주기적으로 갱신(고빈도 로그에서 렌더 부하↓).
    mRefreshTimer = new QTimer(this);
    mRefreshTimer->setInterval(400);
    connect(mRefreshTimer, &QTimer::timeout, this, &PerfLogWindow::refreshTable);
    mRefreshTimer->start();

    // 자원 샘플러 — 1초 주기.
    mResTimer = new QTimer(this);
    mResTimer->setInterval(1000);
    connect(mResTimer, &QTimer::timeout, this, &PerfLogWindow::sampleResources);
    mResTimer->start();

    mDirty = true;
    refreshTable();   // 지정 6행을 즉시 표시(데이터 전엔 '—')
}

void PerfLogWindow::addRecord(double t, const QString &section, const QString &qa,
                              const QString &metric, double value, const QString &unit,
                              const QString &extra)
{
    Q_UNUSED(qa);
    auto lab = mLabel.constFind(metric);
    if (lab == mLabel.constEnd()) return;   // 지정 6개 항목만 남긴다(나머지 무시)

    Stat &s = mStats[metric];
    if (s.count == 0) { s.label = lab.value(); s.unit = unit; s.min = s.max = value; }
    else              { if (value < s.min) s.min = value; if (value > s.max) s.max = value; }
    s.sum += value; s.last = value; ++s.count;
    if (s.unit.isEmpty()) s.unit = unit;
    mDirty = true;

    // 원본 로그 한 줄(Raw Log 탭).
    if (mView) {
        QString line = QString("%1  %2  %3=%4 %5")
                           .arg(t, 8, 'f', 0).arg(lab.value(), -12).arg(section)
                           .arg(fmtNum(value)).arg(unit);
        if (!extra.isEmpty()) line += QStringLiteral("  ") + extra;
        QScrollBar *sb = mView->verticalScrollBar();
        const bool follow = mAutoScroll && mAutoScroll->isChecked();
        const int  keep   = sb ? sb->value() : 0;
        mView->appendPlainText(line);
        if (sb) sb->setValue(follow ? sb->maximum() : keep);
    }
}

void PerfLogWindow::refreshTable()
{
    if (!mDirty || !mTable) return;
    mDirty = false;

    QFont mono(QStringLiteral("monospace"), 9);
    mono.setStyleHint(QFont::TypeWriter);
    auto put = [&](int r, int c, const QString &text, Qt::Alignment align, bool numeric) {
        auto *it = new QTableWidgetItem(text);
        it->setTextAlignment(align | Qt::AlignVCenter);
        if (numeric) it->setFont(mono);
        mTable->setItem(r, c, it);
    };

    mTable->setRowCount(mOrder.size());
    for (int r = 0; r < mOrder.size(); ++r) {
        const QString &metric = mOrder[r];
        const QString label = mLabel.value(metric);
        auto it = mStats.constFind(metric);
        put(r, 0, label, Qt::AlignLeft, false);
        if (it == mStats.constEnd() || it->count == 0) {
            put(r, 1, QString(),               Qt::AlignHCenter, false);
            put(r, 2, QStringLiteral("0"),     Qt::AlignRight, true);
            for (int c = 3; c <= 6; ++c) put(r, c, QStringLiteral("—"), Qt::AlignRight, true);
            continue;
        }
        const Stat &s = it.value();
        const double avg = s.sum / s.count;
        put(r, 1, s.unit,                    Qt::AlignHCenter, false);
        put(r, 2, QString::number(s.count),  Qt::AlignRight, true);
        put(r, 3, fmtNum(s.last),            Qt::AlignRight, true);
        put(r, 4, fmtNum(avg),               Qt::AlignRight, true);
        put(r, 5, fmtNum(s.min),             Qt::AlignRight, true);
        put(r, 6, fmtNum(s.max),             Qt::AlignRight, true);
    }
}

void PerfLogWindow::resetStats()
{
    mStats.clear();
    if (mView) mView->clear();
    mResElapsed = 0.0; mHaveCpu = false;
    if (mCpuPlot) { mCpuPlot->graph(0)->data()->clear(); mCpuPlot->replot(); }
    if (mMemPlot) { mMemPlot->graph(0)->data()->clear(); mMemPlot->replot(); }
    mDirty = true;
    refreshTable();
}

// ── 자원 샘플링 ─────────────────────────────────────────────────────────────
double PerfLogWindow::cpuPercentSample()
{
#if defined(Q_OS_WIN)
    auto u64 = [](const FILETIME &ft) {
        ULARGE_INTEGER v; v.LowPart = ft.dwLowDateTime; v.HighPart = ft.dwHighDateTime; return (double)v.QuadPart;
    };
    FILETIME c, e, k, u; GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    const double proc = u64(k) + u64(u);            // 프로세스 CPU 사용(100ns)
    FILETIME nowFt; GetSystemTimeAsFileTime(&nowFt);
    const double wall = u64(nowFt);                 // 벽시계(100ns)
    double pct = 0.0;
    if (mHaveCpu && wall > mCpuPrevWall)
        pct = 100.0 * (proc - mCpuPrevProc) / ((wall - mCpuPrevWall) * mNumCores);
    mCpuPrevProc = proc; mCpuPrevWall = wall; mHaveCpu = true;
    return pct < 0.0 ? 0.0 : pct;
#elif defined(Q_OS_LINUX)
    double utime = 0, stime = 0;
    if (FILE *f = std::fopen("/proc/self/stat", "r")) {
        // 14·15번째 필드 = utime·stime(clock tick). 앞 필드는 건너뛴다.
        long ut = 0, st = 0;
        // comm 필드에 공백이 있을 수 있어 ') ' 뒤부터 파싱.
        char buf[1024]; if (std::fgets(buf, sizeof(buf), f)) {
            const char *p = std::strrchr(buf, ')');
            if (p) { int dummy; char state;
                std::sscanf(p + 2, "%c %d %d %d %d %d %*u %*u %*u %*u %*u %ld %ld",
                            &state, &dummy, &dummy, &dummy, &dummy, &dummy, &ut, &st); }
        }
        std::fclose(f);
        const double hz = (double)sysconf(_SC_CLK_TCK);
        utime = ut / hz; stime = st / hz;           // 초
    }
    const double proc = (utime + stime) * 1000.0;   // ms
    const double wall = (double)QDateTime::currentMSecsSinceEpoch();
    double pct = 0.0;
    if (mHaveCpu && wall > mCpuPrevWall)
        pct = 100.0 * (proc - mCpuPrevProc) / ((wall - mCpuPrevWall) * mNumCores);
    mCpuPrevProc = proc; mCpuPrevWall = wall; mHaveCpu = true;
    return pct < 0.0 ? 0.0 : pct;
#else
    return 0.0;
#endif
}

double PerfLogWindow::memoryMBSample() const
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX pmc; ZeroMemory(&pmc, sizeof(pmc)); pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc)))
        return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    return 0.0;
#elif defined(Q_OS_LINUX)
    double kb = 0.0;
    if (FILE *f = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
            if (std::sscanf(line, "VmRSS: %lf kB", &kb) == 1) break;
        std::fclose(f);
    }
    return kb / 1024.0;
#else
    return 0.0;
#endif
}

void PerfLogWindow::sampleResources()
{
    mResElapsed += 1.0;
    const double cpu = cpuPercentSample();
    const double mem = memoryMBSample();

    if (mCpuValue) mCpuValue->setText(QString("CPU  %1 %").arg(cpu, 0, 'f', 1));
    if (mMemValue) mMemValue->setText(QString("Mem  %1 MB").arg(mem, 0, 'f', 1));

    const double lo = mResElapsed - 120.0;   // 최근 2분만 유지
    auto feed = [&](QCustomPlot *p, double v, double yMinTop) {
        if (!p) return;
        p->graph(0)->addData(mResElapsed, v);
        p->graph(0)->data()->removeBefore(lo);
        p->xAxis->setRange(qMax(0.0, lo), qMax(1.0, mResElapsed));
        p->graph(0)->rescaleValueAxis(false, true);
        p->yAxis->setRange(0.0, qMax(yMinTop, p->yAxis->range().upper * 1.1));
        if (isVisible()) p->replot(QCustomPlot::rpQueuedReplot);
    };
    feed(mCpuPlot, cpu, 5.0);     // CPU: 최소 상단 5%
    feed(mMemPlot, mem, 50.0);    // Mem: 최소 상단 50MB
}

void PerfLogWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (mCpuPlot) mCpuPlot->replot();
    if (mMemPlot) mMemPlot->replot();
}

void PerfLogWindow::closeEvent(QCloseEvent *e)
{
    emit closed();            // MainWindow → viewLogOpen=false (체크박스 해제 + 버스 비활성)
    QWidget::closeEvent(e);   // 기본: 숨김(WA_DeleteOnClose 미설정 → 창 보존, 재오픈 가능)
}
