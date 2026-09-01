// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/WinHidGateway.h"

#include "core/input/HidTransport.h"
#include "core/input/UsbHidLayout.h"
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
// How long an OUT report may sit unacknowledged before it is abandoned. Well
// under the 2 s heartbeat, so a wedged pad can never back the feedback caller
// up into the session's liveness.
constexpr DWORD kOutputWriteTimeoutMs = 250;

constexpr USAGE kUsagePageGenericDesktop = 0x01;
constexpr USAGE kUsageJoystick = 0x04;
constexpr USAGE kUsageGamepad = 0x05;
// The Steam Controller's game interface is vendor-defined HID with no gamepad
// usages, so Android never enumerates it as a gamepad and neither does this
// filter; it is admitted by model, not by shape.
constexpr USAGE kUsagePageVendor = 0xFF00;

// Microsoft's vendor id. Xbox pads are XInput-claimed and won't appear as HID,
// but a few Microsoft HID peripherals share the VID; we skip MS-VID gamepad
// collections defensively so we never fight XInput for an Xbox pad.
constexpr int kVidMicrosoft = 0x045E;

// Known HID-class "fast-lane" controller vendor ids worth auto-claiming Direct
// on Windows: Sony (DualSense/DS4), Nintendo (Switch Pro), 8BitDo. Xbox pads are
// deliberately absent — they live on XInput, not raw HID. Models from the
// known-model table (PDP Switch pads, the Steam Controller) are reached only
// through an explicit Direct pick, never auto-claimed: the Steam Controller
// claim reconfigures a device its owner may be using as a desktop mouse.
constexpr int kVidSony = 0x054C;
constexpr int kVidNintendo = 0x057E;
constexpr int kVid8BitDo = 0x2DC8;

// Feature-report writes retry the way SDL and hid-steam retry EPIPE: the
// wireless dongle under load fails transiently, not terminally. The budget is
// capped so even a wholly unresponsive device finishes init and teardown well
// inside the manager's transition timeout.
constexpr int kFeatureReportAttempts = 25;
constexpr DWORD kFeatureReportRetryMs = 20;

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
    if (HidD_GetProductString(h, buf.data(), static_cast<ULONG>(buf.size() * sizeof(wchar_t))) !=
        0) {
        const std::string s = wideToUtf8(buf.data());
        if (!s.empty()) { return s; }
    }
    return fallbackPath;
}

// Whether this HID collection is the one the model's parser decodes: the
// vendor-defined game interface for the Steam Controller (its keyboard and
// mouse collections share the VID:PID), a gamepad/joystick collection for
// every other family.
bool collectionMatchesParser(const HIDP_CAPS& caps, input::usbparse::HidParser parser) {
    if (parser == input::usbparse::HidParser::SteamController) {
        return caps.UsagePage == kUsagePageVendor;
    }
    return caps.UsagePage == kUsagePageGenericDesktop &&
           (caps.Usage == kUsageGamepad || caps.Usage == kUsageJoystick);
}

// One Steam Controller config feature report: report id 0 + the packet, padded
// to the collection's feature length, with the transient-failure retry both
// reference drivers use.
bool sendSteamFeature(HANDLE h, int featureLen, const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, 128> buf{};
    if (featureLen <= 0 || static_cast<std::size_t>(featureLen) > buf.size() ||
        len + 1 > static_cast<std::size_t>(featureLen)) {
        return false;
    }
    std::memcpy(buf.data() + 1, data, len);
    for (int attempt = 0; attempt < kFeatureReportAttempts; attempt++) {
        if (HidD_SetFeature(h, buf.data(), static_cast<ULONG>(featureLen)) != 0) { return true; }
        Sleep(kFeatureReportRetryMs);
    }
    return false;
}

