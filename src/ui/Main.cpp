#include "MainWindow.h"
#include "PerfInstrumentation.h"   // [PERF 계측] 성능 검증 로그 (docs/PERF_VERIFICATION_GUIDE.md)

#include <QApplication>
#if PERF_ENABLE && defined(Q_OS_LINUX)
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <processthreadsapi.h>
#endif

int main(int argc, char *argv[])
{
  int result;

#ifdef Q_OS_WIN
 PROCESS_POWER_THROTTLING_STATE PowerThrottling = {0};
 PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
 PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
 PowerThrottling.StateMask = 0; //This will turn off - PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
 SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));
 timeBeginPeriod(1);
 if (SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
     qInfo()<<"WINDOWS OS - Process successfully set to realtime";
 }
#endif

 QApplication a(argc, argv);

 // [PERF 계측] perf_log.csv 열기 — 모든 측정값이 여기로 기록된다(PERF_ENABLE=0 이면 no-op).
 //   (docs/PERF_VERIFICATION_GUIDE.md 의 section/qa 태그로 문서와 1:1 연결)
 PERF_INIT("timegrapher");

#if PERF_ENABLE && defined(Q_OS_LINUX)
 // [PERF] 외부 자원 측정 스크립트(perf_resources.sh)가 실행파일 옆에 있으면 자동 실행한다.
 //  자기 PID 를 넘겨 앱이 종료되면 스크립트도 함께 종료(고아 방지). 스크립트가 없으면(프로덕션
 //  배포 등) 아무 것도 하지 않는다 → '파일 존재 + PERF_ENABLE=1' 2단 안전장치. (Pi 전용)
 {
     const QString script = QDir(QCoreApplication::applicationDirPath()).filePath("perf_resources.sh");
     if (QFileInfo::exists(script))
         QProcess::startDetached("/bin/bash",
                                 { script, QString::number(QCoreApplication::applicationPid()) });
 }
#endif

 //QApplication::setStyle(QStyleFactory::create("Fusion"));

 QPixmap Pixmap(":/images/Splash.png");
 if (Pixmap.isNull())
  {
     qInfo() << "Failed to load splash image!";
  }
 QPixmap scaledPixmap = Pixmap.scaled(1280, 750, Qt::KeepAspectRatio, Qt::SmoothTransformation);

 QSplashScreen splash(scaledPixmap,Qt::WindowStaysOnTopHint);
 splash.show();

 QRect screenGeometry = QGuiApplication::primaryScreen()->availableGeometry();
 int x = (screenGeometry.width() - splash.width()) / 2;
 int y = (screenGeometry.height() - splash.height()) / 2;
 splash.move(x, y);

 QThread::msleep(100); //Needed for Linux.... not sure why
 a.processEvents();

 QThread::sleep(4);

 MainWindow w;
 w.show();

 splash.finish(&w);

 result = a.exec();

 // [PERF 계측] perf_log.csv 정상 종료(플러시/닫기) (PERF_ENABLE=0 이면 no-op)
 PERF_SHUTDOWN();

#ifdef Q_OS_WIN
 timeEndPeriod(1);
#endif

 return result;
}
