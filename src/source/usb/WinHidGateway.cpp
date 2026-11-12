// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/WinHidGateway.h"

#include "core/input/HidTransport.h"
#include "core/input/UsbHidLayout.h"
#include "core/input/UsbOutputReports.h"
#include "core/input/UsbReportParsers.h"
#include "source/system/ThreadPriority.h"

// <windows.h> first (NOMINMAX / WIN32_LEAN_AND_MEAN come from the build defs),
// then the HID + SetupAPI headers, which depend on the base Win32 types.
#include <windows.h>

#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace dish::source::usb {

namespace {

// HID usage page / usages for game controllers (HID Usage Tables §4).
// How long an OUT report may sit unacknowledged before it is abandoned. Well
// under the 2 s heartbeat, so a wedged pad can never back the feedback caller
// up into the session's liveness.

constexpr USAGE kUsagePageGenericDesktop = 0x01;
constexpr USAGE kUsageJoystick = 0x04;
constexpr USAGE kUsageGamepad = 0x05;
// The Steam Controller's game interface is vendor-defined HID with no gamepad
// usages, so Android never enumerates it as a gamepad and neither does this
// filter; it is admitted by model, not by shape.
constexpr USAGE kUsagePageVendor = 0xFF00;

// A path with no interface part, which no Steam Controller receiver's pad has.
constexpr int kNoInterface = -1;

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
// vendor-defined game interface for the Steam Controllers (their keyboard and
// mouse collections share the VID:PID), a gamepad/joystick collection for
// every other family. The 2015 pad's is on page 0xFF00. The 2026 pad's is taken
// to be anywhere on the vendor-defined pages (0xFF00 to 0xFFFF), which no pad
// has confirmed yet: SDL reads it without looking at the page.
bool collectionMatchesParser(const HIDP_CAPS& caps, input::usbparse::HidParser parser) {
    if (parser == input::usbparse::HidParser::SteamController) {
        return caps.UsagePage == kUsagePageVendor;
    }
    if (parser == input::usbparse::HidParser::SteamTriton) {
        return caps.UsagePage >= kUsagePageVendor;
    }
    return caps.UsagePage == kUsagePageGenericDesktop &&
           (caps.Usage == kUsageGamepad || caps.Usage == kUsageJoystick);
}

// Whether the collection at this path carries a pad: every one does but a Steam Controller
// receiver's keyboard, comms and pogo-pin interfaces, whose pads ride its slots only.
bool pathCarriesPad(const std::string& path, int vendorId, int productId) {
    if (!input::usbparse::isReceiver(vendorId, productId)) { return true; }
    const int iface = input::hidInterfaceNumberFromPath(path).value_or(kNoInterface);
    return input::usbparse::interfaceCarriesPad(vendorId, productId, iface);
}

// A receiver's claim opens the slot it was asked for, because each slot is a pad of its own; any
// other model's opens the first collection that carries its pad.
bool pathIsTheClaimedPad(const std::string& path, const UsbDeviceInfo& device) {
    if (!input::usbparse::isReceiver(device.vendorId, device.productId)) { return true; }
    const int iface = input::hidInterfaceNumberFromPath(path).value_or(kNoInterface);
    return iface == device.interfaceNumber;
}

// A 2015 receiver's slot answers with the connect or disconnect event for its controller, on
// the read loop. The one feature write a claim makes; a pad's settings are its output worker's.
bool askSteamReceiverSlot(HidReportWriter& writer) {
    std::array<std::uint8_t, input::usbparse::kSteamFeatureReportBytes> report{};
    const std::size_t n =
        input::usbparse::buildSteamReceiverStateReport(report.data(), report.size());
    const int attempts =
        input::usbparse::configWriteAttempts(input::usbparse::HidParser::SteamController);
    return writeFeatureWithRetry(writer, report.data(), n, attempts);
}

// A claim given up after the open: the handle and the preparsed data go with it.
ClaimResult abandonClaim(HANDLE opened, PHIDP_PREPARSED_DATA preparsed) {
    if (preparsed != nullptr) { HidD_FreePreparsedData(preparsed); }
    CloseHandle(opened);
    return ClaimResult::fail(reducer::DirectClaimFailure::InitFailed, /*frameworkStolen=*/false);
}

// The DualSense's firmware-info feature report, which picks its rumble mode. HidD_GetFeature wants
// the collection's whole feature length and reports no length of its own, so the report's 64 bytes
// are read off the front of that; an unanswered read keeps the pad on the original mode.
input::usbout::Ds5FirmwareInfo readDs5FirmwareInfo(HANDLE h, int featureLen) {
    std::array<std::uint8_t, 128> buf{};
    const bool lengthFits =
        featureLen >= static_cast<int>(input::usbout::kDs5FirmwareInfoReportBytes) &&
        static_cast<std::size_t>(featureLen) <= buf.size();
    if (!lengthFits) { return input::usbout::Ds5FirmwareInfo{}; }
    buf[0] = input::usbout::kDs5FirmwareInfoReportId;
    if (HidD_GetFeature(h, buf.data(), static_cast<ULONG>(featureLen)) == 0) {
        return input::usbout::Ds5FirmwareInfo{};
    }
    return input::usbout::parseDs5FirmwareInfo(buf.data(),
                                               input::usbout::kDs5FirmwareInfoReportBytes);
}

// The interface's wide device path, or empty when the two-call size query does not produce one.
// Wide because CreateFileW wants it that way; the utf8 form is only for the admission checks.
std::wstring interfacePathOf(HDEVINFO devInfo, SP_DEVICE_INTERFACE_DATA& ifData) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);
    if (needed == 0) { return {}; }
    std::vector<std::uint8_t> detailBuf(needed);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr) == 0) {
        return {};
    }
    return detail->DevicePath;
}

