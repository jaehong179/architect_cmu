# TimeGrapher 왼쪽 제어 패널 UX 개선 통합 계획

이 문서는 기존 `ux_draft1.md`와 `draft2.md`를 통합한 설계 문서입니다. 목표는 TimeGrapher 앱의 왼쪽 제어 패널을 Live / Playback / Sim 모드 중심으로 재구성하여, 사용자가 현재 모드에서 필요한 설정만 보고 조작하도록 만드는 것입니다.

---

## 1. 결론 및 권장 방향

권장 접근은 **Live / Playback / Sim을 1차 선택 축으로 두고, 모드별 소스 설정 영역을 `QStackedWidget`으로 전환하는 방식**입니다.

`ux_draft1.md`의 장점은 현재 UX 문제를 사용자 흐름과 코드 구조 관점에서 잘 분석했다는 점입니다. `draft2.md`의 장점은 `QStackedWidget` 기반 구현 방법이 구체적이라는 점입니다.

따라서 최종 방향은 다음과 같습니다.

- 상위 UX 설계는 `ux_draft1.md`의 **모드 중심 재조합**을 따른다.
- 1단계 구현은 `draft2.md`의 **`QStackedWidget` 기반 동적 모드 전환**을 적용한다.
- 장기적으로는 fixed geometry 기반 `.ui`를 `QVBoxLayout` / `QScrollArea` 기반 구조로 전환한다.
- Sim 모드의 **Sim BPH와 Detector BPH 중복 문제**를 명확히 해결한다.
- Playback 모드는 Start 클릭 후 파일 선택이 아니라, 패널에서 미리 WAV 파일을 선택하는 UX로 개선한다.

---

## 2. 현재 상태 분석

현재 `MainWindow.ui` 기준 왼쪽 패널은 고정 너비 242px 영역에 여러 프레임이 항상 표시되는 구조입니다.

| 프레임 | 주요 위젯 | 역할 |
|--------|-----------|------|
| Run Parameters | Mode, Gain, Input Device, Sample Rate, Averaging, Start / Stop | 입력 소스 선택 및 실행 |
| Watch Parameters | BPH, Lift Angle | 검출기 및 측정 엔진 설정 |
| Simulation Parameters | Sim BPH, Error Rate, Amplitude, Beat Error, Realistic | Sim 전용 합성 파라미터 |
| Misc. Parameters | High Pass Cutoff, Use Conset | 고급 검출기 튜닝 |

코드 구조는 이미 모드별로 어느 정도 나뉘어 있습니다.

- `LiveStart()`
- `PlaybackStart()`
- `SimStart()`
- `on_ModeComboBox_currentTextChanged()`

하지만 UI는 모드와 무관하게 대부분의 설정을 항상 보여줍니다. 예를 들어 Live 모드에서도 Sim 파라미터가 보이고, Playback 모드에서도 Gain이나 Input Device처럼 의미가 약한 설정이 노출됩니다.

현재 UX의 핵심 문제는 다음과 같습니다.

- 사용자가 현재 모드에서 필요한 설정과 불필요한 설정을 직접 구분해야 한다.
- Sim 모드에서 `BPHComboBox`와 `SimBPHComboBox`가 동시에 존재하여 의미가 혼동된다.
- Playback 파일 선택이 Start 흐름 중간에 나타나 실행 흐름이 분리되어 보이지 않는다.
- `.ui`가 absolute geometry 기반이라 레이아웃 변경이 어렵고 유지보수가 취약하다.

---

## 3. 목표 UX 모델

기존 mental model은 기능별 프레임을 모두 나열하는 구조입니다.

```text
Run Parameters -> Watch Parameters -> Simulation Parameters -> Misc. Parameters
```

목표 mental model은 사용자의 작업 흐름에 맞춘 구조입니다.

