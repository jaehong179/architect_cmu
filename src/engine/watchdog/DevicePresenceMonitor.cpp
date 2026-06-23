#include "DevicePresenceMonitor.h"
#include "WatchdogState.h"
#include <QMediaDevices>
#include <QAudioDevice>

DevicePresenceMonitor::DevicePresenceMonitor(WatchdogState *state, QObject *parent)
    : QObject(parent), mState(state), mDevices(new QMediaDevices(this))
{
    connect(mDevices, &QMediaDevices::audioInputsChanged,
            this, &DevicePresenceMonitor::onAudioInputsChanged);
}

void DevicePresenceMonitor::setActiveDevice(const QByteArray &deviceId)
{
    mActiveId = deviceId;
    mTracking = !deviceId.isEmpty();
    if (mState) mState->deviceAlive.store(true, std::memory_order_relaxed);
    refresh();
}

void DevicePresenceMonitor::clearActiveDevice()
{
    mTracking = false;
    mActiveId.clear();
    if (mState) mState->deviceAlive.store(true, std::memory_order_relaxed);  // 비추적 = 장치문제 아님
}

void DevicePresenceMonitor::onAudioInputsChanged()
{
    refresh();
}

void DevicePresenceMonitor::refresh()
{
    if (!mState || !mTracking) return;
    bool present = false;
    const auto inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice &d : inputs) {
        if (d.id() == mActiveId) { present = true; break; }
    }
    mState->deviceAlive.store(present, std::memory_order_relaxed);
}
