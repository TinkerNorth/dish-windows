// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/UsbGamepadManager.h"

#include "source/store/UsbPathPreferenceStore.h"

#include "core/input/GamepadButtonLayouts.h"
#include "core/reducer/UsbPollRate.h"

#include "Input/GamepadInputProcessor.h"

#include <utility>

namespace dish::source::usb {

namespace {

// The default satellite controller type a restored Direct binding re-registers
// as when there was never an explicit type choice. Mirrors android's
// CONTROLLER_TYPE_XBOX default in bindTo. (0 == CONTROLLER_TYPE_XBOX, per
// satellite/src/core/types.h, matching SDLGamepadBridge::Device::controllerType.)
constexpr int kControllerTypeXbox = 0;

} // namespace

UsbGamepadManager::UsbGamepadManager(UsbDeviceGateway* gateway,
                                     input::GamepadInputProcessor* processor,
                                     UsbPathPreferenceStore* prefs, UsbDirectObserver* observer)
    : gateway_(gateway), processor_(processor), prefs_(prefs), observer_(observer) {}

reducer::PathChoice UsbGamepadManager::resolvePath(int vendorId, int productId) const {
    std::optional<reducer::PathChoice> stored;
    if (prefs_ != nullptr) { stored = prefs_->choiceFor(vendorId, productId); }
    const bool fastLane =
        gateway_ != nullptr && gateway_->isKnownFastLaneModel(vendorId, productId);
    std::optional<reducer::DirectClaimFailure> prior;
    const auto it = priorFailures_.find(vpKey(vendorId, productId));
    if (it != priorFailures_.end()) { prior = it->second; }
    return reducer::resolvePathChoice(stored, fastLane, prior);
}

void UsbGamepadManager::reconcile() {
    if (gateway_ == nullptr) { return; }
    for (const auto& device : gateway_->enumerate()) { ensureTracked(device); }
}

void UsbGamepadManager::ensureTracked(const UsbDeviceInfo& device) {
    const int key = device.vpKey();
    bool isNew = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        devices_[key] = device;
        if (controllers_.find(key) == controllers_.end()) {
            reducer::UsbController c;
            c.vendorId = device.vendorId;
            c.productId = device.productId;
            c.name = device.name;
            c.phase = reducer::UsbPhase::Routed;
            c.usbPresent = true;
            // Windows raw-HID has no per-device USB permission broker (unlike
            // android's UsbManager grant), so a present pad is always claimable.
            c.hasPermission = true;
            c.desired = resolvePath(device.vendorId, device.productId);
            controllers_.emplace(key, std::move(c));
            isNew = true;
        }
    }
    if (isNew) {
        // Drive toward the resolved path automatically (not user-initiated).
        applyEvent(key, reducer::event::Choose{resolvePath(device.vendorId, device.productId),
                                               /*userInitiated=*/false});
    }
}

void UsbGamepadManager::tryDirectMode(int vendorId, int productId) {
    setPathChoice(vendorId, productId, reducer::PathChoice::Direct);
}

void UsbGamepadManager::setPathChoice(int vendorId, int productId, reducer::PathChoice choice) {
    if (prefs_ != nullptr) { prefs_->setChoice(vendorId, productId, choice); }
    // If the device is present but untracked (chosen before the first scan),
    // bring it in first so the Choose lands on a tracked controller.
    if (gateway_ != nullptr) {
        for (const auto& d : gateway_->enumerate()) {
            if (d.vendorId == vendorId && d.productId == productId) {
                ensureTracked(d);
                break;
            }
        }
    }
    applyEvent(vpKey(vendorId, productId), reducer::event::Choose{choice, /*userInitiated=*/true});
}

void UsbGamepadManager::onFrameworkUp(int vendorId, int productId, int frameworkId) {
    applyEvent(vpKey(vendorId, productId), reducer::event::FrameworkUp{frameworkId});
}

void UsbGamepadManager::onFrameworkDown(int vendorId, int productId) {
    applyEvent(vpKey(vendorId, productId), reducer::event::FrameworkDown{});
}