```text
어떤 소스로 측정할 것인가?
-> 해당 소스에 필요한 설정
-> 공통 측정 설정
-> 고급 검출기 튜닝
-> Start / Stop
```

즉, 왼쪽 패널의 1차 축은 **기능 종류**가 아니라 **입력 모드**가 되어야 합니다.

---

## 4. 제안 레이아웃

왼쪽 패널은 다음과 같이 재구성합니다.

```text
+------------------------------------------------+
| Mode                                           |
| [ Live | Playback | Sim ]                      |
+------------------------------------------------+
| Source Settings                                |
|                                                |
| Live page:                                     |
|   Input Device                                 |
|   Refresh                                      |
|   Gain                                         |
|   Sample Rate                                  |
|                                                |
| Playback page:                                 |
|   Browse WAV                                   |
|   Selected file                                |
|   Sample Rate from file                        |
|                                                |
| Sim page:                                      |
|   Sim BPH                                      |
|   Error Rate                                   |
|   Amplitude                                    |
|   Beat Error                                   |
|   Realistic                                    |
+------------------------------------------------+
| Measurement                                    |
|   Detector BPH hint                            |
|   Lift Angle                                   |
|   Averaging Period                             |
+------------------------------------------------+
| Advanced / Detector Tuning                     |
|   High Pass Cutoff                             |
|   Use Conset                                   |
+------------------------------------------------+
| [ Start ]                              [ Stop ]|
+------------------------------------------------+
```

### 영역별 원칙

**Mode**

- 현재 `ModeComboBox`를 유지할 수 있다.
- 장기적으로는 `QTabBar` 또는 `QButtonGroup` 기반 세그먼트 버튼이 더 직관적이다.

**Source Settings**

- `QStackedWidget`으로 구성한다.
- Live / Playback / Sim 페이지 중 현재 모드에 해당하는 페이지만 표시한다.
- 사용자가 현재 모드에서 의미 없는 설정을 조작하지 않도록 한다.

**Measurement**

- 세 모드 모두에서 사용하는 측정 설정을 모은다.
- `LiftAngleSpinBox`, `AveragingPeriodComboBox`는 공통으로 유지한다.
- `BPHComboBox`는 Live / Playback에서는 `Detector BPH hint`로 의미를 명확히 한다.
- Sim 모드에서는 Sim BPH와의 관계를 별도 정책으로 정한다.

**Advanced / Detector Tuning**

- 기존 `Misc. Parameters`보다 의미가 분명한 이름을 사용한다.
- `High Pass Cutoff`, `Use Conset`은 초보 사용자에게 부담이 될 수 있으므로 장기적으로 접힘 섹션으로 전환한다.

**Start / Stop**

- 항상 접근 가능한 위치에 둔다.
- 장기적으로는 패널 하단 고정이 가장 자연스럽다.

---

## 5. 모드별 표시 정책

| 항목 | Live | Playback | Sim | 비고 |
|------|:----:|:--------:|:---:|------|
| Input Device / Refresh | 표시 | 숨김 | 숨김 | Playback / Sim은 가상 입력 사용 |
| Gain | 표시 | 숨김 | 숨김 | Live 입력 볼륨 |
| Sample Rate | 표시 | 읽기 전용 | 표시 | Playback은 WAV 파일에서 결정 |
| WAV Browse / Selected File | 숨김 | 표시 | 숨김 | Playback 전용 |
| Sim BPH | 숨김 | 숨김 | 표시 | 합성기의 ground truth |
| Sim Error Rate | 숨김 | 숨김 | 표시 | Sim 전용 |
| Sim Amplitude | 숨김 | 숨김 | 표시 | Sim 전용 |
| Sim Beat Error | 숨김 | 숨김 | 표시 | Sim 전용 |
| Realistic | 숨김 | 숨김 | 표시 | Sim 전용 |
| Detector BPH hint | 표시 | 표시 | 정책 필요 | Sim에서는 자동 연동 권장 |
| Lift Angle | 표시 | 표시 | 표시 | 측정 및 Sim 설정 모두에 필요 |
| Averaging Period | 표시 | 표시 | 표시 | 측정 공통 |
| High Pass Cutoff | 표시 | 표시 | 표시 | 고급 검출기 튜닝 |
| Use Conset | 표시 | 표시 | 표시 | 고급 검출기 튜닝 |

