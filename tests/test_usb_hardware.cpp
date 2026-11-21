// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A pad's hardware from its own descriptors, the capture of its reports at rest, and the
// measured usbHost (docs/diagnostics-bundle.md): the pure core on real bytes, shared with
// dish-linux, and what Windows hands it, the hub's reading of the same pad.

#include "core/diagnostics/Json.h"
#include "core/diagnostics/ReportCapture.h"
#include "core/diagnostics/UsbDescriptors.h"
#include "core/diagnostics/UsbHost.h"
#include "source/usb/UsbDeviceGateway.h"
#include "source/usb/UsbHardwareReading.h"

#include "UsbDescriptorFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace dish::diagnostics;
using dish::test::kDualSenseConfiguration;
using dish::test::kDualSenseDevice;
using dish::test::kDualSenseRawDescriptorsSha256;
using dish::test::kDualSenseReportDescriptor;
using dish::test::kDualSenseReportDescriptorSha256;
using dish::test::kRootHub2Descriptors;
using dish::test::kRootHub2DescriptorsSha256;
using dish::test::kRootHub3Descriptors;

namespace {

template <std::size_t N> DescriptorBytes bytesOf(const std::array<std::uint8_t, N>& a) {
    return DescriptorBytes(a.begin(), a.end());
}

UsbEndpoint endpointOf(std::uint8_t address, EndpointType type, std::uint16_t maxPacket,
                       std::uint8_t interval) {
    return UsbEndpoint{address, type, maxPacket, interval};
}

UsbInterface interfaceOf(std::uint8_t number, std::uint8_t alternate, std::uint8_t cls,
                         std::uint8_t subclass, std::vector<UsbEndpoint> endpoints = {}) {
    return UsbInterface{number, alternate, cls, subclass, 0x00, std::move(endpoints)};
}

UsbHardwareFacts dualSenseFacts() {
    UsbHardwareFacts facts;
    facts.bcdDevice = 0x0100;
    facts.raw = RawDescriptors{bytesOf(kDualSenseDevice), bytesOf(kDualSenseConfiguration)};
    facts.interfaces = interfacesOf(facts.raw.configuration);
    const auto device = parseDeviceDescriptor(facts.raw.device);
    REQUIRE(device.has_value());
    facts.strings =
        captureStrings(*device, "Sony Interactive Entertainment", "Wireless Controller");
    facts.reportDescriptor =
        HidReportDescriptor{3, bytesOf(kDualSenseReportDescriptor), DescriptorSource::Device};
    return facts;
}

// The DualSense as its hub answers for it: the device tree's four interfaces, the device and
// configuration descriptors, and the HID interface's report descriptor.
dish::source::usb::UsbHardwareReading dualSenseReading() {
    using dish::source::usb::HubDescriptor;
    using dish::source::usb::UsbReportDescriptor;
    dish::source::usb::UsbHardwareReading reading;
    reading.interfaces = {
        {0, 0x01, 0x01, 0x00}, {1, 0x01, 0x02, 0x00}, {2, 0x01, 0x02, 0x00}, {3, 0x03, 0x00, 0x00}};
    reading.device.bytes = bytesOf(kDualSenseDevice);
    reading.configuration.bytes = bytesOf(kDualSenseConfiguration);
    reading.reportDescriptors = {
        UsbReportDescriptor{3, HubDescriptor{bytesOf(kDualSenseReportDescriptor), std::nullopt}}};
    return reading;
}

// The interface the DualSense is claimed on.
constexpr int kDualSenseHidInterface = 3;
// ERROR_GEN_FAILURE, what a hub answers when the pad stalls the request.
constexpr std::uint32_t kGenFailure = 31;
// What HidD_GetManufacturerString and HidD_GetProductString read off the DualSense.
const CaptureStrings kDualSenseStrings{"Sony Interactive Entertainment", "Wireless Controller"};

} // namespace