// Admission is per model family: the collection the model's parser actually decodes. Everything
// without a table row has to stay "gamepad-shaped"; the Steam Controller's game interface is
// admitted by model despite its vendor usage page.
bool isClaimCandidate(HANDLE h, HIDD_ATTRIBUTES& attrs, HIDP_CAPS& caps) {
    if (HidD_GetAttributes(h, &attrs) == 0) { return false; }
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(h, &preparsed) == 0) { return false; }
    bool accepted = false;
    if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
        const auto parser = input::usbparse::parserForDevice(attrs.VendorID, attrs.ProductID);
        accepted = collectionMatchesParser(caps, parser);
    }
    HidD_FreePreparsedData(preparsed);
    return accepted;
}

// What the claim machinery needs to know about a device it has not opened for reading yet.
UsbDeviceInfo describeDevice(HANDLE h, const std::string& path, const HIDD_ATTRIBUTES& attrs,
                             const HIDP_CAPS& caps) {
    UsbDeviceInfo info;
    info.vendorId = attrs.VendorID;
    info.productId = attrs.ProductID;
    // The catalog name is deterministic where one exists; it also names models whose own product
    // string is generic or empty.
    const auto* model = input::usbparse::lookupKnownModel(attrs.VendorID, attrs.ProductID);
    info.name = model != nullptr ? model->name : productName(h, path);
    info.interfaceNumber = input::hidInterfaceNumberFromPath(path).value_or(0);
    // The input report length is the max-packet proxy; bInterval is not exposed by the HID class
    // API, so it defaults to 1 ms (the common gamepad case) and the poll-rate sampler measures the
    // real rate.
    info.endpointInMaxPacket = caps.InputReportByteLength > 0 ? caps.InputReportByteLength : 64;
    info.endpointInInterval = 1;
    info.hasOutEndpoint = caps.OutputReportByteLength > 0;
    // Derived from the per-model decoder family so it tracks the parser selection rather than a
    // hard-coded VID list.
    info.hasImu = input::usbparse::parserHasImu(
        input::usbparse::parserForDevice(info.vendorId, info.productId));
    return info;
}

