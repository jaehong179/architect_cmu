cmake_minimum_required(VERSION 3.16)

project(TimeGrapher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# [빌드 메모리] 거대 TU(qcustomplot 등) 병렬 컴파일 시 cc1plus 'out of memory' 방지 —
#  동시 컴파일 작업 수를 4로 제한(가용 RAM 대비 코어수 기본 병렬이 메모리를 초과하는 문제).
#  Ninja 제너레이터에서만 적용. 고사양/대용량 RAM 머신이면 숫자를 올려도 된다.
set_property(GLOBAL PROPERTY JOB_POOLS compile_pool=4)
set(CMAKE_JOB_POOL_COMPILE compile_pool)

if(MINGW)
    add_compile_options("-Wa,-mbig-obj")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS PrintSupport REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Widgets)

# [vision · TinyML] Watch-position 추론 기능
option(ENABLE_VISION "Build vision (TinyML watch-position) inference feature" ON)

#DP REMOVED NOT NEED IN THIS VERSION
#IF(WIN32)
#add_library(fftw SHARED IMPORTED)
#set_property(TARGET fftw PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/fftw/libfftw3f-3.dll")
#set_property(TARGET fftw PROPERTY IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/fftw/libfftw3f-3.lib")
#ENDIF()
#DP-END

