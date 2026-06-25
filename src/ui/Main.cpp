#include "MainWindow.h"
#include "PerfInstrumentation.h"   // [PERF 계측] 성능 검증 로그 (docs/PERF_VERIFICATION_GUIDE.md)
#include "VideoSplashScreen.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSplashScreen>
#include <QPixmap>
#include <QThread>
#include <QStyleFactory>
#include <QPalette>
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

 QApplication::setStyle(QStyleFactory::create("Fusion"));

 QPalette darkPalette;
 darkPalette.setColor(QPalette::Window, QColor(24, 24, 31)); // Matching #18181f in QML control panel
 darkPalette.setColor(QPalette::WindowText, QColor(240, 240, 240));
 darkPalette.setColor(QPalette::Base, QColor(30, 30, 38));
 darkPalette.setColor(QPalette::AlternateBase, QColor(37, 37, 48));
 darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
 darkPalette.setColor(QPalette::ToolTipText, Qt::white);
 darkPalette.setColor(QPalette::Text, QColor(240, 240, 240));
 darkPalette.setColor(QPalette::Button, QColor(37, 37, 48));
 darkPalette.setColor(QPalette::ButtonText, QColor(240, 240, 240));
 darkPalette.setColor(QPalette::BrightText, Qt::red);
 darkPalette.setColor(QPalette::Link, QColor(171, 71, 188)); // Purple Accent #ab47bc
 darkPalette.setColor(QPalette::Highlight, QColor(171, 71, 188));
 darkPalette.setColor(QPalette::HighlightedText, Qt::white);

 // Disabled states
 darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
 darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
 darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));

 QApplication::setPalette(darkPalette);

 // [PERF 계측] perf_log.csv 열기 — 모든 측정값이 여기로 기록된다(PERF_ENABLE=0 이면 no-op).
 //   (docs/PERF_VERIFICATION_GUIDE.md 의 section/qa 태그로 문서와 1:1 연결)
 PERF_INIT("timegrapher");

#if PERF_ENABLE && defined(Q_OS_LINUX)
 // [PERF] 외부 자원 측정 스크립트(resource_sample.sh)가 실행파일 옆에 있으면 자동 실행한다.
 //  -p 로 자기 PID 를 넘겨 앱 종료 시 스크립트도 함께 종료(고아 방지), -o 로 perf_log.csv 옆에 출력.
 //  스크립트가 없으면(프로덕션 배포 등) 아무 것도 안 함 → '파일 존재 + PERF_ENABLE=1' 2단 안전장치.
 //  resource_sample.sh 는 vcgencmd 필요(Pi 전용) → 비-Pi 에선 즉시 종료된다. 출력 컬럼은 analyze_perf.py 호환.
 {
     const QDir   dir(QCoreApplication::applicationDirPath());
     const QString script = dir.filePath("resource_sample.sh");
     if (QFileInfo::exists(script))
         QProcess::startDetached("/bin/bash",
             { script, "-p", QString::number(QCoreApplication::applicationPid()),
                       "-o", dir.filePath("resource_ext.csv") });
 }
#endif

  bool playedVideo = false;
  QString videoPath = QCoreApplication::applicationDirPath() + "/Splash.mp4";
  if (!QFileInfo::exists(videoPath)) {
      videoPath = "Splash.mp4";
  }

  if (QFileInfo::exists(videoPath)) {
      VideoSplashScreen videoSplash(videoPath);
      QRect screenGeometry = QGuiApplication::primaryScreen()->availableGeometry();
      int x = (screenGeometry.width() - videoSplash.width()) / 2;
      int y = (screenGeometry.height() - videoSplash.height()) / 2;
      videoSplash.move(x, y);

      videoSplash.show();
      a.processEvents();

      QEventLoop loop;
      QObject::connect(&videoSplash, &VideoSplashScreen::finished, &loop, &QEventLoop::quit);
      videoSplash.play();
      loop.exec();
      playedVideo = true;
  }

  QSplashScreen* splash = nullptr;
  if (!playedVideo) {
      QPixmap Pixmap(":/images/Splash.png");
      if (Pixmap.isNull()) {
          qInfo() << "Failed to load splash image!";
      }
      QPixmap scaledPixmap = Pixmap.scaled(1280, 750, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      splash = new QSplashScreen(scaledPixmap, Qt::WindowStaysOnTopHint);
      splash->show();

      QRect screenGeometry = QGuiApplication::primaryScreen()->availableGeometry();
      int x = (screenGeometry.width() - splash->width()) / 2;
      int y = (screenGeometry.height() - splash->height()) / 2;
      splash->move(x, y);

      QThread::msleep(100); // Needed for Linux.... not sure why
      a.processEvents();

      QThread::sleep(4);
  }

  MainWindow w;
  w.show();

  if (splash) {
      splash->finish(&w);
      delete splash;
  }

  result = a.exec();

 // [PERF 계측] perf_log.csv 정상 종료(플러시/닫기) (PERF_ENABLE=0 이면 no-op)
 PERF_SHUTDOWN();

#ifdef Q_OS_WIN
 timeEndPeriod(1);
#endif

 return result;
}
