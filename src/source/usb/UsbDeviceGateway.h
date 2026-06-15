// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbDeviceGateway — the IO/native boundary the USB-direct claim driver talks
// to. This is the seam that lets the pure FSM (core/reducer/UsbPathMachine) and
// the driver (UsbGamepadManager) be tested against a FAKE device with no real
// USB/HID IO, while production swaps in the Windows raw-HID gateway
// (WinHidGateway). It is the C++ analogue of the slice of android's
// UsbManager + the `core/jni` USB attach/detach surface that UsbGamepadManager
// drives.
//
// SoC: this is a Gateway — IO only, no domain state beyond an in-flight claim.
// The DECISION of when to claim / release / bind lives in the pure reducer; the
// manager turns world signals into events and executes the reducer's effects
// through this interface.

#pragma once

#include "core/reducer/DirectClaimFailure.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dish::source::usb {

// A gamepad-shaped USB device the gateway found (one interrupt-IN endpoint on a
// HID or vendor-specific interface). Mirrors the fields android reads off
// UsbDevice + the chosen interrupt-IN endpoint.
struct UsbDeviceInfo {
    int vendorId = 0;
    int productId = 0;
    std::string name;             // friendly/product name (or the device path).
    int interfaceNumber = 0;      // the claimed interface's number.
    int endpointInMaxPacket = 64; // interrupt-IN wMaxPacketSize.
    int endpointInInterval = 1;   // interrupt-IN bInterval.
    bool hasOutEndpoint = false;  // an interrupt-OUT endpoint exists (for rumble).
    bool hasImu = false;          // the model carries a gyro/accel (DualSense/DS4).

    int vpKey() const { return (vendorId << 16) | (productId & 0xFFFF); }
};

// The result of opening + claiming a device's interface and bringing up its
// read loop. On success a synthetic device id (negative, like android's) is
// handed back; on failure the cause + whether the framework interface was
// stolen (so the FSM knows if it must wait for re-enumeration).
struct ClaimResult {
    bool ok = false;
    int syntheticId = 0;                                                    // valid iff ok.
    reducer::DirectClaimFailure reason = reducer::DirectClaimFailure::Busy; // valid iff !ok.
    bool frameworkStolen = false;                                           // valid iff !ok.

    static ClaimResult success(int syntheticId) {
        return ClaimResult{true, syntheticId, {}, false};
    }
    static ClaimResult fail(reducer::DirectClaimFailure reason, bool frameworkStolen) {
        return ClaimResult{false, 0, reason, frameworkStolen};
    }
};

// A decoded controller report from the claimed device's read loop, already
// normalised to the XUSB axis/trigger scale (the gateway owns the per-model HID
// report parse). The driver maps the buttons through the pure HID<->XUSB layout
// math before publishing to GamepadInputProcessor.
struct UsbReport {
    int hidButtons = 0; // HID button word (BTN_A..BTN_HOME); see GamepadButtonLayouts.
    int hidHat = 0;     // HID hat octant 0..8.
    std::uint8_t lt = 0;
    std::uint8_t rt = 0;
    std::int16_t lx = 0;
    std::int16_t ly = 0;
    std::int16_t rx = 0;
    std::int16_t ry = 0;
};

class UsbDeviceGateway {
  public:
    virtual ~UsbDeviceGateway() = default;

    // Enumerate the gamepad-shaped USB/HID devices present right now. Empty if
    // none. Xbox-class pads claimed by XInput are NOT returned (the platform
    // hides them from raw HID) — see WinHidGateway for the why.
    virtual std::vector<UsbDeviceInfo> enumerate() = 0;

    // Open + claim the device's interrupt interface and start its read loop,
    // returning a ClaimResult. `onReport` is invoked from the gateway's read
    // thread for each decoded report — plain C++, no allocation per call on the
    // hot path. The synthetic id in a successful result is what the driver binds
    // and later passes to releaseClaim/markTransitioning.
    virtual ClaimResult claim(const UsbDeviceInfo& device,
                              std::function<void(const UsbReport&)> onReport) = 0;

    // Stop the read loop and release the interface for a claimed synthetic id.
    // Idempotent; a no-op for an unknown id.
    virtual void releaseClaim(int syntheticId) = 0;

    // Whether the model is a verified "fast-lane" pad worth auto-claiming Direct
    // (DualSense/DS4/8BitDo families on Windows). Mirrors android
    // native.isKnownFastLaneModel.
    virtual bool isKnownFastLaneModel(int vendorId, int productId) const = 0;

    // The model's current measured completion count (URB/transfer count) for the
    // poll-rate sampler. 0 if unknown / not claimed.
    virtual std::int64_t completionCount(int syntheticId) const = 0;
};

} // namespace dish::source::usb