TEST_CASE("usb descriptors: the device descriptor names the release and the strings' indices",
          "[diagnostics][usb]") {
    const auto ds5 = parseDeviceDescriptor(bytesOf(kDualSenseDevice));
    REQUIRE(ds5.has_value());
    CHECK(ds5->bcdUsb == 0x0200);
    CHECK(ds5->vendorId == 0x054C);
    CHECK(ds5->productId == 0x0CE6);
    CHECK(ds5->bcdDevice == 0x0100);
    CHECK(ds5->manufacturerIndex == 1);
    CHECK(ds5->productIndex == 2);
    CHECK(ds5->serialNumberIndex == 0);
    CHECK(ds5->configurations == 1);

    const RawDescriptors hub =
        splitDescriptors(kRootHub2Descriptors.data(), kRootHub2Descriptors.size());
    const auto hubDevice = parseDeviceDescriptor(hub.device);
    REQUIRE(hubDevice.has_value());
    CHECK(hubDevice->bcdDevice == 0x0700);
    CHECK(hubDevice->serialNumberIndex == 1);

    SECTION("anything shorter or of another type is no device descriptor") {
        CHECK_FALSE(parseDeviceDescriptor(DescriptorBytes{0x12, 0x01}).has_value());
        DescriptorBytes config = bytesOf(kDualSenseDevice);
        config[1] = 0x02;
        CHECK_FALSE(parseDeviceDescriptor(config).has_value());
    }
}

TEST_CASE("usb descriptors: the file splits after the 18-byte device descriptor",
          "[diagnostics][usb]") {
    const RawDescriptors hub =
        splitDescriptors(kRootHub2Descriptors.data(), kRootHub2Descriptors.size());
    CHECK(hub.device.size() == 18);
    CHECK(hub.configuration.size() == 25);
    CHECK(hub.configuration[1] == 0x02);
    CHECK(splitDescriptors(nullptr, 0) == RawDescriptors{});
    // A file cut short keeps what is there as the device descriptor.
    const RawDescriptors cut = splitDescriptors(kRootHub2Descriptors.data(), 10);
    CHECK(cut.device.size() == 10);
    CHECK(cut.configuration.empty());

    SECTION("a bLength that lies does not move the split") {
        // sysfs writes sizeof(struct usb_device_descriptor), 18 bytes, before the configurations
        // whatever the pad put in bLength (drivers/usb/core/sysfs.c, descriptors_read).
        std::array<std::uint8_t, kRootHub2Descriptors.size()> lying = kRootHub2Descriptors;
        lying[0] = 0x09;
        const RawDescriptors split = splitDescriptors(lying.data(), lying.size());
        CHECK(split.device.size() == 18);
        CHECK(split.configuration.size() == 25);
        CHECK(split.configuration[0] == 0x09);
        CHECK(split.configuration[1] == 0x02);
    }
}

TEST_CASE("usb descriptors: every interface and alternate setting with the endpoints after it",
          "[diagnostics][usb]") {
    const std::vector<UsbInterface> interfaces = interfacesOf(bytesOf(kDualSenseConfiguration));
    const std::vector<UsbInterface> expected{
        interfaceOf(0, 0, 0x01, 0x01),
        interfaceOf(1, 0, 0x01, 0x02),
        interfaceOf(1, 1, 0x01, 0x02, {endpointOf(0x01, EndpointType::Isochronous, 392, 4)}),
        interfaceOf(2, 0, 0x01, 0x02),
        interfaceOf(2, 1, 0x01, 0x02, {endpointOf(0x82, EndpointType::Isochronous, 196, 4)}),
        interfaceOf(3, 0, 0x03, 0x00,
                    {endpointOf(0x84, EndpointType::Interrupt, 64, 6),
                     endpointOf(0x03, EndpointType::Interrupt, 64, 6)}),
    };
    CHECK(interfaces == expected);
    CHECK(interfaces.back().endpoints.front().isIn());
    CHECK_FALSE(interfaces.back().endpoints.back().isIn());

    SECTION("a SuperSpeed companion descriptor is stepped over") {
        const RawDescriptors hub =
            splitDescriptors(kRootHub3Descriptors.data(), kRootHub3Descriptors.size());
        const std::vector<UsbInterface> hubInterfaces = interfacesOf(hub.configuration);
        REQUIRE(hubInterfaces.size() == 1);
        CHECK(hubInterfaces[0].interfaceClass == 0x09);
        CHECK(hubInterfaces[0].endpoints ==
              std::vector<UsbEndpoint>{endpointOf(0x81, EndpointType::Interrupt, 2, 12)});
    }
}

