// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Reaching a USB pad's own descriptors on Windows with no driver of ours and no elevation, the
// way Microsoft's USBView sample and libusb's Windows backend do: the pad's parent hub answers
// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION for the port the pad sits on. Decided here, pure:
// which port the device tree's strings name, which class a USB interface node's compatible id
// names, and the request's bytes. Walking the tree and asking the hub is
// source/usb/WinUsbHardware's.

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace dish::input::winusb {

namespace detail {

inline std::string lowered(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower;
}

// A port number in decimal, ending at `terminator` or at the end of the text. Ports count from 1,
// and libusb refuses ULONG_MAX, so the range is 1 to ULONG_MAX - 1.
inline std::optional<std::uint32_t> portNumberEndingAt(std::string_view text, char terminator) {
    constexpr std::uint64_t kLastPort = 0xFFFFFFFE;
    std::uint64_t value = 0;
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(begin, end, value);
    const bool readDigits = error == std::errc{} && stop != begin;
    const bool endsThere = stop == end || *stop == terminator;
    const bool inRange = value >= 1 && value <= kLastPort;
    if (!readDigits || !endsThere || !inRange) { return std::nullopt; }
    return static_cast<std::uint32_t>(value);
}

// Two hex digits at `at`.
inline std::optional<std::uint8_t> hexByteAt(std::string_view text, std::size_t at) {
    constexpr std::size_t kDigits = 2;
    constexpr int kHex = 16;
    if (at + kDigits > text.size()) { return std::nullopt; }
    unsigned value = 0;
    const char* const first = text.data() + at;
    const auto [stop, error] = std::from_chars(first, first + kDigits, value, kHex);
    if (error != std::errc{} || stop != first + kDigits) { return std::nullopt; }
    return static_cast<std::uint8_t>(value);
}

inline void putLittle16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t value) {
    constexpr unsigned kByte = 8;
    out[at] = static_cast<std::uint8_t>(value & 0xFFU);
    out[at + 1] = static_cast<std::uint8_t>(value >> kByte);
}

inline void putLittle32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    constexpr unsigned kHalf = 16;
    putLittle16(out, at, static_cast<std::uint16_t>(value & 0xFFFFU));
    putLittle16(out, at + 2, static_cast<std::uint16_t>(value >> kHalf));
}

} // namespace detail

// ── The port the device sits on ─────────────────────────────────────────────
// libusb's get_dev_port_number reads it from the device node in this order, and so does
// WinUsbHardware: the location information, the location path, then the address.

// CM_DRP_LOCATION_INFORMATION: "Port_#0002.Hub_#000D" is port 2. Any other shape names none.
inline std::optional<std::uint32_t> portFromLocationInformation(std::string_view text) {
    constexpr std::string_view kPrefix = "Port_#";
    if (!text.starts_with(kPrefix)) { return std::nullopt; }
    return detail::portNumberEndingAt(text.substr(kPrefix.size()), '.');
}

// The first of CM_DRP_LOCATION_PATHS: the number in its last "#USB(x)" step, so
// "PCIROOT(B2)#PCI(0300)#PCI(0000)#USBROOT(0)#USB(1)#USB(2)#USBMI(3)" is port 2.
inline std::optional<std::uint32_t> portFromLocationPath(std::string_view path) {
    constexpr std::string_view kStep = "#USB(";
    const std::size_t at = path.rfind(kStep);
    if (at == std::string_view::npos) { return std::nullopt; }
    return detail::portNumberEndingAt(path.substr(at + kStep.size()), ')');
}

// ── An interface's class ────────────────────────────────────────────────────

struct InterfaceClass {
    std::uint8_t classCode = 0;
    std::uint8_t subclass = 0;
    std::uint8_t protocol = 0;

    bool operator==(const InterfaceClass&) const = default;
};

