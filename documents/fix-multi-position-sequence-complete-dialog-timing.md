# Multi-Position Sequence Complete Dialog 타이밍 수정 방안

## 문제 요약

Multi-Position Sequence에서 **모든 position 측정이 완료되면** `SequenceComplete` dialog가 표시되어야 하나, 현재는 **마지막 position 측정이 시작되는 시점**에 dialog가 먼저 표시된다.

## 증상

| 기대 동작 | 실제 동작 |
|-----------|-----------|
| 6번째(마지막) position의 측정 시간이 끝난 후 dialog 표시 | 6번째 position 측정이 시작되고 첫 live 값이 들어오는 즉시 dialog 표시 |
| 측정 중에는 "Progress 5/6" 등 진행 상태 유지 | 측정이 아직 진행 중인데 "Sequence complete" dialog가 뜸 |

## 근본 원인

**실시간(live) 측정값**과 **확정(capture)된 측정값**을 구분하지 않고, 테이블에 값이 있으면 “측정 완료”로 판정하기 때문이다.

### 데이터 흐름 (현재 — 버그)

```
Position 1~5 측정
  → position 변경 시 onPositionComboChanged()에서 이전 position capture (정상)

Position 6 측정 시작
  → onMeasurement()가 6번째 행에 live 값 기록
  → hasAllPositionsMeasured() == true (6개 행 모두 "--" 아님)
  → emit allPositionsMeasured()
  → MainWindow::onAllPositionsMeasured()
  → SequenceComplete dialog 표시  ← 측정 시작 직후 (버그)

Position 6 측정 종료
  → onPositionMeasurementEnded() 호출
  → remaining.isEmpty() 이미 true → ChangePosition dialog도, SequenceComplete dialog도 표시 안 함
```

### 관련 코드

| 파일 | 함수/위치 | 역할 | 문제 |
|------|-----------|------|------|
| `src/ui/tabs/TabSequenceDisplay.cpp` | `onMeasurement()` L226–248 | 현재 position 행에 live 값 기록 | live 값이 완료 판정에 사용됨 |
| `src/ui/tabs/TabSequenceDisplay.cpp` | `isPositionMeasuredInTable()` L36–42 | `"--"`가 아니면 측정 완료 | live/capture 구분 없음 |
| `src/ui/tabs/TabSequenceDisplay.cpp` | `updateComplete()` L322–329 | 6/6이면 `allPositionsMeasured` emit | live 업데이트 경로에서 조기 emit |
| `src/ui/tabs/TabSequenceDisplay.cpp` | `onPositionComboChanged()` L511–531 | position **이탈 시** capture | 마지막 position은 이탈 없음 → capture 경로 없음 |
| `src/ui/MainWindow.cpp` | `onAllPositionsMeasured()` L1618–1631 | SequenceComplete dialog 표시 | 조기 emit에 반응 |
| `src/ui/MainWindow.cpp` | `onPositionMeasurementEnded()` L1561–1616 | 측정 창 종료 처리 | 마지막 position finalize 및 complete dialog 없음 |

## 수정 원칙

1. **UI 표시(live preview)** 와 **측정 확정(capture/finalize)** 을 분리한다.
2. **완료 판정**은 capture/finalize된 position만 기준으로 한다.
3. **마지막 position**은 `onPositionComboChanged()` 대신 **측정 창 종료 시점**에 finalize한다.
4. **SequenceComplete dialog**는 마지막 position의 `measurementWindowEnded` 이후에 표시한다.

## 수정 방안

### 1. `TabSequenceDisplay`: capture 상태 추적 추가

**파일:** `src/ui/tabs/TabSequenceDisplay.h`, `src/ui/tabs/TabSequenceDisplay.cpp`

#### 추가 멤버

```cpp
bool mPositionCaptured[6] = {};  // 확정(capture/finalize)된 position만 true
```

#### 추가/변경 API