// Runs one config direction (quiet at attach, restore at release) to the end.
// Restore is best-effort: a pad that is gone can no longer be restored, and
// failing the release over it would strand the claim.
bool runSteamConfig(HANDLE h, int featureLen, input::usbparse::SteamConfig stage) {
    std::array<std::uint8_t, 16> pkt{};
    for (int i = 0;; i++) {
        const std::size_t n =
            input::usbparse::buildSteamConfigPacket(stage, i, pkt.data(), pkt.size());
        if (n == 0) { break; }
        if (!sendSteamFeature(h, featureLen, pkt.data(), n)) { return false; }
    }
    return true;
}

} // namespace

// The caps-derived field map for GENERIC-HID pads. Windows exposes preparsed
// data instead of the raw report descriptor, so HidP_GetUsageValue/GetUsages
// against it replaces the bit-offset walk of core/input/UsbHidLayout.h; the
// scaling and the button-index mapping stay in that shared pure header so the
// cross-client decode rules have one home.
struct WinHidGateway::HidPDecode {
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    bool valid = false;

    struct Axis {
        bool present = false;
        USAGE page = 0;
        USAGE usage = 0;
        input::usbhid::HidAxis scale;
    };
    Axis lx, ly, rx, ry, lt, rt;
    bool hasHat = false;
    USAGE hatPage = 0;
    USAGE hatUsage = 0;
    std::int32_t hatLogicalMin = 0;
    std::int32_t hatLogicalMax = 0;
    bool hasButtons = false;
    USAGE buttonPage = 0;
    USAGE buttonUsageMin = 0;
    bool switchOrderButtons = false;

    ~HidPDecode() {
        if (preparsed != nullptr) { HidD_FreePreparsedData(preparsed); }
    }

    // First declaration of an axis wins, mirroring UsbHidLayout::assignUsage.
    void assign(USAGE page, USAGE usage, const HIDP_VALUE_CAPS& vc) {
        Axis* slot = nullptr;
        if (page == 0x01) {
            switch (usage) {
            case 0x30:
                slot = &lx;
                break;
            case 0x31:
                slot = &ly;
                break;
            case 0x32:
                slot = &rx;
                break;
            case 0x35:
                slot = &ry;
                break;
            case 0x33:
                slot = &lt;
                break;
            case 0x34:
                slot = &rt;
                break;
            case 0x39:
                if (!hasHat) {
                    hasHat = true;
                    hatPage = page;
                    hatUsage = usage;
                    hatLogicalMin = vc.LogicalMin;
                    hatLogicalMax = vc.LogicalMax;
                }
                return;
            default:
                return;
            }
        } else if (page == 0x02) {
            if (usage == 0xC5) {
                slot = &lt;
            } else if (usage == 0xC4) {
                slot = &rt;
            } else {
                return;
            }
        } else {
            return;
        }
        if (slot->present) { return; }
        slot->present = true;
        slot->page = page;
        slot->usage = usage;
        slot->scale.present = true;
        slot->scale.bitSize = static_cast<std::uint8_t>(vc.BitSize);
        slot->scale.logicalMin = vc.LogicalMin;
        slot->scale.logicalMax = vc.LogicalMax;
    }

