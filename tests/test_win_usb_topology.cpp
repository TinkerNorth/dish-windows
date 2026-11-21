// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Reaching a pad's own descriptors through its parent hub: the device tree's strings and the
// request's bytes. The location strings are the forms libusb's Windows backend reads
// (get_dev_port_number, libusb/os/windows_winusb.c), the compatible ids the forms Microsoft's
// "Standard USB Identifiers" page lists, the request the USB_DESCRIPTOR_REQUEST layout
// (usbioctl.h).

#include "core/input/WinUsbTopology.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace winusb = dish::input::winusb;

namespace {

using Bytes = std::vector<std::uint8_t>;

// A request buffer's header: the connection index and the setup packet.
Bytes headerOf(const Bytes& buffer) {
    return {buffer.begin(),
            buffer.begin() + static_cast<std::ptrdiff_t>(winusb::kDescriptorRequestHeaderBytes)};
}

bool allZeroAfterHeader(const Bytes& buffer) {
    for (std::size_t i = winusb::kDescriptorRequestHeaderBytes; i < buffer.size(); ++i) {
        if (buffer[i] != 0) { return false; }
    }
    return true;
}

// What the hub hands back: the request's header with the descriptor written after it.
Bytes replyOf(const Bytes& request, const Bytes& descriptor) {
    Bytes reply = request;
    reply.resize(winusb::kDescriptorRequestHeaderBytes + descriptor.size());
    std::copy(descriptor.begin(), descriptor.end(),
              reply.begin() + static_cast<std::ptrdiff_t>(winusb::kDescriptorRequestHeaderBytes));
    return reply;
}

} // namespace

// ── The port the device sits on ─────────────────────────────────────────────

TEST_CASE("usb topology: the location information names the hub port", "[winusb]") {
    CHECK(winusb::portFromLocationInformation("Port_#0002.Hub_#000D") ==
          std::optional<std::uint32_t>(2));
    CHECK(winusb::portFromLocationInformation("Port_#0011.Hub_#0004") ==
          std::optional<std::uint32_t>(11));
    // A port alone, with no hub after it, still names the port, as libusb reads it.
    CHECK(winusb::portFromLocationInformation("Port_#0003") == std::optional<std::uint32_t>(3));
}

TEST_CASE("usb topology: location information of any other shape names no port", "[winusb]") {
    // Ports count from 1.
    CHECK_FALSE(winusb::portFromLocationInformation("Port_#0000.Hub_#0001").has_value());
    CHECK_FALSE(winusb::portFromLocationInformation("Port_#.Hub_#0001").has_value());
    CHECK_FALSE(winusb::portFromLocationInformation("Port_#0002x.Hub_#0001").has_value());
    // An interface node's, and a node the hub driver did not name.
    CHECK_FALSE(
        winusb::portFromLocationInformation("0000.0014.0000.003.000.000.000.000.000").has_value());
    CHECK_FALSE(winusb::portFromLocationInformation("USB Composite Device").has_value());
    CHECK_FALSE(winusb::portFromLocationInformation("").has_value());
}

TEST_CASE("usb topology: the location path names the port in its last USB step", "[winusb]") {
    CHECK(winusb::portFromLocationPath(
              "PCIROOT(B2)#PCI(0300)#PCI(0000)#USBROOT(0)#USB(1)#USB(2)#USBMI(3)") ==
          std::optional<std::uint32_t>(2));
    CHECK(winusb::portFromLocationPath("PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(7)") ==
          std::optional<std::uint32_t>(7));
}

TEST_CASE("usb topology: a location path with no USB step names no port", "[winusb]") {
    CHECK_FALSE(winusb::portFromLocationPath("PCIROOT(0)#PCI(1400)#USBROOT(0)").has_value());
    CHECK_FALSE(winusb::portFromLocationPath("ACPI(_SB_)#ACPI(PCI0)#ACPI(XHC_)").has_value());
    CHECK_FALSE(winusb::portFromLocationPath("PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(0)").has_value());
    CHECK_FALSE(winusb::portFromLocationPath("PCIROOT(0)#USBROOT(0)#USB(x)").has_value());
    CHECK_FALSE(winusb::portFromLocationPath("").has_value());
}

// ── An interface's class ────────────────────────────────────────────────────

TEST_CASE("usb topology: a compatible id with all three codes names the interface's class",
          "[winusb]") {
    CHECK(winusb::interfaceClassOf(R"(USB\Class_03&SubClass_00&Prot_00)") ==
          std::optional<winusb::InterfaceClass>({0x03, 0x00, 0x00}));
    // An Xbox 360 pad's vendor class, written the way the INF documentation spells it.
    CHECK(winusb::interfaceClassOf(R"(USB\CLASS_FF&SUBCLASS_5D&PROT_01)") ==
          std::optional<winusb::InterfaceClass>({0xFF, 0x5D, 0x01}));
}

TEST_CASE("usb topology: the shorter compatible ids name no class", "[winusb]") {
    CHECK_FALSE(winusb::interfaceClassOf(R"(USB\Class_03&SubClass_00)").has_value());
    CHECK_FALSE(winusb::interfaceClassOf(R"(USB\Class_03)").has_value());
    CHECK_FALSE(winusb::interfaceClassOf(R"(USB\COMPOSITE)").has_value());
    CHECK_FALSE(winusb::interfaceClassOf(R"(USB\Class_0G&SubClass_00&Prot_00)").has_value());
    CHECK_FALSE(winusb::interfaceClassOf(R"(USB\Class_03&SubClass_00&Prot_00&x)").has_value());
    CHECK_FALSE(winusb::interfaceClassOf("").has_value());
}

