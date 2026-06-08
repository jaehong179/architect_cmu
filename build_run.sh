#!/usr/bin/env bash
#
# build_run.sh — CMake/Qt 프로젝트 빌드 & 실행 스크립트
#
set -euo pipefail

# ---- 설정: Qt / 도구 경로 ---------------------------------------------------
QT_PREFIX="/home/lg/Qt/6.11.1/gcc_arm64"
if [[ -x "/home/lg/Qt/Tools/CMake/bin/cmake" ]]; then
    CMAKE="/home/lg/Qt/Tools/CMake/bin/cmake"
else
    CMAKE="$(command -v cmake || true)"
fi

# ---- 도움말 ----------------------------------------------------------------
usage() {
cat <<'EOF'
┌──────────────────────────────────────────────────────────────┐
│  build_run.sh — CMake/Qt 프로젝트 빌드 & 실행                  │
└──────────────────────────────────────────────────────────────┘

사용법:
  build_run.sh <소스경로> [옵션]
  build_run.sh help              ← 이 가이드 표시

인자:
  <소스경로>   CMakeLists.txt 가 있는 디렉토리 (생략 시 현재 디렉토리)

옵션:
  -c, --clean        빌드 디렉토리를 지우고 새로 구성
  -d, --debug        Debug 빌드 (기본: Release)
  -n, --no-run       빌드만 하고 실행하지 않음
  -b, --build-dir D  빌드 디렉토리 지정 (기본: <소스경로>/build)
  -t, --target NAME  실행할 타겟/바이너리 이름 (기본: 자동 감지)
      --display D    실행에 사용할 DISPLAY (기본: 자동 감지, 보통 :0)
  -h, --help, help   이 가이드 표시

예시:
  # 빌드 후 자동 실행
  build_run.sh /home/lg/Desktop/TimeGrapher_Perf/architect_cmu

  # 깨끗이 다시 빌드만 (실행 안 함)
  build_run.sh /path/to/proj --clean --no-run

  # 현재 디렉토리에서 Debug 빌드
  cd /path/to/proj && build_run.sh -d

동작:
  1) <소스경로>/CMakeLists.txt 확인
  2) cmake 구성 (캐시 없을 때만, Qt: /home/lg/Qt/6.11.1/gcc_arm64)
  3) cmake --build --parallel (Ninja)
  4) 실행 파일 자동 감지 후 DISPLAY 잡아서 실행
EOF
}

# ---- 인자 파싱 --------------------------------------------------------------
SRC=""
BUILD_DIR=""
BUILD_TYPE="Release"
DO_RUN=1
DO_CLEAN=0
TARGET=""
DISPLAY_ARG=""

# 인자 없이 실행하면 가이드 표시
if [[ $# -eq 0 ]]; then usage; exit 0; fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        help|-h|--help) usage; exit 0 ;;
        -c|--clean)     DO_CLEAN=1; shift ;;
        -d|--debug)     BUILD_TYPE="Debug"; shift ;;
        -n|--no-run)    DO_RUN=0; shift ;;
        -b|--build-dir) BUILD_DIR="$2"; shift 2 ;;
        -t|--target)    TARGET="$2"; shift 2 ;;
        --display)      DISPLAY_ARG="$2"; shift 2 ;;
        -*)             echo "알 수 없는 옵션: $1 (도움말: build_run.sh help)"; exit 1 ;;
        *)              SRC="$1"; shift ;;
    esac
done

# ---- 사전 점검 --------------------------------------------------------------
[[ -z "${CMAKE:-}" ]] && { echo "오류: cmake 를 찾을 수 없습니다."; exit 1; }

SRC="${SRC:-$PWD}"
SRC="$(cd "$SRC" 2>/dev/null && pwd)" || { echo "오류: 소스 경로가 존재하지 않습니다."; exit 1; }
[[ -f "$SRC/CMakeLists.txt" ]] || { echo "오류: $SRC 에 CMakeLists.txt 가 없습니다."; exit 1; }
BUILD_DIR="${BUILD_DIR:-$SRC/build}"

