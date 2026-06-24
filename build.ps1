<#
  build.ps1 — TimeGrapher 빌드 스크립트 (Windows / Qt MinGW)

  VSCode 통합 터미널(또는 PowerShell)에서:
      .\build.ps1                 # build_cli 에 Release 빌드 (없으면 자동 configure)
      .\build.ps1 -Clean          # build_cli 지우고 새로 빌드
      .\build.ps1 -Run            # 빌드 후 실행
      .\build.ps1 -Deploy         # windeployqt 로 Qt DLL 동봉(단독 실행/배포용)
      .\build.ps1 -Console -Run   # 콘솔 빌드(build_console) + 실행 → 로그가 이 터미널에 출력
      .\build.ps1 -QtVersion 6.11.1
      .\build.ps1 -QtRoot "D:\Qt"

  -Console 은 build_console 디렉터리에 콘솔 서브시스템으로 빌드한다(qInfo/qDebug가 터미널 출력).
  기본 build_cli / Qt Creator / 배포 빌드는 GUI 그대로(영향 없음).

  절대경로를 박지 않고 Qt 설치를 자동탐지하므로 그대로 커밋·공유 가능.
  탐지 우선순위: 매개변수 > 환경변수(QT_ROOT) > 기본값 C:\Qt.
  Qt Creator 빌드와 무관(별도 디렉터리 build_cli).
#>
param(
    [string]$QtRoot    = $(if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\Qt" }),
    [string]$QtVersion = "",                 # 비우면 최신 6.x 자동 선택
    [ValidateSet("Release","Debug","RelWithDebInfo")]
    [string]$Config    = "Release",
    [switch]$Clean,
    [switch]$Deploy,
    [switch]$Run,
    [switch]$Console                          # 콘솔 서브시스템 빌드(qInfo/qDebug 가 터미널에 출력)
)
$ErrorActionPreference = "Stop"
$root     = $PSScriptRoot
# -Console 은 별도 디렉터리에 빌드 → 기본 build_cli / Qt Creator / 배포 빌드와 완전 분리.
$buildDir = if ($Console) { Join-Path $root "build_console" } else { Join-Path $root "build_cli" }

function Find-QtMingw {
    param([string]$Root, [string]$Version)
    if (-not (Test-Path $Root)) { throw "Qt 설치 경로를 찾을 수 없음: $Root  (-QtRoot 또는 QT_ROOT 설정)" }
    if ($Version) {
        $cand = Join-Path $Root "$Version\mingw_64"
        if (Test-Path $cand) { return $cand }
        throw "Qt $Version mingw_64 없음: $cand"
    }
    $vers = Get-ChildItem -Path $Root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^6\.' } |
            Sort-Object { [version]$_.Name } -Descending
    foreach ($v in $vers) {
        $m = Join-Path $v.FullName "mingw_64"
        if (Test-Path $m) { return $m }
    }
    throw "Qt 6.x\mingw_64 키트를 $Root 아래에서 못 찾음. -QtVersion 또는 -QtRoot 지정."
}

function Find-Tool {
    param([string]$PreferredExe, [string]$FallbackName)
    if ($PreferredExe -and (Test-Path $PreferredExe)) { return $PreferredExe }
    $cmd = Get-Command $FallbackName -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "$FallbackName 를 찾을 수 없음 (Qt Tools 또는 PATH 확인)"
}

# ── 툴체인 자동탐지 ──
$qtMingw  = Find-QtMingw -Root $QtRoot -Version $QtVersion
$qtBin    = Join-Path $qtMingw "bin"
$tools    = Join-Path $QtRoot "Tools"

$mingwDir = Get-ChildItem -Path $tools -Directory -Filter "mingw*_64" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
$mingwBin = if ($mingwDir) { Join-Path $mingwDir.FullName "bin" } else { "" }

$cmake = Find-Tool (Join-Path $tools "CMake_64\bin\cmake.exe") "cmake"
$ninja = Find-Tool (Join-Path $tools "Ninja\ninja.exe")       "ninja"
$ninjaDir = Split-Path $ninja -Parent
$cmakeDir = Split-Path $cmake -Parent

# 빌드/실행에 필요한 경로를 세션 PATH 앞에 추가
$env:PATH = "$qtBin;$mingwBin;$ninjaDir;$cmakeDir;$env:PATH"

Write-Host "[build] Qt      : $qtMingw"      -ForegroundColor Cyan
Write-Host "[build] CMake   : $cmake"        -ForegroundColor Cyan
Write-Host "[build] Config  : $Config"       -ForegroundColor Cyan
Write-Host "[build] OutDir  : $buildDir"     -ForegroundColor Cyan

# ── clean ──
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "[build] clean: removing $buildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

# ── configure (캐시 없으면) ──
if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host "[build] configuring... (console=$Console)" -ForegroundColor Green
    $consoleFlag = if ($Console) { "ON" } else { "OFF" }
    # 인자는 따옴표 배열로 — 비따옴표 -D...=$var 는 PowerShell 에서 확장이 안 될 수 있다.
    $cfgArgs = @(
        "-S", "$root",
        "-B", "$buildDir",
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_PREFIX_PATH=$qtMingw",
        "-DCONSOLE_BUILD=$consoleFlag"
    )
    & $cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}

# ── build (CMakeLists 변경 시 Ninja 자동 재구성) ──
Write-Host "[build] building..." -ForegroundColor Green
& $cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build 실패" }

$exe = Join-Path $buildDir "TimeGrapher.exe"
Write-Host "[build] OK -> $exe" -ForegroundColor Green

# ── deploy (선택): Qt DLL 동봉 → 단독 실행/배포 가능 ──
if ($Deploy) {
    $wdq = Join-Path $qtBin "windeployqt.exe"
    if (Test-Path $wdq) { Write-Host "[build] windeployqt..." -ForegroundColor Green; & $wdq $exe }
    else { Write-Warning "windeployqt.exe 없음 — deploy 건너뜀" }
}

# ── run (선택) ──
if ($Run) {
    Write-Host "[build] running..." -ForegroundColor Green
    & $exe
}