// Null for an interface that is not a claim candidate at all.
std::optional<UsbDeviceInfo> probeInterface(const std::wstring& widePath) {
    const std::string path = wideToUtf8(widePath.c_str());
    // A Bluetooth-connected pad is a HID device too (same VID:PID as its USB identity) but is NOT
    // a USB-direct claim candidate: the raw-HID claim is a USB feature, the per-model decoders
    // parse the USB report layout (the BT layout differs, since a DS4 streams the short 0x01
    // report until a feature-report handshake), and tracking it would grow a bogus "USB PATH"
    // control on a wireless pad. Skipped before probing.
    if (input::isBluetoothHidDevicePath(path)) { return std::nullopt; }

    // Opened for query only, with no read/write share, so probing does not disturb other readers.
    // The actual claim re-opens with read access.
    HANDLE h = CreateFileW(widePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { return std::nullopt; }

    HIDD_ATTRIBUTES attrs{};
    attrs.Size = sizeof(attrs);
    HIDP_CAPS caps{};
    std::optional<UsbDeviceInfo> info;
    if (isClaimCandidate(h, attrs, caps) && pathCarriesPad(path, attrs.VendorID, attrs.ProductID)) {
        info = describeDevice(h, path, attrs, caps);
    }
    CloseHandle(h);
    return info;
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
    input::usbhid::ButtonOrder order = input::usbhid::ButtonOrder::Western;

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

        order = input::usbparse::buttonOrderForDevice(vendorId, productId);
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

        using input::usbhid::axisIsDirection;
        using input::usbhid::directionBits;
        using input::usbhid::scaleAxis16;
        using input::usbhid::scaleTrig8;
        std::uint16_t b = 0;
        if (axisIsDirection(lx.scale)) {
            b = static_cast<std::uint16_t>(
                b | directionBits(static_cast<std::uint32_t>(readValue(lx, report, rlen)), lx.scale,
                                  static_cast<std::uint16_t>(input::layout::kXusbDpadLeft),
                                  static_cast<std::uint16_t>(input::layout::kXusbDpadRight)));
        } else if (lx.present) {
            s.lx = scaleAxis16(static_cast<std::uint32_t>(readValue(lx, report, rlen)), lx.scale,
                               false);
        }
        if (axisIsDirection(ly.scale)) {
            b = static_cast<std::uint16_t>(
                b | directionBits(static_cast<std::uint32_t>(readValue(ly, report, rlen)), ly.scale,
                                  static_cast<std::uint16_t>(input::layout::kXusbDpadUp),
                                  static_cast<std::uint16_t>(input::layout::kXusbDpadDown)));
        } else if (ly.present) {
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
                const input::usbhid::ButtonOrderMap& map = input::usbhid::buttonOrderMap(order);
                bool leftTrigger = false;
                bool rightTrigger = false;
                for (ULONG i = 0; i < count; i++) {
                    if (usages[i] < buttonUsageMin) { continue; }
                    const auto index = static_cast<USAGE>(usages[i] - buttonUsageMin);
                    if (index >= input::usbhid::kMaxButtons) { continue; }
                    const auto idx = static_cast<std::uint8_t>(index);
                    if (idx == map.leftTriggerIndex) {
                        leftTrigger = true;
                    } else if (idx == map.rightTriggerIndex) {
                        rightTrigger = true;
                    } else {
                        b = static_cast<std::uint16_t>(b | map.bits[idx]);
                    }
                }
                // A digital trigger is the whole truth about that trigger: an analog usage
                // beside it only echoes the same button.
                if (map.leftTriggerIndex != input::usbhid::kNoDigitalTrigger) {
                    s.lt = leftTrigger ? 255 : 0;
                    s.rt = rightTrigger ? 255 : 0;
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
            std::lock_guard<thread::TrackedMutex> lock(mtx_);
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
        const std::wstring widePath = interfacePathOf(devInfo, ifData);
        if (widePath.empty()) { continue; }
        auto info = probeInterface(widePath);
        if (!info.has_value()) { continue; }
        // Microsoft-VID gamepad collections are skipped: Xbox pads belong to XInput, not this
        // raw-HID path, and should not be fought over.
        if (info->vendorId == kVidMicrosoft) { continue; }
        out.push_back(std::move(*info));
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
        // interface — enumerate() filtered it, so the claim must match. A
        // receiver's claim takes only the slot it was asked for.
        const std::string utf8Path = wideToUtf8(detail->DevicePath);
        if (input::isBluetoothHidDevicePath(utf8Path)) { continue; }
        if (!pathIsTheClaimedPad(utf8Path, device)) { continue; }
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

    // A Steam Controller's settings (lizard mode off, the IMU on) are its output
    // worker's to put on the pad and to take off again, so the claim writes
    // nothing to a pad. A 2015 receiver's slot is asked what it holds, the way
    // SDL's InitDevice and hid-steam's probe start a receiver; the answer is its
    // controller's connect or disconnect event, on the read loop.
    const int featureReportLen = caps.FeatureReportByteLength;
    const bool isSteam2015 = parser == input::usbparse::HidParser::SteamController;
    const bool receiverSlot = input::usbparse::isReceiver(device.vendorId, device.productId);
    auto claim = std::make_unique<Claimed>();
    if (isSteam2015 && receiverSlot) {
        HandleReportWriter writer(
            opened, 0, static_cast<std::size_t>(std::max(featureReportLen, 0)), &claim->writeMtx);
        if (!askSteamReceiverSlot(writer)) { return abandonClaim(opened, preparsed); }
    }

    // Read once at the claim, the way hid-playstation reads it at probe.
    const input::usbout::Ds5FirmwareInfo ds5Firmware =
        parser == input::usbparse::HidParser::DualSense
            ? readDs5FirmwareInfo(opened, featureReportLen)
            : input::usbout::Ds5FirmwareInfo{};

    const int syntheticId = nextSyntheticId_.fetch_sub(1);
    claim->path = device.name;
    claim->handle = opened;
    claim->onReport = std::move(onReport);
    claim->vendorId = device.vendorId;
    claim->productId = device.productId;
    claim->parser = parser;
    claim->featureReportLen = featureReportLen;
    claim->linked.store(!receiverSlot);
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
        std::lock_guard<thread::TrackedMutex> lock(mtx_);
        claimed_.emplace(syntheticId, std::move(claim));
    }
    raw->reader = std::thread([this, raw] { readLoop(raw); });
    return ClaimResult::success(syntheticId, ds5Firmware);
}

namespace {

// Short enough that running=false is observed promptly, long enough that an idle pad does not spin
// the read thread.
constexpr DWORD kReadWaitMs = 100;

// What one overlapped read came back with. Idle covers both a wait timeout and a zero-length
// read: neither is an error and neither carries a report.
enum class ReadOutcome : std::uint8_t { Report, Idle, Stop };

ReadOutcome readOneReport(HANDLE handle, OVERLAPPED& ov, std::array<std::uint8_t, 128>& buf,
                          DWORD& read) {
    read = 0;
    ResetEvent(ov.hEvent);
    if (ReadFile(handle, buf.data(), static_cast<DWORD>(buf.size()), &read, &ov) == 0) {
        if (GetLastError() != ERROR_IO_PENDING) { return ReadOutcome::Stop; }
        if (WaitForSingleObject(ov.hEvent, kReadWaitMs) == WAIT_TIMEOUT) {
            CancelIo(handle);
            return ReadOutcome::Idle;
        }
        if (GetOverlappedResult(handle, &ov, &read, FALSE) == 0) { return ReadOutcome::Stop; }
    }
    if (read == 0) { return ReadOutcome::Idle; }
    return ReadOutcome::Report;
}

// The Steam Controller's vendor collection is id-less, so Windows prepends a 0x00 report-id byte
// the wire packet never carried; the decoders expect the packet as it left the device.
void stripPrependedReportId(const std::uint8_t*& data, std::size_t& len) {
    if (len > 1 && data[0] == 0x00) {
        data += 1;
        len -= 1;
    }
}

// Straight field copy: ParsedReport is the decoder's shape and UsbReport is the gateway's.
// micMuted is the latch rather than the button, mirrored up so the driver can edge-detect it.
UsbReport toUsbReport(const input::usbparse::ParsedReport& parsed, const bool micMuted) {
    UsbReport report{};
    report.wButtons = parsed.wButtons;
    report.micMuted = micMuted;
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
    report.batteryValid = parsed.batteryValid;
    report.batteryLevel = parsed.batteryLevel;
    report.batteryStatus = parsed.batteryStatus;
    return report;
}

} // namespace

// True when the packet was a dongle event rather than input, and the loop should take the next
// report. Connect and disconnect interleave with input on the Steam dongle.
bool WinHidGateway::handleSteamWirelessEvent(Claimed* c, const std::uint8_t* data,
                                             std::size_t len) {
    switch (input::usbparse::checkWirelessEvent(c->parser, data, len)) {
    case input::usbparse::WirelessEvent::Connect:
        controllerArrived(c);
        return true;
    case input::usbparse::WirelessEvent::Disconnect:
        controllerLeft(c);
        return true;
    case input::usbparse::WirelessEvent::None:
        return false;
    }
    return false;
}

// A returning pad has rebooted, so its settings are gone: the manager has the slot's output
// worker put them back, off this thread, which only says the pad is there.
void WinHidGateway::controllerArrived(Claimed* c) {
    c->linked.store(true, std::memory_order_release);
    if (c->onReport) { c->onReport(padLinkReport(PadLinkEvent::ControllerArrived)); }
}

// What a controller's leaving means for its last input, its readings and its settings is the
// manager's (UsbGamepadManager's publisher, and the slot's output worker).
void WinHidGateway::controllerLeft(Claimed* c) {
    c->linked.store(false, std::memory_order_release);
    if (c->onReport) { c->onReport(padLinkReport(PadLinkEvent::ControllerLeft)); }
}

// A descriptor-driven layout wins where the device published one; everything else goes through the
// per-model decoder chosen at claim time.
bool WinHidGateway::decodeOneReport(Claimed* c, const std::uint8_t* data, std::size_t len,
                                    input::usbparse::ParsedReport& parsed) {
    if (c->hidp != nullptr && c->hidp->valid) { return c->hidp->decode(data, len, parsed); }
    return input::usbparse::decodeReport(c->parser, data, len, parsed, c->sticks, &c->micMute);
}

// Plain C++ on its own thread: no allocation per report, no Qt. Each HID input report is decoded
// into a normalised XUSB report via the pure core/input/UsbReportParsers decoders and handed to
// onReport, which publishes through GamepadInputProcessor.
//
// Allocation discipline: the read buffer and the ParsedReport scratch live on this thread's stack
// and are reused every iteration; the decoder is a pure function over those, mutating the device's
// stick auto-range state in place. HidP_GetUsageValue/GetUsages walk preparsed data in user mode,
// with no IO and no allocation. Nothing on the per-report path heap-allocates.
//
// NOTE: the button/stick/trigger byte offsets mirror dish-android's usb_parsers.cpp 1:1
// (hardware-validated there). The DS4/DualSense IMU and touchpad offsets are the public
// hid-playstation layout and need a final sign/scale check against real pads (flagged in
// UsbReportParsers.h).
void WinHidGateway::readLoop(Claimed* c) {
    const thread::ScopedThreadRole role(thread::ThreadRole::UsbReader);
    const ScopedThreadPriority priority(thread::ThreadRole::UsbReader);
    auto* handle = static_cast<HANDLE>(c->handle);
    std::array<std::uint8_t, 128> buf{};
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (c->running.load()) {
        DWORD read = 0;
        const ReadOutcome outcome = readOneReport(handle, ov, buf, read);
        if (outcome == ReadOutcome::Stop) { break; }
        if (outcome == ReadOutcome::Idle) { continue; }
        c->completions.fetch_add(1);

        const std::uint8_t* data = buf.data();
        auto len = static_cast<std::size_t>(read);
        if (c->parser == input::usbparse::HidParser::SteamController) {
            stripPrependedReportId(data, len);
        }
        if (input::usbparse::parserHasWirelessEvents(c->parser) &&
            handleSteamWirelessEvent(c, data, len)) {
            continue;
        }

        // A report that does not match the family's shape (wrong id, too short) is skipped rather
        // than published as noise.
        input::usbparse::ParsedReport parsed{};
        if (!decodeOneReport(c, data, len, parsed)) { continue; }
        c->decoded.fetch_add(1);
        // A report means a controller is there, its connect event missed or not.
        if (!c->linked.load(std::memory_order_relaxed)) { controllerArrived(c); }
        if (c->onReport) {
            c->onReport(toUsbReport(parsed, c->micMute.muted.load(std::memory_order_relaxed)));
        }
    }
    if (ov.hEvent != nullptr) { CloseHandle(ov.hEvent); }
    c->alive.store(false);
}

void WinHidGateway::releaseClaim(int syntheticId) {
    std::unique_ptr<Claimed> claim;
    {
        std::lock_guard<thread::TrackedMutex> lock(mtx_);
        const auto it = claimed_.find(syntheticId);
        if (it == claimed_.end()) { return; }
        claim = std::move(it->second);
        claimed_.erase(it);
    }
    claim->running.store(false);
    auto* handle = static_cast<HANDLE>(claim->handle);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) { CancelIoEx(handle, nullptr); }
    if (claim->reader.joinable()) { claim->reader.join(); }
    // The settings the pad held of ours came off it when the manager stopped the claim's output
    // worker, before this; nothing is written to the pad here.
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); }
}