// "USB\Class_03&SubClass_00&Prot_00", the compatible id that names all three codes (Microsoft's
// "Standard USB Identifiers"); the shorter ones beside it name fewer.
inline std::optional<InterfaceClass> interfaceClassOf(std::string_view compatibleId) {
    constexpr std::string_view kClass = "usb\\class_";
    constexpr std::string_view kSubclass = "&subclass_";
    constexpr std::string_view kProtocol = "&prot_";
    constexpr std::size_t kCodeDigits = 2;
    constexpr std::size_t kClassAt = kClass.size();
    constexpr std::size_t kSubclassTagAt = kClassAt + kCodeDigits;
    constexpr std::size_t kSubclassAt = kSubclassTagAt + kSubclass.size();
    constexpr std::size_t kProtocolTagAt = kSubclassAt + kCodeDigits;
    constexpr std::size_t kProtocolAt = kProtocolTagAt + kProtocol.size();
    constexpr std::size_t kLength = kProtocolAt + kCodeDigits;
    const std::string id = detail::lowered(compatibleId);
    const std::string_view text(id);
    const bool shaped = text.size() == kLength && text.starts_with(kClass) &&
                        text.substr(kSubclassTagAt, kSubclass.size()) == kSubclass &&
                        text.substr(kProtocolTagAt, kProtocol.size()) == kProtocol;
    if (!shaped) { return std::nullopt; }
    const std::optional<std::uint8_t> classCode = detail::hexByteAt(text, kClassAt);
    const std::optional<std::uint8_t> subclass = detail::hexByteAt(text, kSubclassAt);
    const std::optional<std::uint8_t> protocol = detail::hexByteAt(text, kProtocolAt);
    if (!classCode.has_value() || !subclass.has_value() || !protocol.has_value()) {
        return std::nullopt;
    }
    return InterfaceClass{*classCode, *subclass, *protocol};
}

// A node's CM_DRP_COMPATIBLEIDS, in their order.
inline std::optional<InterfaceClass>
firstInterfaceClassOf(const std::vector<std::string>& compatibleIds) {
    for (const std::string& id : compatibleIds) {
        const std::optional<InterfaceClass> named = interfaceClassOf(id);
        if (named.has_value()) { return named; }
    }
    return std::nullopt;
}

// ── The hub request ─────────────────────────────────────────────────────────

// GET_DESCRIPTOR (USB 2.0 §9.4.3), and the HID class's report descriptor (HID 1.11 §7.1.1).
inline constexpr std::uint8_t kGetDescriptor = 0x06;
inline constexpr std::uint8_t kStandardDeviceIn = 0x80;
inline constexpr std::uint8_t kStandardInterfaceIn = 0x81;
inline constexpr std::uint8_t kDeviceDescriptorType = 0x01;
inline constexpr std::uint8_t kConfigurationDescriptorType = 0x02;
inline constexpr std::uint8_t kHidReportDescriptorType = 0x22;
inline constexpr std::uint16_t kDeviceDescriptorBytes = 18;
inline constexpr std::uint16_t kConfigurationHeaderBytes = 9;
// What hidapi asks for when it has not read the exact length (HID_API_MAX_REPORT_DESCRIPTOR_SIZE).
// A device answers with what it has, as GET_DESCRIPTOR always may.
inline constexpr std::uint16_t kReportDescriptorRequestBytes = 4096;

struct SetupPacket {
    std::uint8_t bmRequest = kStandardDeviceIn;
    std::uint8_t bRequest = kGetDescriptor;
    std::uint16_t wValue = 0;
    std::uint16_t wIndex = 0;
    std::uint16_t wLength = 0;

    bool operator==(const SetupPacket&) const = default;
};

constexpr std::uint16_t descriptorValue(std::uint8_t type, std::uint8_t index) {
    constexpr unsigned kTypeShift = 8;
    return static_cast<std::uint16_t>((static_cast<unsigned>(type) << kTypeShift) | index);
}

