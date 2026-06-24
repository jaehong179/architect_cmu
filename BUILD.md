# 빌드 방법 (Build)

Windows에서 `build.ps1` 스크립트로 빌드/실행합니다. 절대경로를 박지 않고 Qt 설치를 자동탐지하므로 누구나 `git pull` 후 바로 쓸 수 있습니다.

---

## 사전 조건 (각 PC에 1회 설치)

- **Qt 6.x (MinGW 64-bit) + CMake + Ninja + MinGW 컴파일러**
  - Qt 온라인 설치관리자 → "Qt 6.x.x → MinGW 64-bit", 그리고 "Developer and Designer Tools"에서 **CMake / Ninja / MinGW** 체크.
  - 기본 설치 위치 `C:\Qt` 면 추가 설정 불필요. 다른 곳이면 아래 *Qt 경로 지정* 참고.
- **PowerShell 실행 정책 허용** (스크립트 실행용, 1회):
  ```powershell
  Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
  ```
  (또는 매번 `powershell -ExecutionPolicy Bypass -File .\build.ps1 ...` 로 실행)

> Linux/macOS는 이 스크립트(PowerShell/MinGW 전용) 대상이 아닙니다.

---

## 빠른 시작

VSCode 통합 터미널(또는 PowerShell)에서 `architect_cmu` 폴더로 이동 후:

```powershell
.\build.ps1            # build_cli 에 Release 빌드 (없으면 자동 configure)
.\build.ps1 -Run       # 빌드 후 실행
```

성공하면 `[build] OK -> ...\build_cli\TimeGrapher.exe` 가 출력됩니다.

---

## 옵션

| 명령 | 동작 |
|---|---|
| `.\build.ps1` | `build_cli` 에 빌드 (Release, GUI) |
| `.\build.ps1 -Run` | 빌드 후 실행 |
| `.\build.ps1 -Clean` | `build_cli` 삭제 후 새로 빌드 |
| `.\build.ps1 -Deploy` | `windeployqt` 로 Qt DLL 동봉(단독 실행/배포용) |
| `.\build.ps1 -Console -Run` | **콘솔 빌드** 후 실행 → 로그(qInfo/qDebug)가 터미널에 출력 |
| `.\build.ps1 -Config Debug` | Debug 구성으로 빌드 |

---

## 로그 보기 (워치독 등)

앱은 기본적으로 GUI 서브시스템이라 `qInfo`/`qDebug`(예: `[watchdog] ...`)가 **터미널에 안 나옵니다.** 로그를 터미널에서 보려면:

```powershell
.\build.ps1 -Console -Run
```

- `-Console` 은 **별도 디렉터리 `build_console`** 에 콘솔 서브시스템으로 빌드합니다(CMake 옵션 `CONSOLE_BUILD=ON`).
- 기본 `build_cli` / Qt Creator / 배포 빌드는 **GUI 그대로 영향 없음**.

---

## Qt 경로 지정 (C:\Qt 가 아닐 때)

```powershell
# 방법 1) 환경변수 (한 번만)
$env:QT_ROOT = "D:\Qt"

# 방법 2) 인자
.\build.ps1 -QtRoot "D:\Qt"
.\build.ps1 -QtVersion 6.11.1      # 특정 버전 강제(미지정 시 최신 6.x 자동)
```

탐지 우선순위: `-QtRoot`/`-QtVersion` 인자 > `QT_ROOT` 환경변수 > 기본값 `C:\Qt`.

---

## VSCode에서 F5로 빌드+실행 (선택)

`.vscode/launch.json` + `tasks.json` 을 두면 **F5 한 번에 빌드 후 실행**됩니다. 단 이 파일들은 `.gitignore` 대상(개인 설정)이라 공유되지 않습니다. 필요하면 각자 만들거나 팀 리드에게 요청하세요. (없어도 터미널 `.\build.ps1` 로 동일하게 빌드 가능)

---

## Qt Creator 병행

Qt Creator로도 평소처럼 열고 빌드·실행할 수 있습니다. `build.ps1` 은 **별도 디렉터리(`build_cli`/`build_console`)** 를 쓰고 소스/`CMakeLists.txt` 빌드 정의를 바꾸지 않으므로 서로 간섭하지 않습니다.

---

## 트러블슈팅

- **"스크립트를 실행할 수 없으므로..."** → 실행 정책. 위 *사전 조건*의 `Set-ExecutionPolicy` 또는 `powershell -ExecutionPolicy Bypass -File .\build.ps1`.
- **"Qt 6.x\mingw_64 를 못 찾음"** → Qt 설치 위치가 다름. `QT_ROOT` 지정 또는 `-QtRoot`.
- **`cmake`/`ninja` 못 찾음** → Qt 설치 시 "Developer and Designer Tools"(CMake/Ninja/MinGW)를 같이 설치했는지 확인.
- **clean 후에도 이상** → `.\build.ps1 -Clean` (콘솔이면 `-Console -Clean`).