TEST_CASE("usb descriptors: a descriptor that lies about its length ends the walk",
          "[diagnostics][usb]") {
    // interface 0, then a zero-length descriptor that would never advance.
    const DescriptorBytes zeroLength{0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00,
                                     0x00, 0x00, 0x05, 0x81, 0x03, 0x40, 0x00, 0x01};
    CHECK(interfacesOf(zeroLength).size() == 1);
    CHECK(interfacesOf(zeroLength).front().endpoints.empty());
    // an endpoint that claims more bytes than are left.
    const DescriptorBytes overrun{0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00,
                                  0x00, 0x00, 0x09, 0x05, 0x81, 0x03};
    CHECK(interfacesOf(overrun).front().endpoints.empty());
    // an endpoint before any interface, and an interface descriptor too short to be one.
    const DescriptorBytes orphan{0x07, 0x05, 0x81, 0x03, 0x40, 0x00,
                                 0x01, 0x05, 0x04, 0x00, 0x00, 0x01};
    CHECK(interfacesOf(orphan).empty());
    CHECK(interfacesOf(DescriptorBytes{}).empty());
}

TEST_CASE("usb descriptors: a pad is named by the SHA-256 of its raw USB descriptors",
          "[diagnostics][usb]") {
    CHECK(sha256Hex(bytesOf(kDualSenseReportDescriptor)) == kDualSenseReportDescriptorSha256);
    CHECK(sha256Hex(DescriptorBytes{}).empty());
    // The device descriptor, then every configuration in index order, as the bus delivered them.
    CHECK(descriptorSha256Json(dualSenseFacts()).stringValue() == kDualSenseRawDescriptorsSha256);
    UsbHardwareFacts hub;
    hub.raw = splitDescriptors(kRootHub2Descriptors.data(), kRootHub2Descriptors.size());
    CHECK(rawDescriptorsSha256(hub) == std::optional<std::string>(kRootHub2DescriptorsSha256));

    SECTION("a pad with no raw descriptors has none, whatever report descriptor it has") {
        UsbHardwareFacts reportOnly;
        reportOnly.reportDescriptor =
            HidReportDescriptor{0, bytesOf(kDualSenseReportDescriptor), DescriptorSource::Os};
        CHECK_FALSE(rawDescriptorsSha256(reportOnly).has_value());
        CHECK(descriptorSha256Json(reportOnly).kind() == Json::Kind::Null);
        CHECK(descriptorSha256Json(UsbHardwareFacts{}).kind() == Json::Kind::Null);
    }
}

TEST_CASE("usb descriptors: release numbers and bytes read as the bundle writes them",
          "[diagnostics][usb]") {
    CHECK(bcdText(0x0107) == "0107");
    CHECK(bcdText(0x0100) == "0100");
    // Four lowercase hex digits, the descriptor's own value, never a release string like "1.07".
    CHECK(bcdText(0x01AB) == "01ab");
    CHECK(hexOf(DescriptorBytes{0x12, 0x01, 0xAB}) == "1201ab");
    CHECK(endpointTypeToken(EndpointType::Interrupt) == "interrupt");
    CHECK(endpointTypeToken(EndpointType::Isochronous) == "isochronous");
    CHECK(endpointTypeToken(EndpointType::Bulk) == "bulk");
    CHECK(endpointTypeToken(EndpointType::Control) == "control");
    // Where a report descriptor came from: off the pad itself, the system's copy, or rebuilt.
    CHECK(descriptorSourceToken(DescriptorSource::Device) == "device");
    CHECK(descriptorSourceToken(DescriptorSource::Os) == "os");
    CHECK(descriptorSourceToken(DescriptorSource::Reconstructed) == "reconstructed");
}