set(PROJECT_SOURCES
    src/ui/Main.cpp
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
        src/ui/MainWindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(TimeGrapher
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        qcustomplot.h
        qcustomplot.cpp
        # [audio] 공유 링버퍼 + 입력 소스/녹음 (capture·playback·sim·recording)
        src/audio/SharedAudio.h
        src/audio/AudioRingBuffer.h
        src/audio/capture/AudioWorker.h
        src/audio/capture/AudioWorker.cpp
        src/audio/playback/PlaybackWorker.h
        src/audio/playback/PlaybackWorker.cpp
        src/audio/recording/WaveHeader.h
        src/audio/recording/WavStreamWriter.h
        src/audio/recording/WavStreamWriter.cpp
        src/audio/recording/WavFileReader.h
        src/audio/recording/WavFileReader.cpp
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
        # [render] Sound Print 폴딩 렌더러 + 위젯 (표현)
        src/render/SoundImageWidget.h
        src/render/SoundImageWidget.cpp
        src/render/SoundImageRenderer.h
        src/render/SoundImageRenderer.cpp
        src/audio/sim/SimWorker.cpp
        src/audio/sim/SimWorker.h
        src/audio/sim/WatchSynthStream.cpp
        src/audio/sim/WatchSynthStream.h
        # [perf] 성능 계측 (횡단 관심사)
        src/perf/PerfInstrumentation.h
        src/perf/PerfInstrumentation.cpp
        src/perf/UiResponsivenessSampler.h
        src/perf/UiResponsivenessSampler.cpp
        # [engine] 측정 계산 + 입력 소스 오케스트레이션
        src/engine/MeasurementEngine.h
        src/engine/MeasurementEngine.cpp
        src/engine/CaptureController.h
        src/engine/CaptureController.cpp
        src/engine/SimConfigBuilder.h
        src/engine/SimConfigBuilder.cpp
        # [탭 모듈 · QA-MOD-01] 디스플레이 탭 인프라 + 신규 탭 모듈 (tabs/)
        src/ui/tabs/MeasurementModel.h
        src/ui/tabs/TabView.h
        src/ui/tabs/PlotHelpers.h
        src/ui/tabs/TabScaffold.h
        src/ui/tabs/WaveBuffer.h
        src/ui/tabs/ScopeRender.h
        src/ui/tabs/ReadoutBar.h
        src/ui/tabs/TabManager.h
        src/ui/tabs/TabManager.cpp
        src/ui/tabs/TabTraceDisplay.h
        src/ui/tabs/TabTraceDisplay.cpp
        src/ui/tabs/TabVarioStability.h
        src/ui/tabs/TabVarioStability.cpp
        src/ui/tabs/TabSequenceDisplay.h
        src/ui/tabs/TabSequenceDisplay.cpp
        src/ui/tabs/TabBeatNoiseScope.h
        src/ui/tabs/TabBeatNoiseScope.cpp
        src/ui/tabs/TabBeatErrorTrace.h
        src/ui/tabs/TabBeatErrorTrace.cpp
        src/ui/tabs/TabLongTermPerformance.h
        src/ui/tabs/TabLongTermPerformance.cpp
        src/ui/tabs/TabEscapementAnalyzer.h
        src/ui/tabs/TabEscapementAnalyzer.cpp
        src/ui/tabs/TabSpectrogram.h
        src/ui/tabs/TabSpectrogram.cpp
        src/ui/tabs/TabWaveformCompare.h
        src/ui/tabs/TabWaveformCompare.cpp
        src/ui/tabs/TabSyncSweepScope.h
        src/ui/tabs/TabSyncSweepScope.cpp
        src/ui/tabs/TabFilterViews.h
        src/ui/tabs/TabFilterViews.cpp
        src/ui/tabs/ScopeFilters.h
        src/ui/tabs/ScopeFilters.cpp
        src/ui/tabs/LegendBox.h
        src/ui/tabs/LegendBox.cpp
        src/ui/tabs/TabSoundPrint.h
        src/ui/tabs/TabSoundPrint.cpp
        src/ui/tabs/TabRateScope.h
        src/ui/tabs/TabRateScope.cpp
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
target_include_directories(TimeGrapher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/tabs)

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

# [render·perf·engine 모듈] 평면 include 유지
target_include_directories(TimeGrapher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/perf
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

# [ui 모듈] MainWindow 헤더 탐색 경로 ("MainWindow.h" + "tabs/..." 접두사 include 해결)
target_include_directories(TimeGrapher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::PrintSupport)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Multimedia)

# ──────────────────────────────────────────────────────────────────────────────
# [vision · TinyML] Watch-position 추론 — TFLite C API 런타임 동적 로드.
#   - Windows : prebuilt tensorflowlite_c.dll 자동 다운로드(공개된 ValYouW/tflite-dist package)
#   - Linux/RPi5 : TensorFlow 소스(핀 v2.18.0)에서 libtensorflowlite_c.so 빌드(FetchContent)
#   - 클래스 라벨은 VisionConfig.h 에 직접 정의 / model.tflite → .qrc 임베드 (런타임 외부파일 0)
#   thirdparty 코드는 수정하지 않으며, 모두 자동 취득되어 clean 환경에서 빌드된다.
# ─────────────────────────────────────────────────────────────────────────
if(ENABLE_VISION)
    include(FetchContent)
    set(_TFLITE_VERSION "2.18.0")

    # ── 설정값 → VisionConfig.h 생성 (클래스 라벨은 헤더에 직접 정의됨) ──
    set(VISION_IMG_SIZE       192)
    set(VISION_CROP_X1        448)
    set(VISION_CROP_Y1        168)
    set(VISION_CROP_X2        832)
    set(VISION_CROP_Y2        552)
    set(VISION_CAM_WIDTH      1280)
    set(VISION_CAM_HEIGHT     720)
    set(VISION_CAMERA_ID      1)
    set(VISION_CONF_THRESH    0.6)
    set(VISION_NUM_THREADS    2)
    set(VISION_CLAHE_CLIP     2.0)
    set(VISION_CLAHE_TILES    8)
    set(VISION_INTERVAL_MS    1000)
    set(VISION_MODEL_RESOURCE ":/vision/model.tflite")

    set(_vision_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vision")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vision/VisionConfig.h.in"
        "${_vision_gen_dir}/VisionConfig.h"
        @ONLY)

    # ── TFLite C 라이브러리/헤더 확보 (플랫폼 분기) ──
    if(WIN32)
        FetchContent_Declare(tflite_dist
            URL      "https://github.com/ValYouW/tflite-dist/releases/download/v${_TFLITE_VERSION}/tflite-dist-${_TFLITE_VERSION}.zip"
            URL_HASH SHA256=ae6530c64e4e161fc46c97e2d1d3beb0518d42ab230ac560721a2719c0f2a1cc
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(tflite_dist)
        set(VISION_TFLITE_INCLUDE_DIR "${tflite_dist_SOURCE_DIR}/include")
        set(VISION_TFLITE_RUNTIME     "${tflite_dist_SOURCE_DIR}/libs/windows_x86_64/tensorflowlite_c.dll")
    else()
        FetchContent_Declare(tensorflow
            GIT_REPOSITORY https://github.com/tensorflow/tensorflow.git
            GIT_TAG        "v${_TFLITE_VERSION}"
            GIT_SHALLOW    TRUE)
        FetchContent_GetProperties(tensorflow)
        if(NOT tensorflow_POPULATED)
            FetchContent_Populate(tensorflow)
            add_subdirectory(
                "${tensorflow_SOURCE_DIR}/tensorflow/lite/c"
                "${CMAKE_CURRENT_BINARY_DIR}/tflite-c-build")
        endif()
        set(VISION_TFLITE_INCLUDE_DIR "${tensorflow_SOURCE_DIR}")
        set(VISION_TFLITE_RUNTIME     "$<TARGET_FILE:tensorflowlite_c>")
        add_dependencies(TimeGrapher tensorflowlite_c)   # 빌드 의존성만(링크 X — 런타임 dlopen)
    endif()

    target_sources(TimeGrapher PRIVATE
        src/vision/TfliteApi.h
        src/vision/TfliteApi.cpp
        src/vision/VisionPreprocess.h
        src/vision/VisionPreprocess.cpp
        src/vision/VisionWorker.h
        src/vision/VisionWorker.cpp
        src/vision/vision.qrc)
    target_include_directories(TimeGrapher PRIVATE
        "${_vision_gen_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vision"
        "${VISION_TFLITE_INCLUDE_DIR}")
    target_compile_definitions(TimeGrapher PRIVATE ENABLE_VISION=1)
    target_link_libraries(TimeGrapher PRIVATE ${CMAKE_DL_LIBS})

    # 런타임 라이브러리를 실행파일 옆에 복사(dlopen 탐색 경로).
    add_custom_command(TARGET TimeGrapher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VISION_TFLITE_RUNTIME}" "$<TARGET_FILE_DIR:TimeGrapher>"
        COMMENT "[vision] copy TFLite runtime next to executable")
endif()

IF(WIN32)
#DP REMOVED NOT NEED IN THIS VERSION
#target_link_libraries(TimeGrapher PRIVATE fftw)
#DP-END
target_link_libraries(TimeGrapher PRIVATE winmm)
target_link_libraries(TimeGrapher PRIVATE Ole32)
target_link_libraries(TimeGrapher PRIVATE Uuid)
target_link_libraries(TimeGrapher PRIVATE Propsys)
# (psapi 링크 제거 — 자원 측정은 외부 스크립트로 이동, 인앱 GetProcessMemoryInfo 미사용)
ELSEIF(LINUX)
target_link_libraries(TimeGrapher PRIVATE asound)
#DP REMOVED NOT NEED IN THIS VERSION
#target_link_libraries(TimeGrapher PRIVATE fftw3f)
#DP-END
ELSE()

ENDIF()

# [PERF] 외부 자원 측정 스크립트(resource_sample.sh)를 실행파일 옆에 복사한다.
#  PERF_ENABLE=1 빌드는 시작 시 이 파일이 있으면 자동 실행(없으면 무시 — 2단 안전장치).
#  앱이 '/bin/bash <script>' 로 실행하므로 실행권한(+x)은 불필요. 출력은 analyze_perf.py 호환 resource_ext.csv.
add_custom_command(TARGET TimeGrapher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/resource_sample.sh"
            "$<TARGET_FILE_DIR:TimeGrapher>/resource_sample.sh"
    COMMENT "[PERF] copy resource_sample.sh next to executable")

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