constexpr SetupPacket deviceDescriptorRequest() {
    return {kStandardDeviceIn, kGetDescriptor, descriptorValue(kDeviceDescriptorType, 0), 0,
            kDeviceDescriptorBytes};
}

constexpr SetupPacket configurationDescriptorRequest(std::uint16_t length) {
    return {kStandardDeviceIn, kGetDescriptor, descriptorValue(kConfigurationDescriptorType, 0), 0,
            length};
}

// The interface is the recipient, as HID 1.11 §7.1.1 has it. The hub request cannot carry that:
// the USB stack ignores bmRequest and sends 0x80 (Microsoft's USB_DESCRIPTOR_REQUEST reference),
// so whether a pad answers is its firmware's choice, and its refusal is a fact to record.
constexpr SetupPacket reportDescriptorRequest(std::uint8_t interfaceNumber, std::uint16_t length) {
    return {kStandardInterfaceIn, kGetDescriptor, descriptorValue(kHidReportDescriptorType, 0),
            interfaceNumber, length};
}

// USB_DESCRIPTOR_REQUEST (usbioctl.h): the ULONG connection index, the eight setup bytes, then
// the room the descriptor lands in, little-endian as every Windows target is.
inline constexpr std::size_t kDescriptorRequestHeaderBytes = 12;
inline constexpr std::size_t kConnectionIndexAt = 0;
inline constexpr std::size_t kBmRequestAt = 4;
inline constexpr std::size_t kBRequestAt = 5;
inline constexpr std::size_t kWValueAt = 6;
inline constexpr std::size_t kWIndexAt = 8;
inline constexpr std::size_t kWLengthAt = 10;

inline std::vector<std::uint8_t> descriptorRequestBuffer(std::uint32_t connectionIndex,
                                                         const SetupPacket& setup) {
    std::vector<std::uint8_t> buffer(kDescriptorRequestHeaderBytes + setup.wLength, 0);
    detail::putLittle32(buffer, kConnectionIndexAt, connectionIndex);
    buffer[kBmRequestAt] = setup.bmRequest;
    buffer[kBRequestAt] = setup.bRequest;
    detail::putLittle16(buffer, kWValueAt, setup.wValue);
    detail::putLittle16(buffer, kWIndexAt, setup.wIndex);
    detail::putLittle16(buffer, kWLengthAt, setup.wLength);
    return buffer;
}

// What the hub wrote after the header. `bytesReturned` counts the header too, and an answer no
// longer than the header carries no descriptor.
inline std::vector<std::uint8_t> descriptorOfReply(const std::vector<std::uint8_t>& buffer,
                                                   std::size_t bytesReturned) {
    const std::size_t end = std::min(bytesReturned, buffer.size());
    if (end <= kDescriptorRequestHeaderBytes) { return {}; }
    return {buffer.begin() + static_cast<std::ptrdiff_t>(kDescriptorRequestHeaderBytes),
            buffer.begin() + static_cast<std::ptrdiff_t>(end)};
}

// wTotalLength from a configuration descriptor's first nine bytes (USB 2.0 §9.6.3): what to ask
// for the second time, as USBView's GetConfigDescriptor does, and refused the same way when it is
// shorter than the configuration descriptor itself.
inline std::optional<std::uint16_t>
configurationTotalLength(const std::vector<std::uint8_t>& head) {
    constexpr std::size_t kTypeAt = 1;
    constexpr std::size_t kTotalLengthAt = 2;
    constexpr unsigned kByte = 8;
    const bool isAConfiguration =
        head.size() >= kConfigurationHeaderBytes && head[kTypeAt] == kConfigurationDescriptorType;
    if (!isAConfiguration) { return std::nullopt; }
    const auto total = static_cast<std::uint16_t>(
        head[kTotalLengthAt] | (static_cast<unsigned>(head[kTotalLengthAt + 1]) << kByte));
    if (total < kConfigurationHeaderBytes) { return std::nullopt; }
    return total;
}

} // namespace dish::input::winusb
