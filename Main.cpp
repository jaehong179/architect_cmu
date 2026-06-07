#include "MainWindow.h"
#include "PerfInstrumentation.h"   // [PERF 계측] 성능 검증 로그 (docs/PERF_VERIFICATION_GUIDE.md)

#include <QApplication>
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

 // [PERF 계측] perf_log.csv 열기 — 모든 측정값(§A~§D)이 여기로 기록된다.
 //   (docs/PERF_VERIFICATION_GUIDE.md 의 section/qa 태그로 문서와 1:1 연결)
 Perf::init("timegrapher");

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

 // [PERF 계측] perf_log.csv 정상 종료(플러시/닫기)
 Perf::shutdown();

#ifdef Q_OS_WIN
 timeEndPeriod(1);
#endif

 return result;
}