---

## 6. BPH 중복 문제 해결

현재 BPH 관련 UI는 두 가지입니다.

| UI | 의미 | 사용처 |
|----|------|--------|
| `BPHComboBox` | Detector가 예상하는 BPH hint | `pushCaptureConfig()` -> `setDetectorConfig()` |
| `SimBPHComboBox` | 합성기가 실제로 만드는 BPH ground truth | `SimStart()` -> `SimConfigBuilder::build()` |

Sim 모드에서 두 값이 다르면 합성된 신호의 실제 BPH와 검출기 힌트가 어긋날 수 있습니다. 이는 실험 목적이 아니라면 사용자 실수로 이어질 가능성이 큽니다.

권장 정책은 다음 중 하나입니다.

### 권장안 A: Sim BPH를 단일 기준으로 사용

- Sim 모드에서는 `BPHComboBox`를 숨긴다.
- `SimBPHComboBox` 값이 변경되면 Detector BPH도 같은 값으로 자동 설정한다.
- UI에는 "Detector BPH is matched to Sim BPH" 같은 안내를 표시할 수 있다.

이 방식은 초보자에게 가장 명확하고 실수 가능성이 낮습니다.

### 대안 B: Detector BPH를 Auto로 강제

- Sim 모드에서는 `BPHComboBox`를 숨긴다.
- `pushCaptureConfig()` 시 Detector BPH를 Auto로 설정한다.
- 합성 ground truth와 검출기 추정 결과의 차이를 관찰하기 쉽다.

이 방식은 검출기 성능 확인에는 유리하지만, Sim BPH와 Detector hint를 일치시키고 싶은 경우에는 덜 직관적입니다.

### 대안 C: 고급 모드에서만 분리 허용

- 기본 Sim 모드에서는 Sim BPH와 Detector BPH를 자동 연동한다.
- Advanced 영역에서 "Override detector BPH hint" 옵션을 켜면 별도 설정을 허용한다.

가장 유연하지만 초기 구현 범위가 커집니다.

**1단계 구현에서는 권장안 A를 우선 적용하는 것이 가장 적절합니다.**

---

## 7. 구현 계획

### Phase 1: QStackedWidget 기반 최소 재구성

목표는 가장 작은 변경으로 모드별 불필요 UI 노출을 줄이는 것입니다.

#### `MainWindow.ui`

- `ModeStackedWidget`을 추가한다.
- `ModeStackedWidget` 안에 세 개의 페이지를 만든다.
  - `page_live`
  - `page_playback`
  - `page_sim`
- Live 전용 위젯을 `page_live`로 이동한다.
  - `InputDeviceComboBox`
  - `RefreshPushButton`
  - `MicrophoneHorizontalSlider`
  - `GainLabel`
  - `SampleRatesComboBox`
  - `SampleRateLabel`
- Playback 전용 페이지를 만든다.
  - 1단계에서는 안내 라벨만 둘 수 있다.
  - 가능하면 `Browse WAV`, 선택 파일명, 파일 샘플레이트 표시까지 포함한다.
- Sim 전용 위젯을 `page_sim`으로 이동한다.
  - `SimBPHComboBox`
  - `SimErrorRateSpinBox`
  - `SimAmplitudeSpinBox`
  - `SimBeatErrorSpinBox`
  - `RealisticCheckBox`
- 공통 측정 설정은 stack 밖에 둔다.
  - `BPHComboBox`
  - `LiftAngleSpinBox`
  - `AveragingPeriodComboBox`
  - `HighLineEdit`
  - `UseConsetCheckBox`

