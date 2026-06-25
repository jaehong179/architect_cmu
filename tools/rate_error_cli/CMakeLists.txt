cmake_minimum_required(VERSION 3.16)
project(rate_error_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 메인 앱(TimeGrapher, Qt 빌드)과 무관하게 독립적으로 빌드되는 콘솔 도구.
# Qt 의존 없이 core/timing·core/dsp·core/detection(순수 C/C++)만 재사용한다.
set(TG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(rate_error_cli
    main.cpp
    WavReader.cpp
    ${TG_ROOT}/src/core/timing/Timegrapher.cpp
    ${TG_ROOT}/src/core/timing/Bph.cpp
    ${TG_ROOT}/src/core/detection/Detector.cpp
    ${TG_ROOT}/src/core/dsp/Dsp.cpp
)

target_include_directories(rate_error_cli PRIVATE
    # stub 을 먼저 둬서 Timegrapher.cpp 의 #include "PerfInstrumentation.h" 가
    # src/perf(Qt 의존)가 아니라 이 디렉터리의 no-op 버전을 찾게 한다.
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TG_ROOT}/src/core/timing
    ${TG_ROOT}/src/core/detection
    ${TG_ROOT}/src/core/dsp
)