TEST_CASE("usb topology: a node's class is the first of its compatible ids that names one",
          "[winusb]") {
    const std::vector<std::string> ids{R"(USB\Class_03&SubClass_00&Prot_00)",
                                       R"(USB\Class_03&SubClass_00)", R"(USB\Class_03)"};
    CHECK(winusb::firstInterfaceClassOf(ids) ==
          std::optional<winusb::InterfaceClass>({0x03, 0x00, 0x00}));
    CHECK_FALSE(winusb::firstInterfaceClassOf({R"(USB\COMPOSITE)"}).has_value());
    CHECK_FALSE(winusb::firstInterfaceClassOf({}).has_value());
}

// ── The hub request ─────────────────────────────────────────────────────────

TEST_CASE("usb topology: the device descriptor request", "[winusb]") {
    const Bytes buffer = winusb::descriptorRequestBuffer(3, winusb::deviceDescriptorRequest());
    // Connection index 3, then GET_DESCRIPTOR(device) for 18 bytes.
    CHECK(headerOf(buffer) ==
          Bytes{0x03, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00});
    CHECK(buffer.size() == winusb::kDescriptorRequestHeaderBytes + 18);
    CHECK(allZeroAfterHeader(buffer));
}

TEST_CASE("usb topology: the configuration descriptor request, its head and then the whole",
          "[winusb]") {
    const Bytes head = winusb::descriptorRequestBuffer(
        2, winusb::configurationDescriptorRequest(winusb::kConfigurationHeaderBytes));
    CHECK(headerOf(head) ==
          Bytes{0x02, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x09, 0x00});
    const Bytes whole =
        winusb::descriptorRequestBuffer(2, winusb::configurationDescriptorRequest(0x00E3));
    CHECK(headerOf(whole) ==
          Bytes{0x02, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0xE3, 0x00});
    CHECK(whole.size() == winusb::kDescriptorRequestHeaderBytes + 0x00E3);
}

TEST_CASE("usb topology: the report descriptor request names the interface as its recipient",
          "[winusb]") {
    const Bytes buffer = winusb::descriptorRequestBuffer(
        0x0102, winusb::reportDescriptorRequest(3, winusb::kReportDescriptorRequestBytes));
    // bmRequest 0x81, GET_DESCRIPTOR, wValue 0x2200 (report, index 0), wIndex the interface,
    // wLength 4096.
    CHECK(headerOf(buffer) ==
          Bytes{0x02, 0x01, 0x00, 0x00, 0x81, 0x06, 0x00, 0x22, 0x03, 0x00, 0x00, 0x10});
    CHECK(buffer.size() == winusb::kDescriptorRequestHeaderBytes + 4096);
    CHECK(allZeroAfterHeader(buffer));
}

// ── The hub's answer ────────────────────────────────────────────────────────

TEST_CASE("usb topology: the descriptor is what the hub wrote after the header", "[winusb]") {
    const Bytes request = winusb::descriptorRequestBuffer(3, winusb::deviceDescriptorRequest());
    // A device descriptor in USB 2.0's Table 9-8 layout: Sony 054c:0ce6, bcdDevice 0x0100.
    const Bytes device{0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x4C,
                       0x05, 0xE6, 0x0C, 0x00, 0x01, 0x01, 0x02, 0x00, 0x01};
    const Bytes reply = replyOf(request, device);
    CHECK(winusb::descriptorOfReply(reply, reply.size()) == device);
    // A shorter answer is what the device sent, as GET_DESCRIPTOR allows.
    CHECK(winusb::descriptorOfReply(reply, winusb::kDescriptorRequestHeaderBytes + 8) ==
          Bytes(device.begin(), device.begin() + 8));
}

TEST_CASE("usb topology: an answer no longer than the header carries no descriptor", "[winusb]") {
    const Bytes request = winusb::descriptorRequestBuffer(3, winusb::deviceDescriptorRequest());
    CHECK(winusb::descriptorOfReply(request, winusb::kDescriptorRequestHeaderBytes).empty());
    CHECK(winusb::descriptorOfReply(request, 0).empty());
    // A count past the buffer reads no further than the buffer.
    CHECK(winusb::descriptorOfReply(request, request.size() + 100).size() == 18);
}

TEST_CASE("usb topology: a configuration descriptor's head gives the length to ask for",
          "[winusb]") {
    // USB 2.0 Table 9-10: bLength 9, type 2, wTotalLength 0x00E3, four interfaces.
    const Bytes head{0x09, 0x02, 0xE3, 0x00, 0x04, 0x01, 0x00, 0xC0, 0xFA};
    CHECK(winusb::configurationTotalLength(head) == std::optional<std::uint16_t>(0x00E3));
}

TEST_CASE("usb topology: a head that is not a configuration descriptor gives no length",
          "[winusb]") {
    CHECK_FALSE(winusb::configurationTotalLength(
                    Bytes{0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x4C})
                    .has_value());
    CHECK_FALSE(winusb::configurationTotalLength(Bytes{0x09, 0x02, 0xE3}).has_value());
    // A total shorter than the configuration descriptor itself, which USBView refuses too.
    CHECK_FALSE(winusb::configurationTotalLength(
                    Bytes{0x09, 0x02, 0x05, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32})
                    .has_value());
}
