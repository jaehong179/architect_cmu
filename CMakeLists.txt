cmake_minimum_required(VERSION 3.16)

project(TimeGrapher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(MINGW)
    add_compile_options("-Wa,-mbig-obj")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS PrintSupport REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Widgets)

#DP REMOVED NOT NEED IN THIS VERSION
#IF(WIN32)
#add_library(fftw SHARED IMPORTED)
#set_property(TARGET fftw PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/fftw/libfftw3f-3.dll")
#set_property(TARGET fftw PROPERTY IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/fftw/libfftw3f-3.lib")
#ENDIF()
#DP-END

set(PROJECT_SOURCES
    Main.cpp
        MainWindow.cpp
        MainWindow.h
        MainWindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(TimeGrapher
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        qcustomplot.h
        qcustomplot.cpp
        # [audio] 공유 링버퍼 + 입력 소스/녹음 (capture·playback·sim·recording)
        src/audio/SharedAudio.h
        src/audio/capture/AudioWorker.h
        src/audio/capture/AudioWorker.cpp
        src/audio/playback/PlaybackWorker.h
        src/audio/playback/PlaybackWorker.cpp
        src/audio/recording/WaveHeader.h
        src/audio/recording/WavStreamWriter.h
        src/audio/recording/WavStreamWriter.cpp
        # [core/timing] 타이밍·동기·파이프라인 (순수 C, Qt 의존 0)
        src/core/timing/Timegrapher.h
        src/core/timing/Timegrapher.cpp
        src/core/timing/Bph.h
        src/core/timing/Bph.cpp
        # [core/detection] 이벤트 검출 상태머신
        src/core/detection/Detector.h
        src/core/detection/Detector.cpp
        # [core/dsp] 신호 필터(HPF·envelope)
        src/core/dsp/Dsp.h
        src/core/dsp/Dsp.cpp
        # [core/stats] 윈도 통계 유틸
        src/core/stats/RollingLeastSquares.h
        src/core/stats/RollingLeastSquares.cpp
        src/core/stats/RollingAverage.h
        src/core/stats/RollingAverage.cpp
        SoundImageWidget.h
        SoundImageWidget.cpp
        SoundImageRenderer.h
        SoundImageRenderer.cpp
        src/audio/sim/SimWorker.cpp
        src/audio/sim/SimWorker.h
        src/audio/sim/WatchSynthStream.cpp
        src/audio/sim/WatchSynthStream.h
        PerfInstrumentation.h
        PerfInstrumentation.cpp
        # [탭 모듈 · QA-MOD-01] 디스플레이 탭 인프라 + 신규 탭 모듈 (tabs/)
        tabs/MeasurementModel.h
        tabs/TabView.h
        tabs/TabScaffold.h
        tabs/WaveBuffer.h
        tabs/ScopeRender.h
        tabs/ReadoutBar.h
        tabs/TabManager.h
        tabs/TabManager.cpp
        tabs/TabTraceDisplay.h
        tabs/TabTraceDisplay.cpp
        tabs/TabVarioStability.h
        tabs/TabVarioStability.cpp
        tabs/TabSequenceDisplay.h
        tabs/TabSequenceDisplay.cpp
        tabs/TabBeatNoiseScope.h
        tabs/TabBeatNoiseScope.cpp
        tabs/TabBeatErrorTrace.h
        tabs/TabBeatErrorTrace.cpp
        tabs/TabLongTermPerformance.h
        tabs/TabLongTermPerformance.cpp
        tabs/TabEscapementAnalyzer.h
        tabs/TabEscapementAnalyzer.cpp
        tabs/TabSpectrogram.h
        tabs/TabSpectrogram.cpp
        tabs/TabWaveformCompare.h
        tabs/TabWaveformCompare.cpp
        tabs/TabSyncSweepScope.h
        tabs/TabSyncSweepScope.cpp
        tabs/TabFilterViews.h
        tabs/TabFilterViews.cpp
        tabs/ScopeFilters.h
        tabs/ScopeFilters.cpp
        tabs/LegendBox.h
        tabs/LegendBox.cpp
        Splash.qrc
        src/audio/capture/LinuxAudio.cpp
        src/audio/capture/LinuxAudio.h
        src/audio/capture/WindowsAudio.h
        src/audio/capture/WindowsAudio.cpp
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET TimeGrapher APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(TimeGrapher SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(TimeGrapher
            ${PROJECT_SOURCES}
        )
    endif()
endif()

#DP-START REMOVED NOT NEED IN THIS VERSION
#target_include_directories(TimeGrapher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/fftw")
#DP-END

# [탭 모듈] tabs/ 헤더 탐색 경로 (TabView.h 등 형제 include + AUTOMOC)
target_include_directories(TimeGrapher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tabs)

# [core 모듈] 평면 include("Detector.h" 등) 유지를 위한 탐색 경로 — 소비자 수정 없이 물리적 분리
target_include_directories(TimeGrapher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/detection
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/timing
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/stats
)

# [audio 모듈] 평면 include("SharedAudio.h"·"AudioWorker.h" 등) 유지를 위한 탐색 경로
target_include_directories(TimeGrapher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/capture
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/playback
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/sim
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/recording
)

target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::PrintSupport)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Multimedia)
IF(WIN32)
#DP REMOVED NOT NEED IN THIS VERSION
#target_link_libraries(TimeGrapher PRIVATE fftw)
#DP-END
target_link_libraries(TimeGrapher PRIVATE winmm)
target_link_libraries(TimeGrapher PRIVATE Ole32)
target_link_libraries(TimeGrapher PRIVATE Uuid)
target_link_libraries(TimeGrapher PRIVATE Propsys)
target_link_libraries(TimeGrapher PRIVATE psapi)   # [PERF 계측 §D-1] GetProcessMemoryInfo (Windows RSS)
ELSEIF(LINUX)
target_link_libraries(TimeGrapher PRIVATE asound)
#DP REMOVED NOT NEED IN THIS VERSION
#target_link_libraries(TimeGrapher PRIVATE fftw3f)
#DP-END
ELSE()

ENDIF()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.TimeGrapher)
endif()
set_target_properties(TimeGrapher PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(APPLE)
    set_target_properties(TimeGrapher PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
endif()

include(GNUInstallDirs)
install(TARGETS TimeGrapher
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(TimeGrapher)
endif()
