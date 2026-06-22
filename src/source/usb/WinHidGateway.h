// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// WinHidGateway — the Windows raw-HID implementation of UsbDeviceGateway. The
// USB-direct claim path for HID gamepads (DualSense / DualShock 4 / 8BitDo).
//
// ── XInput hides Xbox-class pads from raw HID (the platform reality) ──────────
// Windows routes Xbox 360 / Xbox One / Series controllers through the XInput
// (XUSB) driver, which does NOT expose them as generic HID collections — a raw
// HID enumeration via HidD_GetHidGuid / SetupDiGetClassDevs simply will not list
// an XInput-claimed pad. So this gateway can only see, open, and claim HID-class
// pads: DualSense, DualShock 4, Switch Pro, 8BitDo, and other generic-HID
// controllers. Xbox pads stay on SDL/XInput (the "framework" path the FSM calls
// Routed/Standard), and the UsbGamepadManager falls back to that path for them.
// This is why USB-direct on Windows benefits a NARROWER set of pads than on
// android, where the framework claims everything and USB-direct bypasses its
// rate cap for all of them. See UsbGamepadManager.h for the full discussion.
//
// ── Implementation status ─────────────────────────────────────────────────--
// Enumeration (HidD_GetHidGuid + SetupDi*), open (CreateFile), descriptor query
// (HidD_GetAttributes + HidP_GetCaps), and the ReadFile read-loop are all wired
// here and link against hid.lib + setupapi.lib. The genuinely MODEL-SPECIFIC
// part — decoding a raw HID input report into normalised buttons/sticks/triggers
// — is family-dependent (DualSense vs DS4 vs 8BitDo each pack their report
// differently). A conservative skeleton decode is provided; the exact per-model
// byte layout needs validation against real hardware (a manual integration
// step, flagged in the workstream report). The pure FSM / poll-rate / path-
// choice logic and the claim-outcome classification are fully covered by tests
// against a fake gateway; this class is the IO body those tests stand in for.

#pragma once

#include "source/usb/UsbDeviceGateway.h"

#include "core/input/UsbReportParsers.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dish::source::usb {

class WinHidGateway : public UsbDeviceGateway {
  public:
    WinHidGateway();
    ~WinHidGateway() override;

    std::vector<UsbDeviceInfo> enumerate() override;
    ClaimResult claim(const UsbDeviceInfo& device,
                      std::function<void(const UsbReport&)> onReport) override;
    void releaseClaim(int syntheticId) override;
    bool isKnownFastLaneModel(int vendorId, int productId) const override;
    std::int64_t completionCount(int syntheticId) const override;

  private:
    // One claimed device's read loop + handle.
    struct Claimed {
        std::string path;       // the HID device interface path.
        void* handle = nullptr; // HANDLE from CreateFile (opaque here).
        std::thread reader;     // the ReadFile loop.
        std::atomic<bool> running{false};
        std::atomic<std::int64_t> completions{0};
        std::function<void(const UsbReport&)> onReport;
        int vendorId = 0;
        int productId = 0;
        // The per-model decoder family chosen from VID:PID at claim time, and the
        // expand-only stick auto-range state it mutates per report (Switch Pro).
        // Both are read-loop-thread-only (no lock needed): the reader is the sole
        // owner once started, and joined before the Claimed is destroyed.
        input::usbparse::HidParser parser = input::usbparse::HidParser::None;
        input::usbparse::StickAutoRangeState sticks;
    };

    void readLoop(Claimed* c);

    // The next synthetic id to hand out. Negative + decreasing, mirroring the
    // android synthetic-id space (so they never collide with positive SDL ids).
    std::atomic<int> nextSyntheticId_{-1000};

    mutable std::mutex mtx_;
    std::map<int, std::unique_ptr<Claimed>> claimed_; // syntheticId -> claim.
};

} // namespace dish::source::usb