namespace {

// The capture's `strings` object for a pad with these indices and these texts.
Json stringsFor(const UsbDeviceDescriptor& device, const std::string& manufacturer,
                const std::string& product) {
    UsbHardwareFacts facts;
    facts.strings = captureStrings(device, manufacturer, product);
    return *captureJson(facts, {}).find("strings");
}

} // namespace

TEST_CASE("usb descriptors: a capture's strings are the maker's and the product's by role, never "
          "the serial number",
          "[diagnostics][usb]") {
    UsbDeviceDescriptor device;
    device.manufacturerIndex = 1;
    device.productIndex = 2;
    device.serialNumberIndex = 3;
    const Json both = stringsFor(device, "Sony Interactive Entertainment", "Wireless Controller");
    REQUIRE(both.find("manufacturer") != nullptr);
    REQUIRE(both.find("product") != nullptr);
    CHECK(both.find("manufacturer")->stringValue() == "Sony Interactive Entertainment");
    CHECK(both.find("product")->stringValue() == "Wireless Controller");
    CHECK(both.keys().size() == 2);
    SECTION("a string that shares the serial number's index is the serial number") {
        device.productIndex = 3;
        const Json strings = stringsFor(device, "Maker", "Unit 0042");
        REQUIRE(strings.find("product") != nullptr);
        CHECK(strings.find("manufacturer")->stringValue() == "Maker");
        CHECK(strings.find("product")->kind() == Json::Kind::Null);
    }
    SECTION("index 0 names no string, and an empty one is not reported") {
        device.manufacturerIndex = 0;
        const Json strings = stringsFor(device, "Maker", "");
        REQUIRE(strings.find("manufacturer") != nullptr);
        REQUIRE(strings.find("product") != nullptr);
        CHECK(strings.find("manufacturer")->kind() == Json::Kind::Null);
        CHECK(strings.find("product")->kind() == Json::Kind::Null);
    }
}

TEST_CASE("usb descriptors: each interface is written with its endpoints", "[diagnostics][usb]") {
    const Json interfaces = interfacesJson(dualSenseFacts().interfaces);
    REQUIRE(interfaces.items().size() == 6);
    const Json& hid = interfaces.items().back();
    CHECK(hid.find("number")->integerValue() == 3);
    CHECK(hid.find("alternate")->integerValue() == 0);
    CHECK(hid.find("class")->stringValue() == "03");
    CHECK(hid.find("subclass")->stringValue() == "00");
    CHECK(hid.find("protocol")->stringValue() == "00");
    const Json& in = hid.find("endpoints")->items().front();
    CHECK(in.find("address")->stringValue() == "84");
    CHECK(in.find("type")->stringValue() == "interrupt");
    CHECK(in.find("maxPacketSize")->integerValue() == 64);
    CHECK(in.find("interval")->integerValue() == 6);
}

TEST_CASE("capture: twenty reports at rest, then the capture disarms itself",
          "[diagnostics][capture]") {
    ReadLoopWord word;
    word.start();
    ReportCapture capture;
    CHECK_FALSE(ReadLoopWord::capturing(word.load()));
    REQUIRE(capture.arm(word));
    CHECK(ReadLoopWord::capturing(word.load()));
    CHECK(ReadLoopWord::running(word.load()));
    CHECK_FALSE(capture.arm(word));
    for (std::uint8_t i = 0; i < 25; ++i) {
        const std::array<std::uint8_t, 3> report{0x01, i, 0x80};
        if (ReadLoopWord::capturing(word.load())) {
            capture.offer(word, report.data(), report.size(), 1000 + i);
        }
    }
    const std::vector<CapturedReport> kept = capture.kept();
    REQUIRE(kept.size() == kCaptureReports);
    CHECK(kept.front().bytes[1] == 0);
    CHECK(kept.back().bytes[1] == 19);
    CHECK(kept.back().atMs == 1019);
    CHECK(kept.back().length == 3);
    CHECK_FALSE(ReadLoopWord::capturing(word.load()));
    CHECK(ReadLoopWord::running(word.load()));

    SECTION("a capture armed again starts over at the first slot") {
        REQUIRE(capture.arm(word));
        CHECK(capture.kept().empty());
        const std::array<std::uint8_t, 1> report{0x42};
        capture.offer(word, report.data(), report.size(), 5000);
        REQUIRE(capture.kept().size() == 1);
        CHECK(capture.kept().front().bytes[0] == 0x42);
    }
}

