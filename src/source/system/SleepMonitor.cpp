// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/SleepMonitor.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

namespace dish::source {

namespace {

Q_LOGGING_CATEGORY(lcDishSleepMonitor, "dish.sleep")

// GUID_LIDSWITCH_STATE_CHANGE, spelled out so nothing depends on which import
// library happens to define the winnt.h symbol.
constexpr GUID kLidSwitchStateChange = {
    0xba3e0f4d, 0xb817, 0x4094, {0xa2, 0xd1, 0xd5, 0x63, 0x79, 0xe6, 0xa0, 0xf3}};

constexpr wchar_t kLidWindowClassName[] = L"DishLidSwitchWindow";

LRESULT CALLBACK lidWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* monitor =
        reinterpret_cast<WindowsPowerSleepMonitor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    const bool isPowerBroadcast = message == WM_POWERBROADCAST;
    if (!isPowerBroadcast || monitor == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    monitor->onPowerBroadcast(wParam, lParam);
    return TRUE;
}

} // namespace

WindowsPowerSleepMonitor::WindowsPowerSleepMonitor(QObject* parent) : SleepMonitor(parent) {}

WindowsPowerSleepMonitor::~WindowsPowerSleepMonitor() { stop(); }

void WindowsPowerSleepMonitor::start() {
    if (started_) { return; }
    auto* app = QCoreApplication::instance();
    if (app == nullptr) { return; }
    app->installNativeEventFilter(this);
    watchTheLid();
    started_ = true;
}

void WindowsPowerSleepMonitor::stop() {
    if (!started_) { return; }
    stopWatchingTheLid();
    if (auto* app = QCoreApplication::instance()) { app->removeNativeEventFilter(this); }
    started_ = false;
}

// Message-only is enough here, unlike the tray's window: the registration
// addresses this window directly, and the suspend broadcast it would miss
// already reaches Qt's own windows through the filter.
void WindowsPowerSleepMonitor::watchTheLid() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &lidWindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kLidWindowClassName;
    const ATOM windowClass = RegisterClassExW(&wc);
    const bool classIsMissing = windowClass == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS;
    if (classIsMissing) {
        qCWarning(lcDishSleepMonitor) << "lid window class registration failed:" << GetLastError();
        return;
    }
    const HWND window = CreateWindowExW(0, kLidWindowClassName, L"Dish lid", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, instance, nullptr);
    if (window == nullptr) {
        qCWarning(lcDishSleepMonitor) << "lid window creation failed:" << GetLastError();
        return;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    lidWindow_ = window;
    const HPOWERNOTIFY notification = RegisterPowerSettingNotification(
        window, &kLidSwitchStateChange, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (notification == nullptr) {
        qCWarning(lcDishSleepMonitor)
            << "lid switch registration failed; the lid goes unreported:" << GetLastError();
        return;
    }
    lidNotification_ = notification;
}

void WindowsPowerSleepMonitor::stopWatchingTheLid() {
    if (lidNotification_ != nullptr) {
        UnregisterPowerSettingNotification(static_cast<HPOWERNOTIFY>(lidNotification_));
        lidNotification_ = nullptr;
    }
    if (lidWindow_ != nullptr) {
        auto* window = static_cast<HWND>(lidWindow_);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
        lidWindow_ = nullptr;
    }
}

std::optional<bool> WindowsPowerSleepMonitor::edgeFor(unsigned long long powerEvent) {
    switch (powerEvent) {
    case PBT_APMSUSPEND:
        return true;
    case PBT_APMRESUMEAUTOMATIC:
    case PBT_APMRESUMESUSPEND:
        return false;
    default:
        return std::nullopt;
    }
}

std::optional<reducer::LidPosition>
WindowsPowerSleepMonitor::lidPositionFor(const void* powerSetting) {
    if (powerSetting == nullptr) { return std::nullopt; }
    const auto* setting = static_cast<const POWERBROADCAST_SETTING*>(powerSetting);
    const bool isTheLidSwitch = setting->PowerSetting == kLidSwitchStateChange;
    const bool carriesADword = setting->DataLength == sizeof(DWORD);
    if (!isTheLidSwitch || !carriesADword) { return std::nullopt; }
    DWORD state = 0;
    std::memcpy(&state, setting->Data, sizeof(state));
    return reducer::lidPositionFromSwitchState(state);
}

void WindowsPowerSleepMonitor::onPowerBroadcast(quintptr powerEvent, qintptr data) {
    if (const auto edge = edgeFor(powerEvent)) {
        emit preparingForSleep(*edge);
        return;
    }
    if (powerEvent != PBT_POWERSETTINGCHANGE) { return; }
    const auto* setting = reinterpret_cast<const void*>(data);
    if (const auto lid = lidPositionFor(setting)) { emit lidPositionChanged(*lid); }
}

bool WindowsPowerSleepMonitor::nativeEventFilter(const QByteArray& eventType, void* message,
                                                 qintptr* /*result*/) {
    if (message == nullptr || eventType != "windows_generic_MSG") { return false; }
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_POWERBROADCAST) { return false; }
    onPowerBroadcast(msg->wParam, msg->lParam);
    return false;
}

} // namespace dish::source