bool WinHidGateway::isClaimAlive(int syntheticId) const {
    std::lock_guard<thread::TrackedMutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return false; }
    return it->second->alive.load();
}

bool WinHidGateway::isPadLinked(int syntheticId) const {
    std::lock_guard<thread::TrackedMutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return false; }
    return it->second->linked.load(std::memory_order_acquire);
}

std::int64_t WinHidGateway::completionCount(int syntheticId) const {
    std::lock_guard<thread::TrackedMutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return 0; }
    return it->second->completions.load();
}

std::int64_t WinHidGateway::decodedCount(int syntheticId) const {
    std::lock_guard<thread::TrackedMutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return 0; }
    return it->second->decoded.load();
}

bool WinHidGateway::setPadMicMuted(int syntheticId, bool muted) {
    std::lock_guard<thread::TrackedMutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    if (it == claimed_.end()) { return false; }
    if (it->second->parser != input::usbparse::HidParser::DualSense) { return false; }
    // The latch alone, never `held`: a UI toggle is not a button press, and
    // touching the edge tracker could swallow or double a real press racing in.
    it->second->micMute.muted.store(muted, std::memory_order_relaxed);
    return true;
}

// Null when nothing claims the id. The claim map's lock is released before the caller writes: a
// write can block on a sleeping pad, and holding mtx_ across it would stall reconcile(). Safe
// because releaseClaim() joins the reader and erases the entry only from the owner thread, and
// every caller here is downstream of a live binding for this device.
WinHidGateway::Claimed* WinHidGateway::claimedFor(int syntheticId) const {
    std::lock_guard<thread::TrackedMutex> lock(mtx_);
    const auto it = claimed_.find(syntheticId);
    return it == claimed_.end() ? nullptr : it->second.get();
}