#### `MainWindow.h`

모드 페이지 인덱스를 정의한다.

```cpp
enum ModeStackPage {
    PAGE_LIVE = 0,
    PAGE_PLAYBACK = 1,
    PAGE_SIM = 2
};
```

단, `ModeComboBox->currentIndex()`를 그대로 `QStackedWidget` 인덱스로 쓰면 안 됩니다. 현재 `LoadMode()`는 Live 장치가 없을 때 Live 항목을 건너뛸 수 있기 때문입니다. 페이지 전환은 `ModeComboBox`의 item data 또는 실제 mode enum 값을 기준으로 매핑해야 합니다.

#### `MainWindow.cpp`

모드 변경 시 source page를 전환하는 함수를 추가합니다.

```cpp
void MainWindow::ApplyModeUiState()
{
    const int mode = ui->ModeComboBox->currentData().toInt();

    switch (mode) {
    case LIVE:
        ui->ModeStackedWidget->setCurrentIndex(PAGE_LIVE);
        break;
    case PLAYBACK:
        ui->ModeStackedWidget->setCurrentIndex(PAGE_PLAYBACK);
        break;
    case SIM:
        ui->ModeStackedWidget->setCurrentIndex(PAGE_SIM);
        break;
    }
}
```

`on_ModeComboBox_currentTextChanged()` 또는 `currentIndexChanged()`에서는 기존 오디오 장치 전환 로직을 유지하되, 마지막에 `ApplyModeUiState()`를 호출합니다.

또한 `SetGuiRunMode()` / `SetGuiStopMode()`는 장기적으로 다음 방향으로 정리합니다.

- 실행 중에는 mode 선택, source 설정, measurement 설정을 비활성화한다.
- 숨겨진 페이지의 개별 위젯을 일일이 제어하지 않는다.
- 현재 모드와 실행 상태를 기준으로 UI 상태를 한곳에서 계산한다.

---

## 8. Phase 2: Playback 파일 UX 개선

현재 Playback은 `Start` 클릭 후 `QFileDialog`를 띄웁니다. 개선 방향은 다음과 같습니다.

- Playback 페이지에 `Browse WAV` 버튼을 추가한다.
- 선택된 파일 경로 또는 파일명 라벨을 표시한다.
- WAV 헤더에서 읽은 sample rate를 읽기 전용으로 표시한다.
- `Start`는 이미 선택된 파일을 재생한다.
- 파일이 선택되지 않은 상태에서 `Start`를 누르면 Browse를 먼저 유도한다.

이 방식은 Playback의 실행 흐름을 Live / Sim과 비슷하게 만듭니다.

```text
설정 선택 -> Start
```

현재 방식은 다음처럼 흐름이 중간에 끊깁니다.

```text
Start -> 파일 선택 -> 실제 시작
```

---

## 9. Phase 3: 레이아웃 구조 개선

현재 `.ui`는 absolute geometry 기반입니다. Phase 1에서는 변경 범위를 줄이기 위해 기존 구조를 최대한 유지할 수 있지만, 장기적으로는 layout 기반으로 바꾸는 것이 좋습니다.

권장 구조는 다음과 같습니다.

- 왼쪽 패널 전체를 `QVBoxLayout` 기반으로 구성한다.
- 높이가 부족할 수 있으므로 `QScrollArea`를 고려한다.
- `RunFrame`, `WatchFrame`, `MiscFrame` 같은 기능 중심 이름을 UX 중심 이름으로 바꾼다.
  - `SourceSettingsFrame`
  - `MeasurementFrame`
  - `DetectorTuningFrame`
- `MainWindow`에서 왼쪽 패널을 분리해 `ControlPanelWidget` 같은 별도 클래스로 이동한다.

이 단계는 UI XML 변경량이 크므로 Phase 1 완료 후 별도 작업으로 진행하는 것이 안전합니다.

