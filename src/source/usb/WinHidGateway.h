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
#include "core/thread/TrackedMutex.h"

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
    bool isClaimAlive(int syntheticId) const override;
    bool isPadLinked(int syntheticId) const override;
    std::int64_t completionCount(int syntheticId) const override;
    std::int64_t decodedCount(int syntheticId) const override;
    bool writeOutputReport(int syntheticId, const std::uint8_t* data, std::size_t len) override;
    std::unique_ptr<HidReportWriter> openReportWriter(int syntheticId) override;
    bool setPadMicMuted(int syntheticId, bool muted) override;

  private:
    // The HidP-driven field map for GENERIC-HID pads, built once at claim time
    // from the device's preparsed data (Windows never exposes the raw report
    // descriptor, so HidP_Get*Caps stands in for parsing it). Defined in the
    // .cpp so the Windows HID SDK types stay out of this header.
    struct HidPDecode;

    // The claimed device behind an id, or null. Takes mtx_ and gives it back, so the caller holds
    // no gateway lock while it talks to the pad.
    struct Claimed;
    Claimed* claimedFor(int syntheticId) const;

    // One claimed device's read loop + handle.
    struct Claimed {
        std::string path;       // the HID device interface path.
        void* handle = nullptr; // HANDLE from CreateFile (opaque here).
        std::thread reader;     // the ReadFile loop.
        std::atomic<bool> running{false};
        // Cleared by the reader as it exits, whatever ended it.
        std::atomic<bool> alive{true};
        // Whether a controller is there: from the claim for a model's own pad, and for a
        // receiver's slot from its controller's connect event or first report until its
        // disconnect event. Written by the reader, read by isPadLinked.
        std::atomic<bool> linked{true};
        std::atomic<std::int64_t> completions{0};
        std::atomic<std::int64_t> decoded{0};
        std::function<void(const UsbReport&)> onReport;
        int vendorId = 0;
        int productId = 0;
        // The per-model decoder family chosen from VID:PID at claim time, and the
        // expand-only stick auto-range state it mutates per report (Switch Pro,
        // Steam Controller held stick). Both are read-loop-thread-only (no lock
        // needed): the reader is the sole owner once started, and joined before
        // the Claimed is destroyed.
        input::usbparse::HidParser parser = input::usbparse::HidParser::None;
        input::usbparse::StickAutoRangeState sticks;
        // The DualSense mic-mute latch. Atomics, because it is the one piece
        // of decode state a second thread touches: the read loop flips it on
        // the button edge, setPadMicMuted overwrites it when the user toggles
        // mute in the UI, and both must land on the wire's next report.
        input::usbparse::MicMuteLatch micMute;
        // GENERIC-HID only: the caps-derived decode map (read-loop-thread-only).
        std::unique_ptr<HidPDecode> hidp;
        // Feature-report buffer length from HIDP_CAPS (id byte included), for the
        // claim's feature writes: the 2015 receiver's state request and the
        // output worker's settings.
        int featureReportLen = 0;
        // Output-report buffer length from HIDP_CAPS (id byte included). The
        // Windows HID stack rejects a WriteFile whose length is not exactly
        // this, so a report shorter than the device's longest is zero-padded up
        // to it — the pad ignores the tail, and the alternative is a write that
        // fails with ERROR_INVALID_PARAMETER and no output at all.
        int outputReportLen = 0;
        // Serialises writers against each other. The read loop is untouched: the
        // handle is overlapped, so a write never waits on the pending read.
        thread::TrackedMutex writeMtx{"hid.write"};

        ~Claimed();
    };

    void readLoop(Claimed* c);

    // True when the packet was a dongle event rather than input. Neither arm writes to the pad:
    // the read thread only says whether a controller is there.
    static bool handleSteamWirelessEvent(Claimed* c, const std::uint8_t* data, std::size_t len);
    static void controllerArrived(Claimed* c);
    static void controllerLeft(Claimed* c);
    static bool decodeOneReport(Claimed* c, const std::uint8_t* data, std::size_t len,
                                input::usbparse::ParsedReport& parsed);

    // The next synthetic id to hand out. Negative + decreasing, mirroring the
    // android synthetic-id space (so they never collide with positive SDL ids).
    std::atomic<int> nextSyntheticId_{-1000};

    mutable thread::TrackedMutex mtx_{"hid.claims"};
    std::map<int, std::unique_ptr<Claimed>> claimed_; // syntheticId -> claim.
};

} // namespace dish::source::usb