    void build(int vendorId, int productId) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) { return; }

        std::vector<HIDP_VALUE_CAPS> vcaps(caps.NumberInputValueCaps);
        USHORT vlen = caps.NumberInputValueCaps;
        if (vlen > 0 &&
            HidP_GetValueCaps(HidP_Input, vcaps.data(), &vlen, preparsed) == HIDP_STATUS_SUCCESS) {
            for (USHORT i = 0; i < vlen; i++) {
                const auto& vc = vcaps[i];
                if (vc.IsRange != 0) {
                    for (USAGE u = vc.Range.UsageMin; u <= vc.Range.UsageMax; u++) {
                        assign(vc.UsagePage, u, vc);
                    }
                } else {
                    assign(vc.UsagePage, vc.NotRange.Usage, vc);
                }
            }
        }

        std::vector<HIDP_BUTTON_CAPS> bcaps(caps.NumberInputButtonCaps);
        USHORT blen = caps.NumberInputButtonCaps;
        if (blen > 0 &&
            HidP_GetButtonCaps(HidP_Input, bcaps.data(), &blen, preparsed) == HIDP_STATUS_SUCCESS) {
            for (USHORT i = 0; i < blen; i++) {
                const auto& bc = bcaps[i];
                if (bc.UsagePage != 0x09 || hasButtons) { continue; }
                hasButtons = true;
                buttonPage = bc.UsagePage;
                buttonUsageMin = bc.IsRange != 0 ? bc.Range.UsageMin : bc.NotRange.Usage;
            }
        }

        switchOrderButtons = input::usbparse::buttonOrderForDevice(vendorId, productId) ==
                             input::usbparse::ButtonOrder::Switch;
        // Same validity rule as parseReportDescriptor: something gamepad-like
        // must exist, else the fixed-offset fallback stays in charge.
        valid = lx.present || ly.present || hasButtons || hasHat;
    }

    std::int32_t readValue(const Axis& a, PCHAR report, ULONG len) const {
        ULONG raw = 0;
        if (HidP_GetUsageValue(HidP_Input, a.page, 0, a.usage, &raw, preparsed, report, len) !=
            HIDP_STATUS_SUCCESS) {
            return 0;
        }
        return static_cast<std::int32_t>(raw);
    }

    bool decode(const std::uint8_t* buf, std::size_t len, input::usbparse::ParsedReport& s) const {
        if (!valid) { return false; }
        auto* report = reinterpret_cast<PCHAR>(const_cast<std::uint8_t*>(buf));
        const auto rlen = static_cast<ULONG>(len);

        using input::usbhid::scaleAxis16;
        using input::usbhid::scaleTrig8;
        if (lx.present) {
            s.lx = scaleAxis16(static_cast<std::uint32_t>(readValue(lx, report, rlen)), lx.scale,
                               false);
        }
        if (ly.present) {
            s.ly = scaleAxis16(static_cast<std::uint32_t>(readValue(ly, report, rlen)), ly.scale,
                               true);
        }
        if (rx.present) {
            s.rx = scaleAxis16(static_cast<std::uint32_t>(readValue(rx, report, rlen)), rx.scale,
                               false);
        }
        if (ry.present) {
            s.ry = scaleAxis16(static_cast<std::uint32_t>(readValue(ry, report, rlen)), ry.scale,
                               true);
        }
        if (lt.present) {
            s.lt = scaleTrig8(static_cast<std::uint32_t>(readValue(lt, report, rlen)), lt.scale);
        }
        if (rt.present) {
            s.rt = scaleTrig8(static_cast<std::uint32_t>(readValue(rt, report, rlen)), rt.scale);
        }

        std::uint16_t b = 0;
        if (hasHat) {
            ULONG raw = 0;
            if (HidP_GetUsageValue(HidP_Input, hatPage, 0, hatUsage, &raw, preparsed, report,
                                   rlen) == HIDP_STATUS_SUCCESS) {
                const int dir = static_cast<int>(raw) - static_cast<int>(hatLogicalMin);
                const int range = static_cast<int>(hatLogicalMax) - static_cast<int>(hatLogicalMin);
                if (dir >= 0 && dir <= range && dir <= 7) {
                    b = static_cast<std::uint16_t>(b | input::usbhid::dpadBitsForDir(dir));
                }
            }
        }
        if (hasButtons) {
            std::array<USAGE, 64> usages{};
            auto count = static_cast<ULONG>(usages.size());
            if (HidP_GetUsages(HidP_Input, buttonPage, 0, usages.data(), &count, preparsed, report,
                               rlen) == HIDP_STATUS_SUCCESS) {
                bool zl = false;
                bool zr = false;
                for (ULONG i = 0; i < count; i++) {
                    if (usages[i] < buttonUsageMin) { continue; }
                    const auto idx = static_cast<std::uint8_t>(usages[i] - buttonUsageMin);
                    if (switchOrderButtons) {
                        if (idx == 6) {
                            zl = true;
                        } else if (idx == 7) {
                            zr = true;
                        } else {
                            b = static_cast<std::uint16_t>(
                                b | input::usbhid::switchOrderButtonBit(idx));
                        }
                    } else {
                        b = static_cast<std::uint16_t>(b | input::usbhid::layoutButtonBit(idx));
                    }
                }
                if (switchOrderButtons) {
                    s.lt = zl ? 255 : 0;
                    s.rt = zr ? 255 : 0;
                }
            }
        }
        s.wButtons = b;
        return true;
    }
};