// The claim's own handle and write lock; the manager stops the worker that holds the writer
// before it releases the claim, which is what closes the handle. A collection with feature
// reports but no OUT report (the 2015 Steam Controller's) gets the writer for its settings.
std::unique_ptr<HidReportWriter> WinHidGateway::openReportWriter(int syntheticId) {
    Claimed* c = claimedFor(syntheticId);
    if (c == nullptr) { return UsbDeviceGateway::openReportWriter(syntheticId); }
    const bool takesReports = c->outputReportLen > 0 || c->featureReportLen > 0;
    if (!takesReports) { return UsbDeviceGateway::openReportWriter(syntheticId); }
    return std::make_unique<HandleReportWriter>(
        c->handle, static_cast<std::size_t>(c->outputReportLen),
        static_cast<std::size_t>(std::max(c->featureReportLen, 0)), &c->writeMtx);
}

bool WinHidGateway::writeOutputReport(int syntheticId, const std::uint8_t* data, std::size_t len) {
    Claimed* c = claimedFor(syntheticId);
    if (c == nullptr || c->outputReportLen <= 0) { return false; }
    return HandleReportWriter(c->handle, static_cast<std::size_t>(c->outputReportLen),
                              static_cast<std::size_t>(std::max(c->featureReportLen, 0)),
                              &c->writeMtx)
        .write(data, len);
}

} // namespace dish::source::usb
