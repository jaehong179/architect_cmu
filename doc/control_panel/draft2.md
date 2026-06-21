# Option 1 (QStackedWidget 기반 동적 모드 전환) 진행 계획서

이 문서는 TimeGrapher 앱의 왼쪽 설정 패널을 **Live, Playback, Sim** 모드에 맞춰 필요한 항목만 동적으로 표시하도록 재조합하는 **Option 1**의 상세 구현 계획을 기술합니다.

---

## 1. 개요 및 설계 목표
- **목적**: 불필요한 설정 위젯 노출을 최소화하여 화면 공간을 효율적으로 사용하고 사용자 경험(UX)을 향상시킵니다.
- **핵심 아이디어**: 왼쪽 패널의 중간 부분에 `QStackedWidget`을 배치하여 모드 선택(`ModeComboBox`) 상태에 따라 해당 모드 전용 UI 페이지를 노출합니다.
- **공통 항목 고정**: 모드와 무관하게 항상 조절해야 하는 측정 설정(Target BPH, 구속각, 필터 컷오프 등)은 상단/하단에 고정 배치합니다.

---

## 2. 레이아웃 재배치 설계 (Geometry 및 계층 구조)

왼쪽 패널의 너비(242px)와 전체 높이(711px) 범위를 유지하면서, 다음과 같이 3개의 영역으로 수직 레이아웃을 분할합니다.

```
+-------------------------------------------------------------+
| 1. 상단 고정 영역 (y: 0 ~ 110, Height: 110)                  |
|    - ModeComboBox (모드 선택: Live / Playback / Sim)         |
|    - StartPushButton / StopPushButton (시작/중지 버튼)        |
+-------------------------------------------------------------+
| 2. QStackedWidget 영역 (y: 110 ~ 300, Height: 190)         |
|    - Page 0 (Live 모드 전용):                                 |
|      * InputDeviceComboBox (마이크 장치 선택)                 |
|      * RefreshPushButton (장치 새로고침)                      |
|      * MicrophoneHorizontalSlider & GainLabel (볼륨 게인)     |
|      * SampleRatesComboBox & SampleRateLabel (샘플 레이트)    |
|    - Page 1 (Playback 모드 전용):                             |
|      * QLabel ("WAV 파일 재생 모드 활성화됨")                  |
|    - Page 2 (Sim 모드 전용):                                  |
|      * SimBPHComboBox (시뮬레이션 BPH)                        |
|      * SimErrorRateSpinBox (오차율 s/d)                       |
|      * SimAmplitudeSpinBox (진폭)                             |
|      * SimBeatErrorSpinBox (비트 에러 ms)                     |
|      * RealisticCheckBox (노이즈 시뮬레이션 여부)              |
+-------------------------------------------------------------+
| 3. 하단 공통 영역 (y: 300 ~ 710, Height: 410)                |
|    - Watch Parameters Frame:                                |
|      * BPHComboBox (Detector 타깃 BPH)                        |
|      * LiftAngleSpinBox (구속각)                               |
|    - Averaging Frame:                                       |
|      * AveragingPeriodComboBox (평균화 필터 기간)               |
|    - Filters & Misc Frame:                                  |
|      * HighLineEdit (고역통과필터 컷오프)                      |
|      * UseConsetCheckBox (C Event 온셋 사용 여부)               |
+-------------------------------------------------------------+
```

---

## 3. 코드 변경 계획

### A. `MainWindow.ui` (Qt Designer XML)
- **QStackedWidget 추가**: `ModeStackedWidget`이라는 이름의 QStackedWidget을 생성하고 3개의 페이지를 구성합니다.
- **위젯 부모(Parent) 재지정**:
  - `InputDeviceComboBox`, `RefreshPushButton`, `MicrophoneHorizontalSlider`, `SampleRatesComboBox` 등을 `ModeStackedWidget`의 `page_live`로 이동시킵니다.
  - `SimFrame` 내부의 모든 시뮬레이션 설정 위젯들을 `ModeStackedWidget`의 `page_sim`으로 이동시키고, 기존 `SimFrame`은 제거합니다.
- **좌표 재조정**: 절대 좌표 방식(Geometry)으로 정의된 위젯들의 `geometry` 속성을 겹치지 않게 순차적으로 배치합니다.

### B. `MainWindow.h`
- Stacked Widget 페이지 인덱스 정의를 추가합니다.
  ```cpp
  enum StackedPageIndex {
      PAGE_LIVE = 0,
      PAGE_PLAYBACK = 1,
      PAGE_SIM = 2
  };
  ```

### C. `MainWindow.cpp`
- **모드 변경 핸들러 수정**: `on_ModeComboBox_currentTextChanged` 또는 `on_ModeComboBox_currentIndexChanged` 시점에 페이지를 동적으로 변경합니다.
  ```cpp
  void MainWindow::on_ModeComboBox_currentTextChanged(const QString &arg1)
  {
      // ... 기존 로직 수행 후 ...
      
      if (arg1 == ModeStrings[LIVE]) {
          ui->ModeStackedWidget->setCurrentIndex(PAGE_LIVE);
      } else if (arg1 == ModeStrings[PLAYBACK]) {
          ui->ModeStackedWidget->setCurrentIndex(PAGE_PLAYBACK);
      } else if (arg1 == ModeStrings[SIM]) {
          ui->ModeStackedWidget->setCurrentIndex(PAGE_SIM);
      }
  }
  ```
- **GUI 활성화/비활성화 상태 제어 수정**:
  - `SetGuiRunMode()`와 `SetGuiStopMode()`에서 비활성화할 위젯 목록을 최적화합니다. (숨겨진 페이지의 위젯들을 일일이 비활성화하지 않고 StackedWidget 자체를 제어하거나 필요한 최소 항목만 제어하도록 구조 단순화)

---

## 4. 검증 및 테스트 계획

### 1단계: 빌드 및 UI 레이아웃 검증
- UI XML 파일의 구문 오류가 없는지 확인하고, 빌드가 정상적으로 수행되는지 검증합니다.
- 앱 실행 시 왼쪽 패널이 깨지거나 위젯이 겹치지 않는지 시각적으로 검증합니다.

### 2단계: 모드 전환 연동 검증
- Mode ComboBox를 전환했을 때 `QStackedWidget`의 각 페이지(Live, Playback, Sim 전용 설정)가 즉각적으로 올바르게 갱신되는지 확인합니다.

### 3단계: 기능 동작성 검증
- **Live 모드**: 장치를 변경하고 Start를 눌렀을 때, 마이크 게인 조절 및 실시간 신호 캡처가 정상 작동하는지 확인합니다.
- **Playback 모드**: Start 클릭 후 WAV 파일을 선택하고 재생 및 분석이 정상적으로 진행되는지 확인합니다.
- **Sim 모드**: Sim BPH, Amplitude 등을 변경하고 Start를 누르면 합성된 파형 신호가 측정 엔진에 정상 입력되는지 확인합니다.