```cpp
bool isPositionCaptured(int row) const;
void finalizeCurrentPosition();  // mLast → 현재 position 행 기록 + mPositionCaptured[r] = true
```

#### 변경 대상 함수

| 함수 | 변경 내용 |
|------|-----------|
| `isPositionMeasuredInTable()` | `mPositionCaptured[row]` 기준으로 변경 (또는 이름을 `isPositionCaptured()`로 통일) |
| `hasAllPositionsMeasured()` | 6개 `mPositionCaptured[]`가 모두 true일 때만 true |
| `measuredPositionIndices()` | captured된 row만 반환 |
| `remainingPositionIndices()` | captured되지 않은 row만 반환 |
| `onPositionComboChanged()` | 이전 position capture 시 `mPositionCaptured[r] = true` 설정 |
| `capture()` | 수동 capture 시 `mPositionCaptured[r] = true` 설정 |
| `onResetSession()` | `mPositionCaptured[]` 전부 false로 초기화 |
| `finalizeCurrentPosition()` (신규) | `mPos->currentText()`에 해당하는 row에 `mLast` 기록 후 capture 플래그 설정, `recomputeSummary()` / `updateComplete()` 호출 |

> **참고:** `onMeasurement()`의 live 테이블 업데이트는 UI preview 용도로 유지 가능. 단, `updateComplete()`에서 `allPositionsMeasured`를 emit하는 조건은 capture 기준으로만 동작해야 한다.

---

### 2. `onMeasurement()` 경로에서 조기 complete signal 방지

**파일:** `src/ui/tabs/TabSequenceDisplay.cpp`

- `hasAllPositionsMeasured()`가 capture 플래그 기반이 되면, live 값만으로는 `allPositionsMeasured`가 emit되지 않는다.
- `updateComplete()`의 `emit allPositionsMeasured()`는 **모든 position이 captured된 경우에만** 발생하도록 유지하거나, signal을 제거하고 MainWindow에서 직접 dialog를 호출하는 방식도 가능.

**권장:** capture 플래그 기반 `hasAllPositionsMeasured()`로 자연스럽게 해결. 별도 guard 불필요.

---

### 3. `MainWindow::onPositionMeasurementEnded()`에서 마지막 position 처리

**파일:** `src/ui/MainWindow.cpp`

측정 창이 끝날 때마다 **현재 position을 먼저 finalize**한 뒤, remaining / allMeasured를 판단한다.

```cpp
void MainWindow::onPositionMeasurementEnded(int positionIndex,
                                          const QString &positionName,
                                          const QString &nextPositionName,
                                          bool sequenceComplete)
{
    if (!mSequenceDisplay)
        return;

    // 1) 방금 끝난 position 확정
    mSequenceDisplay->finalizeCurrentPosition();

    const bool allMeasured = mSequenceDisplay->hasAllPositionsMeasured();
    const bool onSequenceTab = ui && ui->GraphicsTabWidget && mSequenceDisplay
        && ui->GraphicsTabWidget->currentWidget() == mSequenceDisplay;

    if (onSequenceTab) {
        if (!allMeasured)
            mReadoutFrozen = true;

        const QList<int> remaining = mSequenceDisplay->remainingPositionIndices();

        if (!remaining.isEmpty()) {
            // 기존: ChangePosition dialog
            PositionChangeDialog dlg(mSequenceDisplay->measuredPositionIndices(),
                                     remaining,
                                     PositionChangeDialog::Mode::ChangePosition,
                                     this);
            mActivePositionDialog = &dlg;
            dlg.exec();
            mActivePositionDialog = nullptr;
        } else if (allMeasured) {
            // 신규: 마지막 position 측정 종료 시점에 SequenceComplete dialog
            onAllPositionsMeasured();
        }

        if (!allMeasured) {
            mEngine.resetForPositionChange();
            if (mTabManager) mTabManager->broadcastResetExcept(mSequenceDisplay);
        }
    }

    if (mPositionSequence) {
        mPositionSequence->confirmPositionChange(
            mSequenceDisplay->measuredPositionCount(),
            mSequenceDisplay->firstRemainingPositionIndex());
    }
}
```

