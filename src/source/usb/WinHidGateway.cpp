// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/WinHidGateway.h"

#include "core/input/HidTransport.h"
#include "core/input/UsbReportParsers.h"

// <windows.h> first (NOMINMAX / WIN32_LEAN_AND_MEAN come from the build defs),
// then the HID + SetupAPI headers, which depend on the base Win32 types.
#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

#include <array>
#include <cstring>

namespace dish::source::usb {

namespace {

// HID usage page / usages for game controllers (HID Usage Tables §4).
constexpr USAGE kUsagePageGenericDesktop = 0x01;
constexpr USAGE kUsageJoystick = 0x04;
constexpr USAGE kUsageGamepad = 0x05;

// Microsoft's vendor id. Xbox pads are XInput-claimed and won't appear as HID,
// but a few Microsoft HID peripherals share the VID; we skip MS-VID gamepad
// collections defensively so we never fight XInput for an Xbox pad.
constexpr int kVidMicrosoft = 0x045E;

// Known HID-class "fast-lane" controller vendor ids worth auto-claiming Direct
// on Windows: Sony (DualSense/DS4), Nintendo (Switch Pro), 8BitDo. Xbox pads are
// deliberately absent — they live on XInput, not raw HID.
constexpr int kVidSony = 0x054C;
constexpr int kVidNintendo = 0x057E;
constexpr int kVid8BitDo = 0x2DC8;

std::string wideToUtf8(const wchar_t* w) {
    if (w == nullptr) { return {}; }
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) { return {}; }
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// Read a HID device's friendly product string, falling back to the path.
std::string productName(HANDLE h, const std::string& fallbackPath) {
    std::array<wchar_t, 256> buf{};
    if (HidD_GetProductString(h, buf.data(), static_cast<ULONG>(buf.size() * sizeof(wchar_t)))) {
        const std::string s = wideToUtf8(buf.data());
        if (!s.empty()) { return s; }
    }
    return fallbackPath;
}

} // namespace

WinHidGateway::WinHidGateway() = default;

WinHidGateway::~WinHidGateway() {
    // Release every still-open claim (stops the read loops + closes handles).
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& [id, c] : claimed_) { ids.push_back(id); }
    }
    for (int id : ids) { releaseClaim(id); }
}

bool WinHidGateway::isKnownFastLaneModel(int vendorId, int /*productId*/) const {
    return vendorId == kVidSony || vendorId == kVidNintendo || vendorId == kVid8BitDo;
}

std::vector<UsbDeviceInfo> WinHidGateway::enumerate() {
    std::vector<UsbDeviceInfo> out;

    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo =
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) { return out; }

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) { continue; }
        std::vector<std::uint8_t> detailBuf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr)) {
            continue;
        }
        const std::string path = wideToUtf8(detail->DevicePath);

        // A Bluetooth-connected pad is a HID device too (same VID:PID as its
        // USB identity) but is NOT a USB-direct claim candidate: the raw-HID
        // claim is a USB feature, the per-model decoders parse the USB report
        // layout (the BT layout differs — a DS4 streams the short 0x01 report
        // until a feature-report handshake), and tracking it would grow a bogus
        // "USB PATH" control on a wireless pad. Skip it before probing.
        if (input::isBluetoothHidDevicePath(path)) { continue; }

        // Open for query only (no read/write share so we don't disturb other
        // readers while probing). The actual claim re-opens with read access.
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { continue; }

        HIDD_ATTRIBUTES attrs{};
        attrs.Size = sizeof(attrs);
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        bool gamepadShaped = false;
        UsbDeviceInfo info;
        if (HidD_GetAttributes(h, &attrs) && HidD_GetPreparsedData(h, &preparsed)) {
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                gamepadShaped = caps.UsagePage == kUsagePageGenericDesktop &&
                                (caps.Usage == kUsageGamepad || caps.Usage == kUsageJoystick);
            }
            HidD_FreePreparsedData(preparsed);
        }
        if (gamepadShaped) {
            info.vendorId = attrs.VendorID;
            info.productId = attrs.ProductID;
            info.name = productName(h, path);
            info.interfaceNumber = 0;
            // The input report length is our max-packet proxy; bInterval is not
            // exposed by the HID class API, so default it to 1ms (the common
            // gamepad case) — the poll-rate sampler measures the real rate.
            info.endpointInMaxPacket =
                caps.InputReportByteLength > 0 ? caps.InputReportByteLength : 64;
            info.endpointInInterval = 1;
            info.hasOutEndpoint = caps.OutputReportByteLength > 0;
            // DualSense / DS4 (Sony) and the Switch Pro (Nintendo) carry an IMU;
            // derive it from the per-model decoder family so it tracks the parser
            // selection rather than a hard-coded VID list.
            info.hasImu = input::usbparse::parserHasImu(
                input::usbparse::parserForDevice(info.vendorId, info.productId));
        }
        CloseHandle(h);

        // Skip Microsoft-VID gamepad collections: Xbox pads belong to XInput, not
        // this raw-HID path, and shouldn't be fought over.
        if (gamepadShaped && info.vendorId != kVidMicrosoft) { out.push_back(std::move(info)); }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return out;
}