echo "==> 소스      : $SRC"
echo "==> 빌드 디렉 : $BUILD_DIR"
echo "==> 빌드 타입 : $BUILD_TYPE"
echo "==> cmake     : $CMAKE"

# ---- 클린 -------------------------------------------------------------------
if [[ "$DO_CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "==> 클린: $BUILD_DIR 삭제"
    rm -rf "$BUILD_DIR"
fi

# ---- 구성(configure): 캐시가 없을 때만 ------------------------------------
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "==> CMake 구성"
    "$CMAKE" -S "$SRC" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX"
fi

# ---- 빌드 -------------------------------------------------------------------
echo "==> 빌드 시작"
"$CMAKE" --build "$BUILD_DIR" --parallel
echo "==> 빌드 완료"

[[ "$DO_RUN" -eq 0 ]] && { echo "==> --no-run 지정: 실행 생략"; exit 0; }

# ---- 실행 타겟(바이너리) 결정 ----------------------------------------------
BIN=""
if [[ -n "$TARGET" ]]; then
    BIN="$BUILD_DIR/$TARGET"
else
    PROJ="$(grep -m1 -oP 'project\(\s*\K[A-Za-z0-9_]+' "$SRC/CMakeLists.txt" || true)"
    if [[ -n "$PROJ" && -x "$BUILD_DIR/$PROJ" ]]; then
        BIN="$BUILD_DIR/$PROJ"
    else
        BIN="$(find "$BUILD_DIR" -maxdepth 1 -type f -executable -printf '%T@ %p\n' \
               | sort -rn | head -1 | cut -d' ' -f2-)"
    fi
fi
[[ -n "$BIN" && -x "$BIN" ]] || { echo "오류: 실행 파일을 찾지 못했습니다."; exit 1; }
echo "==> 실행 파일 : $BIN"

# ---- DISPLAY 자동 감지 (GUI 앱) --------------------------------------------
if [[ -z "$DISPLAY_ARG" ]]; then
    if [[ -n "${DISPLAY:-}" ]]; then
        DISPLAY_ARG="$DISPLAY"
    elif ls /tmp/.X11-unix/X* >/dev/null 2>&1; then
        DISPLAY_ARG=":$(ls /tmp/.X11-unix/ | head -1 | tr -d 'X')"
    else
        DISPLAY_ARG=":0"
    fi
fi
export DISPLAY="$DISPLAY_ARG"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
echo "==> DISPLAY   : $DISPLAY"

# ---- 실행 (perf_log.csv 는 실행파일 옆에 생성됨) ---------------------------
#  앱은 Perf::init() 에서 실행파일이 있는 디렉토리(applicationDirPath)에
#  perf_log.csv 를 새로 쓴다 (PerfInstrumentation.cpp). 즉 실행한 작업 디렉토리와
#  무관하게 항상 바이너리 옆에 생성된다 → CSV 위치가 어떤 실행 경로에서도 일정.
#  (build 디렉토리는 .gitignore 에 있어 커밋 대상 아님).
PERF_CSV="$(dirname "$BIN")/perf_log.csv"
echo "==> 실행 (작업 디렉토리: $SRC)"
echo "==> perf CSV  : $PERF_CSV (앱 종료 시 생성/갱신)"

# exec 로 셸을 대체하지 않고 일반 실행 → 종료 후 CSV 를 확인·보존할 수 있게 함
cd "$SRC"
"$BIN" "$@"
RUN_RC=$?

# ---- perf CSV 결과 보고 + 실행별 타임스탬프 보존 --------------------------
if [[ -f "$PERF_CSV" ]]; then
    STAMP="$(date +%Y%m%d_%H%M%S)"
    ARCHIVE="$(dirname "$BIN")/perf_log_${STAMP}.csv"
    cp -f "$PERF_CSV" "$ARCHIVE"
    LINES="$(wc -l < "$PERF_CSV" | tr -d ' ')"
    echo "==> perf CSV 생성됨 : $PERF_CSV (${LINES} 줄)"
    echo "==> 보존본          : $ARCHIVE"
else
    echo "경고: perf_log.csv 가 생성되지 않았습니다 ($PERF_CSV)"
fi

exit "$RUN_RC"