// Out of line because HidPDecode is incomplete in the header.
WinHidGateway::Claimed::~Claimed() = default;

WinHidGateway::WinHidGateway() = default;

WinHidGateway::~WinHidGateway() {
    // Release every still-open claim (stops the read loops + closes handles).
    //
    // Drained in place instead of snapshotting the ids into a std::vector first:
    // a destructor is implicitly noexcept, so that vector's growth was a live
    // std::terminate path. Catching the bad_alloc would not have helped either —
    // bailing out of the drain leaves joinable std::threads in claimed_, and
    // ~thread on a joinable thread terminates just the same. map::empty/begin,
    // the id copy, and releaseClaim's own find+erase allocate nothing, so this
    // loop has no throwing step left. mtx_ is still dropped before each call:
    // releaseClaim re-takes it, then joins the reader with the mutex free.
    for (;;) {
        int syntheticId = 0;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (claimed_.empty()) { break; }
            syntheticId = claimed_.begin()->first;
        }
        // Erases the entry it releases, so claimed_ strictly shrinks each pass.
        releaseClaim(syntheticId);
    }
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
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData) != 0;
         ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) { continue; }
        std::vector<std::uint8_t> detailBuf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr) ==
            0) {
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
        bool accepted = false;
        UsbDeviceInfo info;
        if (HidD_GetAttributes(h, &attrs) != 0 && HidD_GetPreparsedData(h, &preparsed) != 0) {
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                // Admission is per model family: the collection the model's
                // parser actually decodes. For everything without a table row
                // that stays "gamepad-shaped"; the Steam Controller's game
                // interface is admitted by model despite its vendor usage page.
                const auto parser =
                    input::usbparse::parserForDevice(attrs.VendorID, attrs.ProductID);
                accepted = collectionMatchesParser(caps, parser);
            }
            HidD_FreePreparsedData(preparsed);
        }
        if (accepted) {
            info.vendorId = attrs.VendorID;
            info.productId = attrs.ProductID;
            // The catalog name is deterministic where one exists (it also names
            // models whose own product string is generic or empty).
            const auto* model = input::usbparse::lookupKnownModel(attrs.VendorID, attrs.ProductID);
            info.name = model != nullptr ? model->name : productName(h, path);
            info.interfaceNumber = 0;
            // The input report length is our max-packet proxy; bInterval is not
            // exposed by the HID class API, so default it to 1ms (the common
            // gamepad case) — the poll-rate sampler measures the real rate.
            info.endpointInMaxPacket =
                caps.InputReportByteLength > 0 ? caps.InputReportByteLength : 64;
            info.endpointInInterval = 1;
            info.hasOutEndpoint = caps.OutputReportByteLength > 0;
            // Derive the IMU from the per-model decoder family so it tracks the
            // parser selection rather than a hard-coded VID list.
            info.hasImu = input::usbparse::parserHasImu(
                input::usbparse::parserForDevice(info.vendorId, info.productId));
        }
        CloseHandle(h);

        // Skip Microsoft-VID gamepad collections: Xbox pads belong to XInput, not
        // this raw-HID path, and shouldn't be fought over.
        if (accepted && info.vendorId != kVidMicrosoft) { out.push_back(std::move(info)); }
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

    const auto parser = input::usbparse::parserForDevice(device.vendorId, device.productId);
    HANDLE opened = INVALID_HANDLE_VALUE;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps{};
    bool sawDevice = false;
    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);
    for (DWORD i = 0; opened == INVALID_HANDLE_VALUE &&
                      SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData) != 0;
         ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) { continue; }
        std::vector<std::uint8_t> detailBuf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr) ==
            0) {
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
        if (HidD_GetAttributes(h, &attrs) != 0 && attrs.VendorID == device.vendorId &&
            attrs.ProductID == device.productId) {
            sawDevice = true;
            // A model can expose several collections under one VID:PID (the
            // Steam Controller's keyboard and mouse ride beside its game
            // interface). Only the collection the parser decodes is the claim.
            PHIDP_PREPARSED_DATA candidate = nullptr;
            HIDP_CAPS candidateCaps{};
            bool matches = false;
            if (HidD_GetPreparsedData(h, &candidate) != 0) {
                matches = HidP_GetCaps(candidate, &candidateCaps) == HIDP_STATUS_SUCCESS &&
                          collectionMatchesParser(candidateCaps, parser);
            }
            if (matches) {
                opened = h;
                preparsed = candidate;
                caps = candidateCaps;
                continue;
            }
            if (candidate != nullptr) { HidD_FreePreparsedData(candidate); }
        }
        CloseHandle(h);
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

    // The Steam Controller ships emulating a keyboard and mouse; quiet mode
    // switches that off and enables the IMU. This is the one family whose init
    // persistently reconfigures the device, so a failure restores the defaults
    // before giving up — a partly-applied init must never strand the pad mute.
    const int featureReportLen = caps.FeatureReportByteLength;
    if (parser == input::usbparse::HidParser::SteamController) {
        if (!runSteamConfig(opened, featureReportLen, input::usbparse::SteamConfig::Quiet)) {
            runSteamConfig(opened, featureReportLen, input::usbparse::SteamConfig::Restore);
            if (preparsed != nullptr) { HidD_FreePreparsedData(preparsed); }
            CloseHandle(opened);
            return ClaimResult::fail(reducer::DirectClaimFailure::InitFailed,
                                     /*frameworkStolen=*/false);
        }
    }

    const int syntheticId = nextSyntheticId_.fetch_sub(1);
    auto claim = std::make_unique<Claimed>();
    claim->path = device.name;
    claim->handle = opened;
    claim->onReport = std::move(onReport);
    claim->vendorId = device.vendorId;
    claim->productId = device.productId;
    claim->parser = parser;
    claim->featureReportLen = featureReportLen;
    claim->outputReportLen = caps.OutputReportByteLength;
    if (parser == input::usbparse::HidParser::GenericHid) {
        // The caps-derived field map replaces the fixed-offset guess wherever
        // the collection declares real usages; the guess stays as the fallback.
        claim->hidp = std::make_unique<HidPDecode>();
        claim->hidp->preparsed = preparsed;
        claim->hidp->build(device.vendorId, device.productId);
        preparsed = nullptr;
    }
    if (preparsed != nullptr) { HidD_FreePreparsedData(preparsed); }
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
    // HidP_GetUsageValue/GetUsages walk preparsed data in user mode — no IO, no
    // allocation. Nothing on the per-report path heap-allocates.
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
        if (ReadFile(handle, buf.data(), static_cast<DWORD>(buf.size()), &read, &ov) == 0) {
            if (GetLastError() != ERROR_IO_PENDING) { break; }
            // Wait with a short timeout so running=false is observed promptly.
            const DWORD w = WaitForSingleObject(ov.hEvent, 100);
            if (w == WAIT_TIMEOUT) {
                CancelIo(handle);
                continue;
            }
            if (GetOverlappedResult(handle, &ov, &read, FALSE) == 0) { break; }
        }
        if (read == 0) { continue; }
        c->completions.fetch_add(1);

        const std::uint8_t* data = buf.data();
        auto len = static_cast<std::size_t>(read);
        if (c->parser == input::usbparse::HidParser::SteamController) {
            // The vendor collection is id-less, so Windows prepends a 0x00
            // report-id byte the wire packet never carried; the decoders expect
            // the packet as it left the device.
            if (len > 1 && data[0] == 0x00) {
                data += 1;
                len -= 1;
            }
            // Dongle connect/disconnect events interleave with input. A
            // returning pad has rebooted (settings gone), so quiet mode is
            // re-applied; a departing pad's last input must not stay latched.
            const auto ev = input::usbparse::checkWirelessEvent(c->parser, data, len);
            if (ev == input::usbparse::WirelessEvent::Connect) {
                runSteamConfig(handle, c->featureReportLen, input::usbparse::SteamConfig::Quiet);
                continue;
            }
            if (ev == input::usbparse::WirelessEvent::Disconnect) {
                if (c->onReport) { c->onReport(UsbReport{}); }
                continue;
            }
        }

        // Decode into the XUSB report. A report that doesn't match the family's
        // shape (wrong id / too short) is skipped rather than published as noise.
        input::usbparse::ParsedReport parsed{};
        bool decoded = false;
        if (c->hidp != nullptr && c->hidp->valid) {
            decoded = c->hidp->decode(data, len, parsed);
        } else {
            decoded = input::usbparse::decodeReport(c->parser, data, len, parsed, c->sticks);
        }
        if (!decoded) { continue; }
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
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        // Quiet mode persists on the device, so every release path restores the
        // stand-alone keyboard/mouse identity before the handle closes; skipping
        // it would hand back a controller that no longer works as a desktop
        // mouse. Best-effort by design — an unplugged pad cannot be written to.
        if (claim->parser == input::usbparse::HidParser::SteamController) {
            runSteamConfig(handle, claim->featureReportLen, input::usbparse::SteamConfig::Restore);
        }
        CloseHandle(handle);
    }
}

