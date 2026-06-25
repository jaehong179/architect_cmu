#ifndef PERFINSTRUMENTATION_H
#define PERFINSTRUMENTATION_H
// 이 콘솔 도구 전용 스텁 — src/perf/PerfInstrumentation.h 는 QString 등 Qt 타입을 쓰므로
// Qt-free 빌드를 위해 Timegrapher.cpp 가 #include "PerfInstrumentation.h" 할 때
// (이 디렉터리를 src/perf 보다 먼저 include path 에 둬서) 대신 이 no-op 버전이 선택되게 한다.
#define PERF_NOW()                 (0.0)
#define PERF_LOG(s,q,m,v,u,...)    ((void)0)
#define PERF_INIT(tag)             ((void)0)
#define PERF_SHUTDOWN()            ((void)0)
#define PERF_SET_ECHO(on)          ((void)0)
#endif // PERFINSTRUMENTATION_H
