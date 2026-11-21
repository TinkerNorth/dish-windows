// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/usb/WinUsbHardware.h"

#include "core/diagnostics/UsbDescriptors.h"
#include "core/input/HidTransport.h"
#include "core/input/WinUsbTopology.h"

#include <QString>

// <windows.h> first, then the device tree and winioctl.h's CTL_CODE, which usbioctl.h's request
// codes are built from; initguid.h before usbiodef.h so GUID_DEVINTERFACE_USB_HUB is defined in
// this unit, as Microsoft's page for it requires.
#include <windows.h>

#include <cfgmgr32.h>
#include <winioctl.h>

#include <initguid.h>

#include <usbiodef.h>

#include <usbioctl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dish::source::usb {

namespace {

namespace winusb = input::winusb;

static_assert(sizeof(USB_DESCRIPTOR_REQUEST) == winusb::kDescriptorRequestHeaderBytes,
              "the request WinUsbTopology builds is the SDK's USB_DESCRIPTOR_REQUEST");

// bInterfaceClass of a HID interface (HID 1.11 §4.1).
constexpr std::uint8_t kHidClass = 0x03;

std::wstring wideIdOf(DEVINST node) {
    std::array<wchar_t, MAX_DEVICE_ID_LEN + 1> id{};
    if (CM_Get_Device_IDW(node, id.data(), static_cast<ULONG>(id.size()), 0) != CR_SUCCESS) {
        return {};
    }
    return {id.data()};
}

std::optional<DEVINST> nodeOf(const std::string& instanceId) {
    std::wstring id = QString::fromStdString(instanceId).toStdWString();
    DEVINST node = 0;
    if (CM_Locate_DevNodeW(&node, id.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return std::nullopt;
    }
    return node;
}

// A REG_SZ or REG_MULTI_SZ property of a node as its strings, empty when it has none.
std::vector<std::string> nodeStrings(DEVINST node, ULONG property) {
    ULONG bytes = 0;
    const bool sized = CM_Get_DevNode_Registry_PropertyW(node, property, nullptr, nullptr, &bytes,
                                                         0) == CR_BUFFER_SMALL &&
                       bytes > 0;
    if (!sized) { return {}; }
    // One wide character more than the property needs, so its last string always ends.
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1, L'\0');
    if (CM_Get_DevNode_Registry_PropertyW(node, property, nullptr, text.data(), &bytes, 0) !=
        CR_SUCCESS) {
        return {};
    }
    std::vector<std::string> strings;
    std::size_t at = 0;
    while (at < text.size() && text[at] != L'\0') {
        const std::wstring one(text.data() + at);
        strings.push_back(QString::fromStdWString(one).toStdString());
        at += one.size() + 1;
    }
    return strings;
}

std::optional<UsbInterfaceEntry> interfaceEntryOf(DEVINST node, std::optional<int> number) {
    if (!number.has_value()) { return std::nullopt; }
    const std::optional<winusb::InterfaceClass> named =
        winusb::firstInterfaceClassOf(nodeStrings(node, CM_DRP_COMPATIBLEIDS));
    if (!named.has_value()) { return std::nullopt; }
    return UsbInterfaceEntry{*number, named->classCode, named->subclass, named->protocol};
}

// A composite device's children are its interfaces, each named by its instance id
// ("USB\VID_054C&PID_0CE6&MI_03\..."); a device with no such child is its own interface 0, the
// only number a one-interface configuration has.
std::vector<UsbInterfaceEntry> interfacesOf(DEVINST device) {
    std::vector<UsbInterfaceEntry> interfaces;
    DEVINST child = 0;
    bool more = CM_Get_Child(&child, device, 0) == CR_SUCCESS;
    while (more) {
        const std::string id = QString::fromStdWString(wideIdOf(child)).toStdString();
        const std::optional<UsbInterfaceEntry> entry =
            interfaceEntryOf(child, input::hidInterfaceNumberFromPath(id));
        if (entry.has_value()) { interfaces.push_back(*entry); }
        more = CM_Get_Sibling(&child, child, 0) == CR_SUCCESS;
    }
    if (!interfaces.empty()) { return interfaces; }
    const std::optional<UsbInterfaceEntry> sole = interfaceEntryOf(device, 0);
    if (sole.has_value()) { interfaces.push_back(*sole); }
    return interfaces;
}

std::optional<std::uint32_t> addressOf(DEVINST device) {
    ULONG address = 0;
    ULONG bytes = sizeof(address);
    if (CM_Get_DevNode_Registry_PropertyW(device, CM_DRP_ADDRESS, nullptr, &address, &bytes, 0) !=
        CR_SUCCESS) {
        return std::nullopt;
    }
    if (address == 0) { return std::nullopt; }
    return static_cast<std::uint32_t>(address);
}

// libusb's order (get_dev_port_number): the location information, the location path, then the
// address, which Microsoft's hub driver sets to the port.
std::optional<std::uint32_t> portOf(DEVINST device) {
    const std::vector<std::string> information = nodeStrings(device, CM_DRP_LOCATION_INFORMATION);
    const std::optional<std::uint32_t> fromInformation =
        information.empty() ? std::optional<std::uint32_t>{}
                            : winusb::portFromLocationInformation(information.front());
    if (fromInformation.has_value()) { return fromInformation; }
    const std::vector<std::string> paths = nodeStrings(device, CM_DRP_LOCATION_PATHS);
    const std::optional<std::uint32_t> fromPath = paths.empty()
                                                      ? std::optional<std::uint32_t>{}
                                                      : winusb::portFromLocationPath(paths.front());
    if (fromPath.has_value()) { return fromPath; }
    return addressOf(device);
}

// The device interface GUID_DEVINTERFACE_USB_HUB names for a hub node: the root hub's, from the
// port driver, or an external hub's, from the hub driver.
std::optional<std::wstring> hubPathOf(DEVINST hub) {
    std::wstring id = wideIdOf(hub);
    GUID hubClass = GUID_DEVINTERFACE_USB_HUB;
    ULONG chars = 0;
    const bool sized =
        CM_Get_Device_Interface_List_SizeW(&chars, &hubClass, id.data(),
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS &&
        chars > 1;
    if (!sized) { return std::nullopt; }
    std::vector<wchar_t> list(chars, L'\0');
    const bool listed =
        CM_Get_Device_Interface_ListW(&hubClass, id.data(), list.data(), chars,
                                      CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS &&
        list.front() != L'\0';
    if (!listed) { return std::nullopt; }
    return std::wstring(list.data());
}

struct HubConnection {
    std::wstring hubPath;
    std::uint32_t port = 0;
};

std::optional<HubConnection> hubConnectionOf(DEVINST device) {
    DEVINST hub = 0;
    if (CM_Get_Parent(&hub, device, 0) != CR_SUCCESS) { return std::nullopt; }
    const std::optional<std::wstring> path = hubPathOf(hub);
    const std::optional<std::uint32_t> port = portOf(device);
    if (!path.has_value() || !port.has_value()) { return std::nullopt; }
    return HubConnection{*path, *port};
}

HubDescriptor askHub(HANDLE hub, std::uint32_t port, const winusb::SetupPacket& setup) {
    std::vector<std::uint8_t> buffer = winusb::descriptorRequestBuffer(port, setup);
    const auto size = static_cast<DWORD>(buffer.size());
    DWORD returned = 0;
    if (DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, buffer.data(), size,
                        buffer.data(), size, &returned, nullptr) == 0) {
        return HubDescriptor{{}, static_cast<std::uint32_t>(GetLastError())};
    }
    return HubDescriptor{winusb::descriptorOfReply(buffer, returned), std::nullopt};
}

// Its first nine bytes for the total length, then the whole, as USBView's GetConfigDescriptor
// asks.
HubDescriptor configurationOf(HANDLE hub, std::uint32_t port, std::uint8_t index) {
    const HubDescriptor head =
        askHub(hub, port,
               winusb::configurationDescriptorRequest(index, winusb::kConfigurationHeaderBytes));
    if (head.refusal.has_value()) { return HubDescriptor{{}, head.refusal}; }
    const std::optional<std::uint16_t> total = winusb::configurationTotalLength(head.bytes);
    if (!total.has_value()) {
        return HubDescriptor{{}, static_cast<std::uint32_t>(ERROR_INVALID_DATA)};
    }
    return askHub(hub, port, winusb::configurationDescriptorRequest(index, *total));
}

// How many configurations the device descriptor names; one where the hub gave none, so the first
// is still asked for its interfaces.
std::uint8_t configurationCountOf(const HubDescriptor& device) {
    const std::optional<diagnostics::UsbDeviceDescriptor> parsed =
        diagnostics::parseDeviceDescriptor(device.bytes);
    if (!parsed.has_value() || parsed->configurations == 0) { return 1; }
    return parsed->configurations;
}

// Every configuration in index order, as the bus hands them to Android's getRawDescriptors() and to
// sysfs's descriptors file; a refusal of any of them is the answer for all.
HubDescriptor configurationsOf(HANDLE hub, std::uint32_t port, std::uint8_t count) {
    HubDescriptor all;
    for (std::uint8_t index = 0; index < count; ++index) {
        const HubDescriptor one = configurationOf(hub, port, index);
        if (one.refusal.has_value()) { return HubDescriptor{{}, one.refusal}; }
        all.bytes.insert(all.bytes.end(), one.bytes.begin(), one.bytes.end());
    }
    return all;
}

std::vector<UsbReportDescriptor>
reportDescriptorsOf(HANDLE hub, std::uint32_t port,
                    const std::vector<UsbInterfaceEntry>& interfaces) {
    std::vector<UsbReportDescriptor> descriptors;
    for (const UsbInterfaceEntry& entry : interfaces) {
        if (entry.classCode != kHidClass) { continue; }
        const winusb::SetupPacket request = winusb::reportDescriptorRequest(
            static_cast<std::uint8_t>(entry.number), winusb::kReportDescriptorRequestBytes);
        descriptors.push_back(UsbReportDescriptor{entry.number, askHub(hub, port, request)});
    }
    return descriptors;
}

} // namespace

UsbHardwareReading readUsbHardware(const std::string& usbDeviceInstanceId) {
    UsbHardwareReading reading;
    const std::optional<DEVINST> device = nodeOf(usbDeviceInstanceId);
    if (!device.has_value()) {
        reading.hubUnreachable = static_cast<std::uint32_t>(ERROR_NOT_FOUND);
        return reading;
    }
    reading.interfaces = interfacesOf(*device);
    const std::optional<HubConnection> connection = hubConnectionOf(*device);
    if (!connection.has_value()) {
        reading.hubUnreachable = static_cast<std::uint32_t>(ERROR_NOT_FOUND);
        return reading;
    }
    // USBView's and libusb's open: write access, shared, with no elevation asked for.
    const HANDLE hub = CreateFileW(connection->hubPath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
    if (hub == INVALID_HANDLE_VALUE) {
        reading.hubUnreachable = static_cast<std::uint32_t>(GetLastError());
        return reading;
    }
    reading.device = askHub(hub, connection->port, winusb::deviceDescriptorRequest());
    reading.configuration =
        configurationsOf(hub, connection->port, configurationCountOf(reading.device));
    reading.reportDescriptors = reportDescriptorsOf(hub, connection->port, reading.interfaces);
    CloseHandle(hub);
    return reading;
}

} // namespace dish::source::usb