TEST_CASE("capture: a report longer than a slot keeps its first bytes", "[diagnostics][capture]") {
    ReadLoopWord word;
    ReportCapture capture;
    REQUIRE(capture.arm(word));
    std::array<std::uint8_t, 100> report{};
    report[63] = 0x63;
    report[64] = 0x64;
    capture.offer(word, report.data(), report.size(), 7);
    REQUIRE(capture.kept().size() == 1);
    CHECK(capture.kept().front().length == kCaptureReportMaxBytes);
    CHECK(capture.kept().front().bytes[63] == 0x63);
}

TEST_CASE("capture: the loop's word keeps running and capturing apart", "[diagnostics][capture]") {
    ReadLoopWord word;
    CHECK_FALSE(ReadLoopWord::running(word.load()));
    word.start();
    REQUIRE(word.armCapture());
    word.stop();
    CHECK_FALSE(ReadLoopWord::running(word.load()));
    CHECK(ReadLoopWord::capturing(word.load()));
    word.disarmCapture();
    CHECK_FALSE(ReadLoopWord::capturing(word.load()));
    SECTION("an unarmed capture keeps nothing it is offered") {
        ReportCapture capture;
        const std::array<std::uint8_t, 1> report{0x01};
        capture.offer(word, report.data(), report.size(), 1);
        CHECK(capture.kept().empty());
    }
}

TEST_CASE("capture: done at twenty reports, or when its time is up, failed without the pad",
          "[diagnostics][capture]") {
    CHECK(captureProgress(kCaptureReports, 3, 20, true) == CaptureProgress::Captured);
    CHECK(captureProgress(5, 3, 20, true) == CaptureProgress::Capturing);
    // A pad that reports only on change sends nothing at rest: its descriptors still ride.
    CHECK(captureProgress(0, 20, 20, true) == CaptureProgress::Captured);
    CHECK(captureProgress(kCaptureReports, 3, 20, false) == CaptureProgress::Failed);
}

TEST_CASE("capture: arrival times move onto the wall clock by their age",
          "[diagnostics][capture]") {
    std::vector<CapturedReport> kept(2);
    kept[0].atMs = 100;
    kept[0].length = 2;
    kept[0].bytes[0] = 0x01;
    kept[0].bytes[1] = 0x7F;
    kept[1].atMs = 104;
    kept[1].length = 1;
    const std::vector<RestReport> reports = restReportsOf(kept, 110, 1763586000000);
    REQUIRE(reports.size() == 2);
    CHECK(reports[0] == RestReport{1763585999990, DescriptorBytes{0x01, 0x7F}});
    CHECK(reports[1].atMs == 1763585999994);
    CHECK(reports[1].bytes.size() == 1);
}

TEST_CASE("capture: the file's usb-descriptors-v1 object", "[diagnostics][capture]") {
    const std::vector<RestReport> reports{RestReport{1763585999990, DescriptorBytes{0x01, 0x80}}};
    const Json capture = captureJson(dualSenseFacts(), reports);
    CHECK(capture.find("format")->stringValue() == "usb-descriptors-v1");
    CHECK(capture.find("device")->stringValue() == "12010002000000404c05e60c0001010200"
                                                   "01");
    CHECK(capture.find("configuration")->stringValue().size() ==
          2 * kDualSenseConfiguration.size());
    const Json& strings = *capture.find("strings");
    REQUIRE(strings.find("manufacturer") != nullptr);
    REQUIRE(strings.find("product") != nullptr);
    CHECK(strings.find("manufacturer")->stringValue() == "Sony Interactive Entertainment");
    CHECK(strings.find("product")->stringValue() == "Wireless Controller");
    const Json& descriptor = capture.find("hidReportDescriptors")->items().front();
    CHECK(descriptor.find("interface")->integerValue() == 3);
    CHECK(descriptor.find("source")->stringValue() == "device");
    CHECK(descriptor.find("hex")->stringValue().rfind("05010905a101", 0) == 0);
    const Json& rest = capture.find("restReports")->items().front();
    CHECK(rest.find("atMs")->integerValue() == 1763585999990);
    CHECK(rest.find("hex")->stringValue() == "0180");

    SECTION("what the platform could not read is null, or empty") {
        const Json none = captureJson(UsbHardwareFacts{}, {});
        CHECK(none.find("device")->kind() == Json::Kind::Null);
        CHECK(none.find("configuration")->kind() == Json::Kind::Null);
        CHECK(none.find("hidReportDescriptors")->items().empty());
        const Json& noStrings = *none.find("strings");
        REQUIRE(noStrings.find("manufacturer") != nullptr);
        CHECK(noStrings.find("manufacturer")->kind() == Json::Kind::Null);
        CHECK(noStrings.find("product")->kind() == Json::Kind::Null);
        CHECK(none.find("restReports")->items().empty());
    }
}