std::int64_t WinHidGateway::completionCount(int syntheticId) const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return 0; }
    return it->second->completions.load();
}

bool WinHidGateway::writeOutputReport(int syntheticId, const std::uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) { return false; }
    Claimed* c = nullptr;
    {
        // The claim map's lock is released before the write: a write can block
        // on a sleeping pad, and holding mtx_ across it would stall reconcile().
        // Safe because releaseClaim() joins the reader and erases the entry only
        // from the owner thread, and every caller here is downstream of a live
        // binding for this device.
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = claimed_.find(syntheticId);
        if (it == claimed_.end()) { return false; }
        c = it->second.get();
    }
    if (c->outputReportLen <= 0) { return false; }
    const auto want = static_cast<std::size_t>(c->outputReportLen);
    // A report LONGER than the collection's output length is not ours to
    // truncate: it would reach the pad as a different report.
    if (len > want) { return false; }

    std::lock_guard<std::mutex> lock(c->writeMtx);
    // Exactly OutputReportByteLength, zero-padded. The HID stack rejects any
    // other length outright.
    std::array<std::uint8_t, 256> buf{};
    if (want > buf.size()) { return false; }
    std::memcpy(buf.data(), data, len);

    HANDLE handle = static_cast<HANDLE>(c->handle);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) { return false; }
    // The handle is overlapped, so the write needs its own OVERLAPPED and event
    // or it would complete into the read loop's. Waiting on the event keeps the
    // call synchronous from the caller's point of view without ever blocking
    // the pending ReadFile.
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) { return false; }
    DWORD written = 0;
    bool ok = WriteFile(handle, buf.data(), static_cast<DWORD>(want), &written, &ov) != 0;
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        // A pad that never completes the write must not wedge the caller (the
        // network receive thread); after the timeout the transfer is cancelled
        // and the feedback is simply dropped, which is the right outcome for a
        // lossy telemetry return path.
        if (WaitForSingleObject(ov.hEvent, kOutputWriteTimeoutMs) == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(handle, &ov, &written, FALSE) != 0;
        } else {
            CancelIoEx(handle, &ov);
            GetOverlappedResult(handle, &ov, &written, TRUE);
            ok = false;
        }
    }
    CloseHandle(ov.hEvent);
    return ok && written == static_cast<DWORD>(want);
}

} // namespace dish::source::usb