ClaimResult WinHidGateway::claim(const UsbDeviceInfo& device,
                                 std::function<void(const UsbReport&)> onReport) {
    // Re-find the device path by VID:PID (enumerate() returned descriptors, not
    // handles, so the claim owns the open). Open with read access this time.
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo =
        SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return ClaimResult::fail(reducer::DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    }

    HANDLE opened = INVALID_HANDLE_VALUE;
    bool sawDevice = false;
    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    for (DWORD i = 0; opened == INVALID_HANDLE_VALUE &&
                      SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData);
         ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) { continue; }
        std::vector<std::uint8_t> detailBuf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr)) {
            continue;
        }
        // Same VID:PID can be present over BOTH transports at once (a pad
        // charging over USB while still BT-paired); never claim the Bluetooth
        // interface — enumerate() filtered it, so the claim must match.
        if (input::isBluetoothHidDevicePath(wideToUtf8(detail->DevicePath))) { continue; }
        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) { continue; }
        HIDD_ATTRIBUTES attrs{};
        attrs.Size = sizeof(attrs);
        if (HidD_GetAttributes(h, &attrs) && attrs.VendorID == device.vendorId &&
            attrs.ProductID == device.productId) {
            sawDevice = true;
            opened = h;
        } else {
            CloseHandle(h);
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);

    if (opened == INVALID_HANDLE_VALUE) {
        // A device whose interface existed but every open failed reads as Busy
        // (held by another owner); a device we never saw also folds to Busy. The
        // framework was never stolen (we never claimed exclusively), so the SDL
        // path stays usable. PermissionDenied is reserved for ERROR_ACCESS_DENIED
        // on a device we did see.
        const DWORD err = GetLastError();
        const auto reason = (sawDevice && err == ERROR_ACCESS_DENIED)
                                ? reducer::DirectClaimFailure::PermissionDenied
                                : reducer::DirectClaimFailure::Busy;
        return ClaimResult::fail(reason, /*frameworkStolen=*/false);
    }

    const int syntheticId = nextSyntheticId_.fetch_sub(1);
    auto claim = std::make_unique<Claimed>();
    claim->path = device.name;
    claim->handle = opened;
    claim->onReport = std::move(onReport);
    claim->vendorId = device.vendorId;
    claim->productId = device.productId;
    claim->parser = input::usbparse::parserForDevice(device.vendorId, device.productId);
    claim->running.store(true);
    Claimed* raw = claim.get();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        claimed_.emplace(syntheticId, std::move(claim));
    }
    raw->reader = std::thread([this, raw] { readLoop(raw); });
    return ClaimResult::success(syntheticId);
}