TEST_CASE("usb host: a host controller is true, none is false, no answer is null",
          "[diagnostics][usb]") {
    CHECK(usbHostOf(2) == std::optional<bool>(true));
    CHECK(usbHostOf(1) == std::optional<bool>(true));
    CHECK(usbHostOf(0) == std::optional<bool>(false));
    CHECK_FALSE(usbHostOf(std::nullopt).has_value());
}

// ── What Windows hands the shared parsers ───────────────────────────────────

TEST_CASE("windows hardware: the hub's descriptors become the pad's facts, as on every platform",
          "[diagnostics][usb][windows]") {
    // The HID class driver's release number differs on purpose: the device descriptor's wins.
    const UsbHardwareFacts facts =
        dish::source::usb::hardwareFactsOf(dualSenseReading(), kDualSenseHidInterface, 0x0200,
                                           "Sony Interactive Entertainment", "Wireless Controller");
    CHECK(facts.raw.device == bytesOf(kDualSenseDevice));
    CHECK(facts.raw.configuration == bytesOf(kDualSenseConfiguration));
    CHECK(facts.interfaces == interfacesOf(bytesOf(kDualSenseConfiguration)));
    CHECK(facts.bcdDevice == std::optional<std::uint16_t>(0x0100));
    CHECK(facts.strings == kDualSenseStrings);
    REQUIRE(facts.reportDescriptor.has_value());
    CHECK(facts.reportDescriptor->interfaceNumber == kDualSenseHidInterface);
    CHECK(facts.reportDescriptor->bytes == bytesOf(kDualSenseReportDescriptor));
    CHECK(facts.reportDescriptor->source == DescriptorSource::Device);
    // The same bytes, so the same name for the pad as dish-android and dish-linux give it.
    CHECK(rawDescriptorsSha256(facts) ==
          std::optional<std::string>(kDualSenseRawDescriptorsSha256));
}

TEST_CASE("windows hardware: a refused report descriptor leaves none, and the pad keeps its name",
          "[diagnostics][usb][windows]") {
    dish::source::usb::UsbHardwareReading reading = dualSenseReading();
    reading.reportDescriptors.front().descriptor = {{}, kGenFailure};
    const UsbHardwareFacts facts =
        dish::source::usb::hardwareFactsOf(reading, kDualSenseHidInterface, 0x0100,
                                           "Sony Interactive Entertainment", "Wireless Controller");
    CHECK_FALSE(facts.reportDescriptor.has_value());
    CHECK(rawDescriptorsSha256(facts) ==
          std::optional<std::string>(kDualSenseRawDescriptorsSha256));
    CHECK(captureJson(facts, {}).find("hidReportDescriptors")->items().empty());

    SECTION("an answer for another interface is not the claimed one's") {
        dish::source::usb::UsbHardwareReading other = dualSenseReading();
        other.reportDescriptors.front().interfaceNumber = 0;
        CHECK_FALSE(
            dish::source::usb::hardwareFactsOf(other, kDualSenseHidInterface, 0x0100, "", "")
                .reportDescriptor.has_value());
    }
}

