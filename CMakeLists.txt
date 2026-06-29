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
set_property(GLOBAL PROPERTY JOB_POOLS compile_pool=8)
set(CMAKE_JOB_POOL_COMPILE compile_pool)

if(MINGW)
    add_compile_options("-Wa,-mbig-obj")
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets QuickWidgets Network)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS PrintSupport REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia MultimediaWidgets Widgets QuickWidgets)

# [vision · TinyML] Watch-position 추론 기능
option(ENABLE_VISION "Build vision (TinyML watch-position) inference feature" ON)
# [diag · TinyML] t1/t3 고장유형 진단(diagnosis) 추론 기능
option(ENABLE_DIAG "Build diagnosis (TinyML t1/t3 fault-type) inference feature" ON)

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
        src/ui/PositionNames.h
        src/ui/PositionTimingModel.h
        src/ui/PositionTimingModel.cpp
        src/ui/PositionSequenceController.h
        src/ui/PositionSequenceController.cpp
        src/ui/PositionChangeDialog.h
        src/ui/PositionChangeDialog.cpp
        src/ui/SequenceResultSaveDialog.h
        src/ui/SequenceResultSaveDialog.cpp
        src/ui/cloud/MeasurementUploadClient.h
        src/ui/cloud/MeasurementUploadClient.cpp
        src/ui/VideoSplashScreen.h
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
        src/core/stats/RollingOutlier.h
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
        # [engine/watchdog · QA-US-01] cross-cutting 워치독 + 이벤트/알림 (SOLID, 확장형)
        src/engine/watchdog/WatchdogEvent.h
        src/engine/watchdog/IEventSink.h
        src/engine/watchdog/IWatchdogCheck.h
        src/engine/watchdog/WatchdogContext.h
        src/engine/watchdog/WatchdogState.h
        src/engine/watchdog/WatchdogClock.h
        src/engine/watchdog/WatchdogWorker.h
        src/engine/watchdog/WatchdogWorker.cpp
        src/engine/watchdog/DevicePresenceMonitor.h
        src/engine/watchdog/DevicePresenceMonitor.cpp
        src/engine/watchdog/EventHandler.h
        src/engine/watchdog/EventHandler.cpp
        src/engine/watchdog/checks/AudioDeviceTimeoutCheck.h
        src/engine/watchdog/checks/AudioDeviceTimeoutCheck.cpp
        src/engine/watchdog/checks/NoSignalTimeoutCheck.h
        src/engine/watchdog/checks/NoSignalTimeoutCheck.cpp
        src/engine/watchdog/checks/CameraDisconnectCheck.h
        src/engine/watchdog/checks/CameraDisconnectCheck.cpp
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
        src/ui/tabs/RadarChartWidget.h
        src/ui/tabs/RadarChartWidget.cpp
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
        src/ui/WarmupOverlay.h
        src/ui/WarmupOverlay.cpp
        # [diag UI] 진단 결과 배너 + 상세 가이드 창 (자기완결 · diag 토글과 무관하게 컴파일)
        src/ui/diag/DiagCatalog.h
        src/ui/diag/DiagCatalog.cpp
        src/ui/diag/DiagBanner.h
        src/ui/diag/DiagBanner.cpp
        src/ui/diag/DiagDetailDialog.h
        src/ui/diag/DiagDetailDialog.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/watchdog
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/watchdog/checks
)

# [ui 모듈] MainWindow 헤더 탐색 경로 ("MainWindow.h" + "tabs/..." 접두사 include 해결)
target_include_directories(TimeGrapher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::PrintSupport)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Multimedia)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::MultimediaWidgets)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::QuickWidgets)
target_link_libraries(TimeGrapher PRIVATE Qt${QT_VERSION_MAJOR}::Network)