void WinHidGateway::readLoop(Claimed* c) {
    // The read loop is plain C++ on its own thread — no allocation per report, no
    // Qt. It decodes each HID input report into a normalised XUSB report via the
    // pure core/input/UsbReportParsers decoders (chosen per-model at claim time)
    // and hands it to onReport, which publishes through GamepadInputProcessor.
    //
    // Allocation discipline: the read buffer + the ParsedReport scratch live on
    // this thread's stack and are reused every iteration; the decoder is a pure
    // function over those, mutating the device's stick auto-range state in place.
    // Nothing on the per-report path heap-allocates.
    //
    // NOTE: the button/stick/trigger byte offsets mirror dish-android's
    // usb_parsers.cpp 1:1 (hardware-validated there). The DS4/DualSense IMU +
    // touchpad offsets are the public hid-playstation layout and need a final
    // sign/scale check against real pads (flagged in UsbReportParsers.h).
    auto* handle = static_cast<HANDLE>(c->handle);
    std::array<std::uint8_t, 128> buf{};
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (c->running.load()) {
        DWORD read = 0;
        ResetEvent(ov.hEvent);
        if (!ReadFile(handle, buf.data(), static_cast<DWORD>(buf.size()), &read, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) { break; }
            // Wait with a short timeout so running=false is observed promptly.
            const DWORD w = WaitForSingleObject(ov.hEvent, 100);
            if (w == WAIT_TIMEOUT) {
                CancelIo(handle);
                continue;
            }
            if (!GetOverlappedResult(handle, &ov, &read, FALSE)) { break; }
        }
        if (read == 0) { continue; }
        c->completions.fetch_add(1);
        // Decode into the XUSB report. A report that doesn't match the family's
        // shape (wrong id / too short) is skipped rather than published as noise.
        input::usbparse::ParsedReport parsed{};
        if (!input::usbparse::decodeReport(c->parser, buf.data(), static_cast<std::size_t>(read),
                                           parsed, c->sticks)) {
            continue;
        }
        UsbReport report{};
        report.wButtons = parsed.wButtons;
        report.lt = parsed.lt;
        report.rt = parsed.rt;
        report.lx = parsed.lx;
        report.ly = parsed.ly;
        report.rx = parsed.rx;
        report.ry = parsed.ry;
        report.motionValid = parsed.motionValid;
        report.gyroX = parsed.gyroX;
        report.gyroY = parsed.gyroY;
        report.gyroZ = parsed.gyroZ;
        report.accelX = parsed.accelX;
        report.accelY = parsed.accelY;
        report.accelZ = parsed.accelZ;
        report.touchpadValid = parsed.touchpadValid;
        report.finger0Active = parsed.finger0Active;
        report.finger0Id = parsed.finger0Id;
        report.finger0X = parsed.finger0X;
        report.finger0Y = parsed.finger0Y;
        report.finger1Active = parsed.finger1Active;
        report.finger1Id = parsed.finger1Id;
        report.finger1X = parsed.finger1X;
        report.finger1Y = parsed.finger1Y;
        report.touchpadButton = parsed.touchpadButton;
        if (c->onReport) { c->onReport(report); }
    }
    if (ov.hEvent != nullptr) { CloseHandle(ov.hEvent); }
}

void WinHidGateway::releaseClaim(int syntheticId) {
    std::unique_ptr<Claimed> claim;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = claimed_.find(syntheticId);
        if (it == claimed_.end()) { return; }
        claim = std::move(it->second);
        claimed_.erase(it);
    }
    claim->running.store(false);
    auto* handle = static_cast<HANDLE>(claim->handle);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) { CancelIoEx(handle, nullptr); }
    if (claim->reader.joinable()) { claim->reader.join(); }
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); }
}

std::int64_t WinHidGateway::completionCount(int syntheticId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return 0; }
    return it->second->completions.load();
}

} // namespace dish::source::usb