TEST_CASE("windows hardware: a hub that gave nothing leaves the device tree's classes, and the "
          "HID class driver's release and strings",
          "[diagnostics][usb][windows]") {
    dish::source::usb::UsbHardwareReading reading;
    reading.interfaces = {{3, 0x03, 0x00, 0x00}};
    reading.hubUnreachable = 5;
    const UsbHardwareFacts facts =
        dish::source::usb::hardwareFactsOf(reading, kDualSenseHidInterface, 0x0107,
                                           "Sony Interactive Entertainment", "Wireless Controller");
    CHECK(facts.raw == RawDescriptors{});
    REQUIRE(facts.interfaces.size() == 1);
    CHECK(facts.interfaces.front() == interfaceOf(3, 0, 0x03, 0x00));
    CHECK(facts.bcdDevice == std::optional<std::uint16_t>(0x0107));
    CHECK(facts.strings == kDualSenseStrings);
    CHECK_FALSE(facts.reportDescriptor.has_value());
    CHECK_FALSE(rawDescriptorsSha256(facts).has_value());

    SECTION("a string the HID class driver read empty is a role the pad does not report") {
        const UsbHardwareFacts bare = dish::source::usb::hardwareFactsOf(
            reading, kDualSenseHidInterface, 0x0107, "", "Wireless Controller");
        CHECK(bare.strings == CaptureStrings{std::nullopt, "Wireless Controller"});
    }
}

TEST_CASE("windows hardware: descriptors read in part are none, so the checksum is never wrong",
          "[diagnostics][usb][windows]") {
    dish::source::usb::UsbHardwareReading reading = dualSenseReading();
    reading.configuration = {{}, kGenFailure};
    const UsbHardwareFacts facts =
        dish::source::usb::hardwareFactsOf(reading, kDualSenseHidInterface, 0x0100,
                                           "Sony Interactive Entertainment", "Wireless Controller");
    CHECK(facts.raw == RawDescriptors{});
    CHECK_FALSE(rawDescriptorsSha256(facts).has_value());
    // The device tree still names the interfaces, without their endpoints.
    CHECK(facts.interfaces.size() == 4);
    CHECK(facts.interfaces.back().endpoints.empty());
    CHECK(facts.strings == kDualSenseStrings);
}

TEST_CASE("windows hardware: the claimed interface's interrupt endpoints, from the configuration",
          "[diagnostics][usb][windows]") {
    const UsbHardwareFacts facts =
        dish::source::usb::hardwareFactsOf(dualSenseReading(), kDualSenseHidInterface, 0, "", "");
    CHECK(dish::source::usb::claimedEndpointsOf(facts, kDualSenseHidInterface) ==
          dish::source::usb::ClaimedEndpoints{true, 0x84, 0x03});
    // The audio interfaces carry isochronous endpoints only.
    CHECK(dish::source::usb::claimedEndpointsOf(facts, 1) ==
          dish::source::usb::ClaimedEndpoints{true, -1, -1});
    CHECK(dish::source::usb::claimedEndpointsOf(facts, 7) == dish::source::usb::ClaimedEndpoints{});
    CHECK(dish::source::usb::claimedEndpointsOf(UsbHardwareFacts{}, kDualSenseHidInterface) ==
          dish::source::usb::ClaimedEndpoints{});
}

TEST_CASE("windows hardware: the expect check reads the endpoints once the hub named them",
          "[diagnostics][usb][windows]") {
    dish::source::usb::UsbDeviceInfo pad;
    pad.inputReportBytes = 64;
    pad.outputReportBytes = 0;
    // Until the hub answers, the HID report lengths stand for the pipes.
    CHECK(pad.observedEndpoints() == dish::controllerdb::ObservedHardware{true, false});
    pad.endpointsRead = true;
    pad.endpointInAddress = 0x84;
    pad.endpointOutAddress = 0x03;
    CHECK(pad.observedEndpoints() == dish::controllerdb::ObservedHardware{true, true});
    pad.endpointOutAddress = -1;
    CHECK(pad.observedEndpoints() == dish::controllerdb::ObservedHardware{true, false});
}
