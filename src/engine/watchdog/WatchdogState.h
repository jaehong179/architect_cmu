#ifndef WATCHDOGSTATE_H
#define WATCHDOGSTATE_H
// WatchdogState — 워치독이 매 틱 읽는 공유 상태(원자적).
//  CaptureController(메인 스레드)가 publish 하고, 워치독 스레드가 read 한다.
//  단일 스칼라는 std::atomic 으로 잠금 없이 안전 — Check 들은 이 상태의 스냅샷(WatchdogContext)만 본다.
//
//  [확장] 카메라가 생기면 cameraPresent/lastCameraFrameMs 같은 필드를 여기에 추가하고
//  카메라 모듈이 publish 하면 된다(오디오가 lastBlockMs 를 publish 하는 것과 동일 방식).
#include <atomic>
#include <cstdint>

enum class CaptureMode { None = 0, Live, Playback, Sim };

struct WatchdogState {
    std::atomic<int>      mode{ static_cast<int>(CaptureMode::None) };
    std::atomic<bool>     measuring{false};   // 세션 진행 중?
    std::atomic<bool>     paused{false};       // 전체 정지 중?
    std::atomic<int>      sampleRateHz{0};
    std::atomic<uint64_t> totalSamples{0};
    std::atomic<double>   lastBlockMs{0.0};    // 최근 오디오 블록 처리 시각 = 장치 liveness
    std::atomic<double>   lastBeatMs{0.0};     // 최근 A(비트) 이벤트 시각 = 시계 신호 liveness
    std::atomic<double>   sessionStartMs{0.0};
    std::atomic<bool>     deviceAlive{true};   // ②QMediaDevices: 활성 캡처 장치가 목록에 존재?
    std::atomic<bool>     cameraActive{false};      // vision 워커가 카메라를 연 상태?(감시 대상)
    std::atomic<bool>     cameraAlive{true};        // ②QMediaDevices: 활성 카메라가 목록에 존재?
    std::atomic<double>   lastCameraFrameMs{0.0};   // ①최근 비디오 프레임 도착 시각 = 카메라 liveness
};

#endif // WATCHDOGSTATE_H