#### Position 1~5 종료 시 (기존 동작 유지)

1. `finalizeCurrentPosition()` — 해당 position capture
2. `remaining`이 비어 있지 않음 → **ChangePosition dialog** 표시
3. 사용자가 watch reposition 후 다음 position으로 진행

#### Position 6(마지막) 종료 시 (수정 후)

1. `finalizeCurrentPosition()` — 6번째 position capture
2. `remaining.isEmpty()` && `allMeasured == true`
3. **SequenceComplete dialog** 표시 (`onAllPositionsMeasured()`)
4. 확인 후 `stopSession()` (기존 `onAllPositionsMeasured()` 동작)

---

### 4. (선택) `PositionSequenceController`: `sequenceComplete` 플래그 전달

**파일:** `src/ui/PositionSequenceController.cpp`

현재 `measurementWindowEnded` 시그널의 마지막 인자 `sequenceComplete`가 항상 `false`로 고정되어 있다.

```cpp
// 현재 (L113)
emit measurementWindowEnded(mCurrentPositionIndex, posName, QString(), false);
```

마지막 sequence step인지 판별하여 `true`를 넘기면 MainWindow에서 “마지막 position 종료”를 더 명확히 처리할 수 있다.  
capture 플래그 기반 수정만으로도 동작 가능하므로 **필수는 아님**.

---

## 수정 후 예상 동작

| 시점 | 동작 |
|------|------|
| 1~5번 position 측정 중 | 해당 행 live preview, Progress N/6 |
| 1~5번 position 측정 종료 | finalize → **ChangePosition dialog** |
| 6번 position 측정 **시작** | 6번째 행 live preview, Progress 5/6 (captured 기준) |
| 6번 position 측정 **종료** | finalize → **SequenceComplete dialog** → stop |

## 수정 파일 목록

| 파일 | 변경 유형 |
|------|-----------|
| `src/ui/tabs/TabSequenceDisplay.h` | capture 플래그, `finalizeCurrentPosition()` 선언 |
| `src/ui/tabs/TabSequenceDisplay.cpp` | capture/finalize 로직, 완료 판정 기준 변경 |
| `src/ui/MainWindow.cpp` | `onPositionMeasurementEnded()`에서 finalize + complete dialog |
| `src/ui/PositionSequenceController.cpp` | (선택) `sequenceComplete` 플래그 |

## 테스트 체크리스트

- [ ] 6-position 전체 sequence 실행 시, 6번째 position **측정 시작 직후** dialog가 뜨지 않는다.
- [ ] 6번째 position **측정 시간 종료 후** SequenceComplete dialog가 표시된다.
- [ ] 1~5번 position 종료 시 ChangePosition dialog가 기존과 같이 표시된다.
- [ ] Progress 표시(N/6)가 captured position 수 기준으로 정확하다.
- [ ] Session reset 후 capture 플래그와 테이블이 모두 초기화된다.
- [ ] Multi-Position Sequence Display 탭이 아닌 다른 탭을 보는 중에는 기존과 같이 부수효과가 없다.
- [ ] Upload to Cloud 버튼은 6개 position 모두 finalize된 후에만 표시된다.

## 참고

- Position capture 설계: 1~5번은 position **이탈 시** (`onPositionComboChanged`), 6번은 **측정 창 종료 시** (`finalizeCurrentPosition`) — 의도적으로 경로가 다름.
- `PositionSequenceController::finishMeasurementWindow()` → `measurementWindowEnded` 시그널이 “측정 종료”의 정확한 타이밍 훅이다.
- 관련 요구사항: FR-MPS-1 (Multi-Position Sequence Display)