void UsbGamepadManager::onUsbGone(int vendorId, int productId) {
    const int key = vpKey(vendorId, productId);
    applyEvent(key, reducer::event::UsbUnplugged{});
    std::lock_guard<std::mutex> lock(mtx_);
    devices_.erase(key);
    // A fresh plug-in of this model should re-evaluate Direct rather than inherit
    // a stale failure (android clears the registry's directFailed here).
    priorFailures_.erase(key);
}

void UsbGamepadManager::fireTimeout(int vendorId, int productId) {
    applyEvent(vpKey(vendorId, productId), reducer::event::Timeout{});
}

std::map<int, reducer::UsbController> UsbGamepadManager::controllers() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return controllers_;
}

std::optional<reducer::UsbController> UsbGamepadManager::controllerFor(int vendorId,
                                                                       int productId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = controllers_.find(vpKey(vendorId, productId));
    if (it == controllers_.end()) { return std::nullopt; }
    return it->second;
}

void UsbGamepadManager::applyEvent(int key, const reducer::UsbEvent& event) {
    std::optional<reducer::UsbController> cur;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = controllers_.find(key);
        if (it == controllers_.end()) { return; }
        cur = it->second;
    }
    auto [next, effects] = reducer::reduce(*cur, event);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (next.has_value()) {
            controllers_[key] = *next;
        } else {
            controllers_.erase(key);
        }
    }
    const reducer::UsbController& ctx = next.has_value() ? *next : *cur;
    for (const auto& fx : effects) { execute(key, ctx, fx); }
}

void UsbGamepadManager::execute(int key, const reducer::UsbController& c,
                                const reducer::UsbEffect& fx) {
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, reducer::effect::Claim>) {
                runClaim(key);
            } else if constexpr (std::is_same_v<T, reducer::effect::Reclaim>) {
                runReclaim(key, c);
            } else if constexpr (std::is_same_v<T, reducer::effect::Release>) {
                if (c.syntheticId.has_value() && gateway_ != nullptr) {
                    gateway_->releaseClaim(*c.syntheticId);
                }
            } else if constexpr (std::is_same_v<T, reducer::effect::RequestPermission>) {
                // Windows raw-HID needs no permission grant — the open/claim is
                // the only gate. Treat the request as immediately satisfied so a
                // user Choose(Direct) proceeds to the claim.
                applyEvent(key, reducer::event::PermissionGranted{});
            } else if constexpr (std::is_same_v<T, reducer::effect::BindFramework>) {
                if (observer_ != nullptr && c.connId.has_value()) {
                    observer_->bind(e.frameworkId, *c.connId, c.type.value_or(kControllerTypeXbox));
                }
            } else if constexpr (std::is_same_v<T, reducer::effect::RemoveSynthetic>) {
                if (gateway_ != nullptr) { gateway_->releaseClaim(e.syntheticId); }
                if (observer_ != nullptr) { observer_->syntheticRemoved(e.syntheticId); }
            } else if constexpr (std::is_same_v<T, reducer::effect::BeginHold>) {
                if (observer_ != nullptr) { observer_->beginHold(c.vendorId, c.productId); }
            } else if constexpr (std::is_same_v<T, reducer::effect::EndHold>) {
                if (observer_ != nullptr) { observer_->endHold(c.vendorId, c.productId); }
            } else if constexpr (std::is_same_v<T, reducer::effect::MarkNeedsReplug>) {
                if (observer_ != nullptr) { observer_->markNeedsReplug(c.vendorId, c.productId); }
            } else if constexpr (std::is_same_v<T, reducer::effect::MarkRestoreStuck>) {
                if (observer_ != nullptr) { observer_->markRestoreStuck(c.vendorId, c.productId); }
            } else if constexpr (std::is_same_v<T, reducer::effect::ClearRestoreStuck>) {
                if (observer_ != nullptr) { observer_->clearRestoreStuck(c.vendorId, c.productId); }
            } else if constexpr (std::is_same_v<T, reducer::effect::StartTimeout>) {
                // The real transition timer is owned by the production wiring
                // (a Qt timer fed back through fireTimeout). Nothing to do in the
                // pure driver core; tests fire the timeout deterministically.
            } else if constexpr (std::is_same_v<T, reducer::effect::Notify>) {
                if (observer_ != nullptr) { observer_->notice(c, e.notice); }
            } else if constexpr (std::is_same_v<T, reducer::effect::SetPref>) {
                if (prefs_ != nullptr) { prefs_->setChoice(c.vendorId, c.productId, e.choice); }
            } else if constexpr (std::is_same_v<T, reducer::effect::MarkFailure>) {
                priorFailures_[key] = e.reason;
                if (observer_ != nullptr) {
                    observer_->markFailure(c.vendorId, c.productId, e.reason);
                }
            } else if constexpr (std::is_same_v<T, reducer::effect::ClearFailure>) {
                priorFailures_.erase(key);
                if (observer_ != nullptr) { observer_->clearFailure(c.vendorId, c.productId); }
            }
        },
        fx);
}

