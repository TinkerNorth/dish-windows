// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/UsbHardwareReading.h"

#include <algorithm>

namespace dish::source::usb {

namespace {

// The default alternate setting is the one a claimed HID interface runs.
constexpr std::uint8_t kDefaultAlternate = 0;

std::vector<diagnostics::UsbInterface> treeInterfaces(const std::vector<UsbInterfaceEntry>& tree) {
    std::vector<diagnostics::UsbInterface> out;
    out.reserve(tree.size());
    for (const UsbInterfaceEntry& entry : tree) {
        diagnostics::UsbInterface intf;
        intf.number = static_cast<std::uint8_t>(entry.number);
        intf.interfaceClass = entry.classCode;
        intf.subclass = entry.subclass;
        intf.protocol = entry.protocol;
        out.push_back(intf);
    }
    return out;
}

std::optional<diagnostics::HidReportDescriptor>
reportDescriptorOf(const std::vector<UsbReportDescriptor>& read, int claimedInterface) {
    for (const UsbReportDescriptor& entry : read) {
        const bool answered = entry.interfaceNumber == claimedInterface &&
                              !entry.descriptor.refusal.has_value() &&
                              !entry.descriptor.bytes.empty();
        if (answered) {
            return diagnostics::HidReportDescriptor{claimedInterface, entry.descriptor.bytes,
                                                    diagnostics::DescriptorSource::Device};
        }
    }
    return std::nullopt;
}

// The raw descriptors only when the hub gave the device descriptor and every configuration: a part
// of them would hash to a name no other platform gives the pad.
diagnostics::RawDescriptors rawDescriptorsOf(const UsbHardwareReading& reading) {
    const bool deviceRead = !reading.device.refusal.has_value() && !reading.device.bytes.empty();
    const bool configurationRead =
        !reading.configuration.refusal.has_value() && !reading.configuration.bytes.empty();
    if (!deviceRead || !configurationRead) { return {}; }
    return diagnostics::RawDescriptors{reading.device.bytes, reading.configuration.bytes};
}

// The HID class driver's strings alone, where no device descriptor names their roles' indices.
std::optional<std::string> nonEmpty(const std::string& text) {
    if (text.empty()) { return std::nullopt; }
    return text;
}

bool isInterruptIn(const diagnostics::UsbEndpoint& endpoint) {
    return endpoint.type == diagnostics::EndpointType::Interrupt && endpoint.isIn();
}

bool isInterruptOut(const diagnostics::UsbEndpoint& endpoint) {
    return endpoint.type == diagnostics::EndpointType::Interrupt && !endpoint.isIn();
}

} // namespace

diagnostics::UsbHardwareFacts hardwareFactsOf(const UsbHardwareReading& reading,
                                              int claimedInterface, int hidVersionNumber,
                                              const std::string& manufacturer,
                                              const std::string& product) {
    diagnostics::UsbHardwareFacts facts;
    facts.raw = rawDescriptorsOf(reading);
    facts.interfaces = facts.raw.configuration.empty()
                           ? treeInterfaces(reading.interfaces)
                           : diagnostics::interfacesOf(facts.raw.configuration);
    const std::optional<diagnostics::UsbDeviceDescriptor> device =
        diagnostics::parseDeviceDescriptor(facts.raw.device);
    if (device.has_value()) {
        facts.bcdDevice = device->bcdDevice;
        facts.strings = diagnostics::captureStrings(*device, manufacturer, product);
    } else {
        facts.bcdDevice = static_cast<std::uint16_t>(hidVersionNumber);
        facts.strings = diagnostics::CaptureStrings{nonEmpty(manufacturer), nonEmpty(product)};
    }
    facts.reportDescriptor = reportDescriptorOf(reading.reportDescriptors, claimedInterface);
    return facts;
}

ClaimedEndpoints claimedEndpointsOf(const diagnostics::UsbHardwareFacts& facts,
                                    int claimedInterface) {
    if (facts.raw.configuration.empty()) { return {}; }
    const auto claimed = std::find_if(facts.interfaces.begin(), facts.interfaces.end(),
                                      [claimedInterface](const diagnostics::UsbInterface& intf) {
                                          return intf.number == claimedInterface &&
                                                 intf.alternate == kDefaultAlternate;
                                      });
    if (claimed == facts.interfaces.end()) { return {}; }
    ClaimedEndpoints out;
    out.read = true;
    const auto in =
        std::find_if(claimed->endpoints.begin(), claimed->endpoints.end(), isInterruptIn);
    const auto outEndpoint =
        std::find_if(claimed->endpoints.begin(), claimed->endpoints.end(), isInterruptOut);
    if (in != claimed->endpoints.end()) { out.inAddress = in->address; }
    if (outEndpoint != claimed->endpoints.end()) { out.outAddress = outEndpoint->address; }
    return out;
}

} // namespace dish::source::usb
