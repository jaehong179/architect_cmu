#ifndef PERFLOGWINDOW_H
#define PERFLOGWINDOW_H
// =============================================================================
//  PerfLogWindow — perf 계측 로그 실시간 뷰어(자유 이동·탭 전환 유지 팝업)
// -----------------------------------------------------------------------------
//  Config 의 "View Log" 체크박스가 켜지면 MainWindow 가 이 창을 띄운다.
//   · Summary 탭: 지정 6개 항목만(캡처 backlog·DSP·측정게시·렌더요청·페인트·전반(UI랙))
//                 count · last · avg · min · max 표
//   · Resources 탭: 프로세스 CPU 사용률·메모리(MB) — 현재값 + 실시간 그래프
//   · Raw Log 탭: 원본 한 줄씩(링버퍼, 지정 항목만)
//  탭 위젯 밖의 최상위 윈도우(Qt::Window)라 자유 이동 + 탭 전환에도 그대로 유지된다.
// =============================================================================
#include <QWidget>
#include <QHash>
#include <QStringList>
#include <QString>

class QPlainTextEdit;
class QTableWidget;
class QCheckBox;
class QLabel;
class QTimer;
class QCloseEvent;
class QShowEvent;
class QCustomPlot;

class PerfLogWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PerfLogWindow(QWidget *parent = nullptr);

public slots:
    // Perf::LogBus::logRecord → (지정 항목만) 통계 집계 + 원본 로그 추가 (UI 스레드)
    void addRecord(double t, const QString &section, const QString &qa, const QString &metric,
                   double value, const QString &unit, const QString &extra);

signals:
    void closed();   // 사용자가 창을 닫으면(X) MainWindow 가 체크박스 해제

protected:
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent *e) override;

private slots:
    void sampleResources();   // 1초 주기 — CPU%·메모리(MB) 샘플 → 라벨·그래프 갱신

private:
    void refreshTable();      // 표 재구성(주기)
    void resetStats();        // 통계·표·로그 초기화

    // ── 지정 항목(고정 순서 + 표시명) ──
    QStringList             mOrder;    // metric 순서(표시 순서)
    QHash<QString, QString> mLabel;    // metric → 표시명
    struct Stat { QString label, unit; double sum = 0, min = 0, max = 0, last = 0; long count = 0; };
    QHash<QString, Stat>    mStats;    // key = metric (지정 항목만)

    QTableWidget   *mTable        = nullptr;
    QPlainTextEdit *mView         = nullptr;
    QCheckBox      *mAutoScroll   = nullptr;
    QTimer         *mRefreshTimer = nullptr;
    bool            mDirty        = false;

    // ── 자원(CPU/메모리) ──
    QCustomPlot *mCpuPlot   = nullptr;
    QCustomPlot *mMemPlot   = nullptr;
    QLabel      *mCpuValue  = nullptr;
    QLabel      *mMemValue  = nullptr;
    QTimer      *mResTimer  = nullptr;
    double       mResElapsed = 0.0;             // 자원 그래프 x축(초)
    double       mCpuPrevProc = 0.0, mCpuPrevWall = 0.0; bool mHaveCpu = false;   // CPU 델타 상태
    int          mNumCores  = 1;
    double       cpuPercentSample();            // 플랫폼별 프로세스 CPU%(직전 호출 이후 평균)
    double       memoryMBSample() const;        // 플랫폼별 프로세스 메모리(MB)
};

#endif // PERFLOGWINDOW_H