void UsbGamepadManager::runClaim(int key) {
    UsbDeviceInfo device;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = devices_.find(key);
        if (it == devices_.end()) {
            // No device to claim (gone between Choose and Claim): treat as Busy,
            // framework not stolen — mirrors android's `null -> Busy` arm.
            applyEvent(key, reducer::event::ClaimFailed{reducer::DirectClaimFailure::Busy, false});
            return;
        }
        device = it->second;
    }
    const ClaimResult outcome = doClaim(device);
    if (outcome.ok) {
        applyEvent(key, reducer::event::ClaimSucceeded{outcome.syntheticId});
    } else {
        applyEvent(key, reducer::event::ClaimFailed{outcome.reason, outcome.frameworkStolen});
    }
}

void UsbGamepadManager::runReclaim(int key, const reducer::UsbController& c) {
    const std::optional<int> oldPlaceholder = c.syntheticId;
    UsbDeviceInfo device;
    bool haveDevice = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = devices_.find(key);
        if (it != devices_.end()) {
            device = it->second;
            haveDevice = true;
        }
    }
    const ClaimResult outcome =
        haveDevice ? doClaim(device) : ClaimResult::fail(reducer::DirectClaimFailure::Busy, false);
    // The old placeholder synthetic is dropped on a reclaim (android removes it
    // before feeding the outcome back).
    if (oldPlaceholder.has_value()) {
        if (gateway_ != nullptr) { gateway_->releaseClaim(*oldPlaceholder); }
        if (observer_ != nullptr) { observer_->syntheticRemoved(*oldPlaceholder); }
    }
    if (outcome.ok) {
        if (observer_ != nullptr && c.connId.has_value()) {
            observer_->bind(outcome.syntheticId, *c.connId, c.type.value_or(kControllerTypeXbox));
        }
        applyEvent(key, reducer::event::ClaimSucceeded{outcome.syntheticId});
    } else {
        applyEvent(key, reducer::event::ClaimFailed{outcome.reason, outcome.frameworkStolen});
    }
}

ClaimResult UsbGamepadManager::doClaim(const UsbDeviceInfo& device) {
    if (gateway_ == nullptr) {
        return ClaimResult::fail(reducer::DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    }
    // The read loop maps each decoded HID report through the pure packed-int
    // HID->XUSB layout math and publishes to GamepadInputProcessor on the
    // gateway thread — the same publish path the SDL bridge uses. No allocation
    // per report; no Qt on this path.
    const std::string deviceTag = device.name;
    const int vp = device.vpKey();
    input::GamepadInputProcessor* processor = processor_;
    const ClaimResult outcome = gateway_->claim(device, [processor, vp](const UsbReport& r) {
        if (processor == nullptr) { return; }
        input::GamepadInputProcessor::DeviceState st;
        st.wButtons = static_cast<std::uint16_t>(input::layout::hidToXusb(r.hidButtons, r.hidHat));
        st.lt = r.lt;
        st.rt = r.rt;
        st.lx = r.lx;
        st.ly = r.ly;
        st.rx = r.rx;
        st.ry = r.ry;
        // The synthetic device id for the input pipeline is the model key as
        // a string; one claimed pad per model on this path.
        processor->publish(std::to_string(vp), st);
    });
    if (outcome.ok && observer_ != nullptr) {
        observer_->syntheticAdded(
            outcome.syntheticId, deviceTag, device.hasImu,
            reducer::computeUsbPollRateHz(device.endpointInInterval, device.endpointInMaxPacket),
            device.vendorId, device.productId);
    }
    return outcome;
}

} // namespace dish::source::usb