# ──────────────────────────────────────────────────────────────────────────────
# [vision · TinyML] Watch-position 추론 — TFLite C API 런타임 동적 로드.
#   - Windows : prebuilt tensorflowlite_c.dll 자동 다운로드(공개된 ValYouW/tflite-dist package)
#   - Linux/RPi5 : TensorFlow 소스(핀 v2.18.0)에서 libtensorflowlite_c.so 빌드(FetchContent)
#   - 클래스 라벨은 VisionConfig.h 에 직접 정의 / model.tflite → .qrc 임베드 (런타임 외부파일 0)
# ─────────────────────────────────────────────────────────────────────────
if(ENABLE_VISION OR ENABLE_DIAG)
    include(FetchContent)
    set(_TFLITE_VERSION "2.18.0")

  if(ENABLE_VISION)
    # ── 설정값 → VisionConfig.h 생성 (클래스 라벨은 헤더에 직접 정의됨) ──
    set(VISION_IMG_SIZE       192)
    set(VISION_CROP_X1        448)
    set(VISION_CROP_Y1        168)
    set(VISION_CROP_X2        832)
    set(VISION_CROP_Y2        552)
    set(VISION_CAM_WIDTH      1280)
    set(VISION_CAM_HEIGHT     720)
    set(VISION_CAMERA_ID      0)
    set(VISION_CONF_THRESH    0.6)
    set(VISION_NUM_THREADS    2)
    set(VISION_CLAHE_CLIP     2.0)
    set(VISION_CLAHE_TILES    8)
    set(VISION_INTERVAL_MS    1000)
    set(VISION_USE_INT8       TRUE)

    # ── 모델 선택 토글 ────────────────────────────────────────────────────
    #   TRUE  → int8 완전 정수 양자화 모델(model_vision_int8.tflite) — 더 작고 빠름(엣지/RPi5 권장)
    #   FALSE → fp32 모델(model_vision.tflite)
    #   추론 코드(TfliteApi)는 입력/출력 텐서 타입을 런타임 판별해 양자화/역양자화를
    #   자동 처리하므로, 이 토글만 바꾸면 양쪽 모두 그대로 동작한다.

    if(VISION_USE_INT8)
        set(VISION_MODEL_FILE "model_vision_int8.tflite")
        set(VISION_USE_INT8_CPP "true")
    else()
        set(VISION_MODEL_FILE "model_vision.tflite")
        set(VISION_USE_INT8_CPP "false")
    endif()
    set(VISION_MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml/${VISION_MODEL_FILE}")
    set(VISION_MODEL_RESOURCE ":/vision/model.tflite")   # qrc alias 고정 → 런타임 경로 불변
    message(STATUS "[vision] model = ${VISION_MODEL_FILE} (VISION_USE_INT8=${VISION_USE_INT8})")

    set(_vision_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/vision")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml/VisionConfig.h.in"
        "${_vision_gen_dir}/VisionConfig.h"
        @ONLY)
    # 선택된 모델 파일을 임베드하도록 qrc 를 설정 시점에 생성(절대경로 사용).
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml/vision.qrc.in"
        "${_vision_gen_dir}/vision.qrc"
        @ONLY)
  endif()

  # [diag] t1/t3 고장유형 진단 — DiagConfig.h + diag.qrc 생성(fp32 모델 임베드, 외부파일 0).
  if(ENABLE_DIAG)
    set(DIAG_SAMPLE_RATE      8000)
    set(DIAG_SEQUENCE_LENGTH  64)
    set(DIAG_NUM_FEATURES     14)
    set(DIAG_WINDOW           64)
    set(DIAG_HOP              32)
    set(DIAG_CONF_THRESH      0.6)
    set(DIAG_NUM_THREADS      2)
    set(DIAG_MODEL_PATH       "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml/model_diag.tflite")
    set(DIAG_MODEL_RESOURCE   ":/tinyml/model_diag.tflite")
    message(STATUS "[diag] model = model_diag.tflite (fp32)")

    set(_diag_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/diag")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml/DiagConfig.h.in"
        "${_diag_gen_dir}/DiagConfig.h"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml/diag.qrc.in"
        "${_diag_gen_dir}/diag.qrc"
        @ONLY)
  endif()

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
        # [vision · KleidiAI] XNNPACK(TFLite 의존)은 빌드 중 gitlab.arm.com에서 KleidiAI
        #  archive zip을 받아 SHA256을 검증한다. 그러나 GitLab archive zip은 바이트 재현이
        #  보장되지 않아 시간이 지나면 해시가 바뀌고(기대 88233e42… ↔ 실제 5d4fe6b1…),
        #  검증이 실패하면 압축을 풀지 않아 build/kleidiai-source 가 비어 add_subdirectory 가
        #  깨진다. XNNPACK 은 KLEIDIAI_SOURCE_DIR 가 정의돼 있으면 다운로드 단계를 건너뛰므로
        #  (xnnpack/CMakeLists.txt: "NOT DEFINED KLEIDIAI_SOURCE_DIR"), 저장소에 동결해 둔
        #  로컬 소스(third_party/kleidiai-source, XNNPACK 핀 커밋 cddf991)를 지정한다.
        #  → Qt Creator·CLI(build_run.sh) 어느 쪽으로 configure 해도 동일하게 동작한다.
        set(_KLEIDIAI_VENDORED "${CMAKE_CURRENT_SOURCE_DIR}/third_party/kleidiai-source")
        if(EXISTS "${_KLEIDIAI_VENDORED}/CMakeLists.txt")
            set(KLEIDIAI_SOURCE_DIR "${_KLEIDIAI_VENDORED}"
                CACHE STRING "Vendored KleidiAI source (skip XNNPACK gitlab download)" FORCE)
        endif()

        # [vision · FlatBuffers] TFLite 는 flatbuffers 소스를 ${CMAKE_BINARY_DIR}/flatbuffers 로
        #  내려받은 뒤, flatbuffers.cmake 가 CMAKE_MODULE_PATH 맨 앞에 ${소스}/CMake 를 끼워넣는다.
        #  그 안의 flatbuffers 자체 FindFlatBuffers.cmake 가 이후 find_package(FlatBuffers REQUIRED)
        #  에서 잡히는데, 이 모듈은 find_path(FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flatbuffers.h)
        #  를 '시스템 경로'에서만 찾는다. 라즈베리파이처럼 libflatbuffers-dev 가 없는 호스트에서는
        #  NOTFOUND → "Could NOT find FlatBuffers (missing: FLATBUFFERS_INCLUDE_DIR)" 로 configure 실패
        #  (헤더가 설치된 x86 데스크톱에서는 우연히 통과). find_path 는 캐시에 값이 이미 있으면 재탐색하지
        #  않으므로, fetch 될 소스의 include 경로를 미리 지정해 시스템 탐색을 건너뛴다(Qt Creator·CLI 공통).
        set(FLATBUFFERS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/flatbuffers/include"
            CACHE PATH "Fetched FlatBuffers headers (avoid system find_path on hosts w/o libflatbuffers-dev)" FORCE)

        FetchContent_Declare(tensorflow
            GIT_REPOSITORY https://github.com/tensorflow/tensorflow.git
            GIT_TAG        "v${_TFLITE_VERSION}"
            GIT_SHALLOW    TRUE)
        FetchContent_GetProperties(tensorflow)
        if(NOT tensorflow_POPULATED)
            FetchContent_Populate(tensorflow)

            # [vision · Protobuf] TFLite 의 protobuf.cmake 는 flatbuffers.cmake 와 달리 재진입
            #  가드가 없다. find_package(Protobuf) 호출자는 두 곳인데(core/example 는 NOT TARGET
            #  가드가 있지만 lite/profiling/proto 는 무가드), 무가드 경로가 protobuf.cmake 를 다시
            #  include → 같은 소스(${BINARY}/protobuf)를 _deps/protobuf-build 로 add_subdirectory
            #  두 번째로 시도 → "binary directory ... is already used to build a source directory"
            #  로 configure 실패한다. fetched 모듈 맨 앞에 멱등 재진입 가드를 주입한다(이미 주입돼
            #  있으면 건너뜀 — clean 빌드마다 재적용되므로 Qt Creator·CLI 모두 동일하게 동작).
            set(_pb_module
                "${tensorflow_SOURCE_DIR}/tensorflow/lite/tools/cmake/modules/protobuf.cmake")
            if(EXISTS "${_pb_module}")
                file(READ "${_pb_module}" _pb_contents)
                if(NOT _pb_contents MATCHES "TimeGrapher protobuf re-entry guard")
                    file(WRITE "${_pb_module}"
"# [TimeGrapher protobuf re-entry guard] 같은 protobuf 소스를 두 번 add_subdirectory 하는 것을 막는다.\nif(TARGET protobuf::libprotobuf)\n  set(Protobuf_FOUND TRUE)\n  return()\nendif()\n${_pb_contents}")
                endif()
            endif()

            # [vision · abseil/cctz] 이 프로젝트는 위쪽에서 CMAKE_INCLUDE_CURRENT_DIR 를 전역 ON 으로
            #  켜는데(Qt 편의), 이 값이 add_subdirectory 로 들어가는 abseil 하위 디렉토리까지 전파된다.
            #  abseil 의 time 라이브러리(cctz 포함)는 ${...}/abseil-cpp/absl/time/CMakeLists.txt 에서
            #  정의되므로, INCLUDE_CURRENT_DIR 가 -I.../absl/time 를 추가한다. 그 디렉토리에는 abseil 의
            #  absl/time/time.h 가 있어, cctz 소스의 #include <ctime> → <time.h> 가 시스템 헤더 대신
            #  absl/time/time.h 를 잡는다 → clock/time/mktime 등 표준 심볼 누락:
            #  "'clock' has not been declared in '::'". 따라서 TFLite/abseil 를 add_subdirectory 하는
            #  동안만 INCLUDE_CURRENT_DIR 를 끄고(서드파티는 target_include_directories 로 자급) 복원한다.
            set(_saved_include_current_dir ${CMAKE_INCLUDE_CURRENT_DIR})
            set(CMAKE_INCLUDE_CURRENT_DIR OFF)

            # [vision · XNNPACK / ARM FP16 vector] 타깃 Pi 5(Cortex-A76, ARMv8.2-A)는 네이티브 fp16
            #  연산을 지원하므로 XNNPACK_ENABLE_ARM_FP16_VECTOR 를 'ON'(기본값)으로 유지한다.
            #  ※ 이 옵션을 OFF 로 내리면 오히려 빌드가 깨진다: TFLite 2.18 이 핀한 XNNPACK 의
            #    src/configs/cmul-config.c 는 fp16 복소수곱 커널 xnn_f16_vcmul_ukernel__neonfp16arith_u16
            #    을 '#if XNN_ARCH_ARM || XNN_ARCH_ARM64'(아키텍처)로만 참조하는데, 그 '선언'
            #    (src/f16-vbinary/f16-vcmul.h)은 '#if XNN_ENABLE_ARM_FP16_VECTOR && ARM/ARM64' 로
            #    가드돼 있다(XNNPACK 의 가드 불일치 버그). 옵션을 끄면 선언만 사라지고 참조는 남아
            #    "'xnn_f16_vcmul_ukernel__neonfp16arith_u16' undeclared" 하드 에러가 난다. 따라서 ON 유지가
            #    정답이며, 아래 GCC14 진단 완화로 fp16 커널 소스의 컴파일 에러만 잡으면 빌드가 통과한다.
            #    (직전 시도에서 캐시에 OFF 가 FORCE 로 박혔을 수 있어, 잔존 캐시를 확실히 덮도록 ON 을 FORCE.)
            set(XNNPACK_ENABLE_ARM_FP16_VECTOR ON CACHE BOOL "Keep ON: XNNPACK v2.18 cmul-config refs f16 vcmul unguarded" FORCE)
            set(XNNPACK_ENABLE_ARM_FP16_SCALAR ON CACHE BOOL "Keep ON for ARMv8.2 native fp16 (Pi 5)" FORCE)

            # [vision · XNNPACK / GCC 14] GCC 14 는 과거 '경고'였던 -Wincompatible-pointer-types /
            #  -Wint-conversion 등을 기본 '에러'로 승격했는데, XNNPACK v2.18 의 fp16 커널이 fp16 값을
            #  uint16_t* 로 다루는 부분에서 "passing argument 1 of 'vld1q_dup_u16' from incompatible
            #  pointer type" 류 에러가 난다(XNNPACK 자체엔 -Werror 없음 → 순수 GCC14 기본동작).
            #  XNNPACK 은 fetch 되는 서드파티라 소스를 고치지 않고, TFLite/XNNPACK add_subdirectory
            #  구간에서만 이 진단을 다시 '경고'로 되돌린다(C 한정). 이후 복원하므로 본체에는 영향 없다.
            set(_saved_c_flags "${CMAKE_C_FLAGS}")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=incompatible-pointer-types -Wno-error=int-conversion")

            add_subdirectory(
                "${tensorflow_SOURCE_DIR}/tensorflow/lite/c"
                "${CMAKE_CURRENT_BINARY_DIR}/tflite-c-build")

            set(CMAKE_C_FLAGS "${_saved_c_flags}")
            set(CMAKE_INCLUDE_CURRENT_DIR ${_saved_include_current_dir})
        endif()
        set(VISION_TFLITE_INCLUDE_DIR "${tensorflow_SOURCE_DIR}")
        set(VISION_TFLITE_RUNTIME     "$<TARGET_FILE:tensorflowlite_c>")
        add_dependencies(TimeGrapher tensorflowlite_c)   # 빌드 의존성만(링크 X — 런타임 dlopen)
    endif()

    # TFLite C API 동적 로더는 vision/diag 가 공유한다.
    target_sources(TimeGrapher PRIVATE
        src/tinyml/TfliteApi.h
        src/tinyml/TfliteApi.cpp)
    target_include_directories(TimeGrapher PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tinyml"
        "${VISION_TFLITE_INCLUDE_DIR}")
    target_link_libraries(TimeGrapher PRIVATE ${CMAKE_DL_LIBS})

  if(ENABLE_VISION)
    target_sources(TimeGrapher PRIVATE
        src/tinyml/VisionPreprocess.h
        src/tinyml/VisionPreprocess.cpp
        src/tinyml/VisionWorker.h
        src/tinyml/VisionWorker.cpp
        "${_vision_gen_dir}/vision.qrc")
    target_include_directories(TimeGrapher PRIVATE "${_vision_gen_dir}")
    target_compile_definitions(TimeGrapher PRIVATE ENABLE_VISION=1)
  endif()

  if(ENABLE_DIAG)
    target_sources(TimeGrapher PRIVATE
        src/tinyml/DiagPreprocess.h
        src/tinyml/DiagPreprocess.cpp
        src/tinyml/DiagWorker.h
        src/tinyml/DiagWorker.cpp
        "${_diag_gen_dir}/diag.qrc")
    target_include_directories(TimeGrapher PRIVATE "${_diag_gen_dir}")
    target_compile_definitions(TimeGrapher PRIVATE ENABLE_DIAG=1)
  endif()

    # 런타임 라이브러리를 실행파일 옆에 복사(dlopen 탐색 경로).
    add_custom_command(TARGET TimeGrapher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VISION_TFLITE_RUNTIME}" "$<TARGET_FILE_DIR:TimeGrapher>"
        COMMENT "[vision] copy TFLite runtime next to executable")

    if(NOT WIN32)
        set_target_properties(TimeGrapher PROPERTIES
            BUILD_RPATH   "$ORIGIN"
            INSTALL_RPATH "$ORIGIN")
    endif()
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
# [개발 편의] 콘솔 서브시스템 빌드 옵션 — 기본 OFF.
#  ON(=build.ps1 -Console)이면 Windows GUI 서브시스템 대신 콘솔로 빌드되어 qInfo/qDebug 가
#  실행 터미널에 출력된다. 기본/배포/Qt Creator 빌드는 OFF 이라 영향 없음(기존과 동일한 GUI exe).
option(CONSOLE_BUILD "Build as console subsystem so qInfo/qDebug print to the terminal" OFF)
if(CONSOLE_BUILD)
    set(TG_WIN32_EXECUTABLE FALSE)
else()
    set(TG_WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(TimeGrapher PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE ${TG_WIN32_EXECUTABLE}
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