---

## 10. 검증 계획

### UI 레이아웃 검증

- 앱 실행 시 왼쪽 패널이 깨지지 않는지 확인한다.
- Live / Playback / Sim 전환 시 올바른 source page가 표시되는지 확인한다.
- 숨겨야 할 모드 전용 설정이 다른 모드에서 보이지 않는지 확인한다.

### 기능 검증

**Live**

- 입력 장치 선택이 가능해야 한다.
- Refresh가 정상 동작해야 한다.
- Gain 조절이 `startLive()` 및 런타임 볼륨 조절에 반영되어야 한다.
- Sample Rate 선택 후 Start가 정상 동작해야 한다.

**Playback**

- Playback 모드에서 Live 장치와 Gain이 노출되지 않아야 한다.
- WAV 파일 선택 및 재생이 정상 동작해야 한다.
- 파일 sample rate 표시가 실제 재생 설정과 일치해야 한다.

**Sim**

- Sim 파라미터가 정상 표시되어야 한다.
- Sim BPH, Error Rate, Amplitude, Beat Error, Realistic 설정이 `SimConfigBuilder` 입력으로 반영되어야 한다.
- Detector BPH 정책이 의도대로 적용되어야 한다.

### 회귀 검증

- Start / Stop 버튼 상태가 기존과 동일하게 동작해야 한다.
- Stop 후 모드별 UI 상태가 올바르게 복구되어야 한다.
- Live 장치가 없을 때 `LoadMode()`가 Live를 건너뛰어도 `QStackedWidget` 페이지 전환이 깨지지 않아야 한다.

---

## 11. 우선순위

| 우선순위 | 작업 | 난이도 | 효과 |
|----------|------|--------|------|
| 1 | `QStackedWidget`으로 모드별 source 설정 분리 | 중간 | 즉시 UX 개선 |
| 2 | Sim BPH와 Detector BPH 자동 연동 | 낮음 | Sim 설정 실수 감소 |
| 3 | Playback Browse UI 추가 | 중간 | Playback 흐름 개선 |
| 4 | `SetGuiRunMode()` / `SetGuiStopMode()` 정리 | 낮음~중간 | 상태 관리 단순화 |
| 5 | Advanced / Detector Tuning 접힘 섹션 | 중간 | 초보자 UX 개선 |
| 6 | absolute geometry -> layout 전환 | 중간~높음 | 장기 유지보수 개선 |
| 7 | `ControlPanelWidget` 분리 | 중간 | `MainWindow` 책임 축소 |

---

## 12. 최종 권장 실행 순서

1. `ModeStackedWidget`을 추가하고 Live / Playback / Sim source 설정을 분리한다.
2. 모드 전환 시 `currentData()` 기반으로 stack page를 전환한다.
3. Sim 모드에서 Detector BPH를 Sim BPH와 자동 연동한다.
4. Playback 페이지에 Browse WAV와 선택 파일 표시를 추가한다.
5. UI 상태 제어를 `ApplyModeUiState()` 중심으로 정리한다.
6. 안정화 후 layout 기반 패널과 `ControlPanelWidget` 분리를 검토한다.

---

## 13. 요약

두 문서를 합친 최종 방향은 다음과 같습니다.

- `ux_draft1.md`의 **모드 중심 UX 설계**를 채택한다.
- `draft2.md`의 **QStackedWidget 기반 구현안**을 1단계 구현 전략으로 사용한다.
- Sim BPH 중복, Playback 파일 선택 UX, absolute geometry 한계를 보완한다.
- 당장 큰 리팩터링을 하기보다, 먼저 모드별 source 설정 분리로 사용자 혼란을 줄인다.

이 접근은 현재 코드의 `LiveStart()` / `PlaybackStart()` / `SimStart()` 구조와 잘 맞고, UI 변경의 위험을 단계적으로 관리할 수 있습니다.
