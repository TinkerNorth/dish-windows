// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The client-to-pad level of Diagnostics (docs/controller-db.md section 9):
// the last Direct write's verdict, per pad. The output worker records it after
// each write, on its own thread and without allocating, so the value is a
// trivially copyable record a seqlock can carry; the page and the bundle word
// it from here.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace dish::diagnostics {

// How the report left the host. An output report goes through WriteFile, which the HID class
// driver sends over the interrupt OUT pipe; a feature report goes through HidD_SetFeature, a
// SET_REPORT control transfer.
enum class WriteRoute : std::uint8_t { InterruptOut, SetReport };

struct WriteVerdict {
    // False until the pad has been written once.
    bool written = false;
    WriteRoute route = WriteRoute::InterruptOut;
    std::uint8_t reportId = 0;
    bool ok = false;
    // GetLastError() after a failed write, 0 for a success or a failure the OS gave no code for.
    std::uint32_t osError = 0;
    // Wall-clock milliseconds since the epoch.
    std::int64_t atMs = 0;

    bool operator==(const WriteVerdict&) const = default;
};

static_assert(std::is_trivially_copyable_v<WriteVerdict>, "a seqlock copies it word by word");

inline WriteVerdict verdictOf(WriteRoute route, const std::uint8_t* report, std::size_t len,
                              bool ok, std::uint32_t osError, std::int64_t atMs) {
    const std::uint8_t reportId = (report != nullptr && len > 0) ? report[0] : 0;
    return WriteVerdict{true, route, reportId, ok, ok ? 0U : osError, atMs};
}

inline std::string_view writeRouteToken(WriteRoute route) {
    switch (route) {
    case WriteRoute::InterruptOut:
        return "interruptOut";
    case WriteRoute::SetReport:
        return "setReport";
    }
    return "interruptOut";
}

struct OsErrorName {
    std::uint32_t code;
    std::string_view name;
};

// The Win32 codes a HID write comes back with, by the names winerror.h gives them.
inline constexpr std::array<OsErrorName, 14> kOsErrorNames{{
    {1, "ERROR_INVALID_FUNCTION"},
    {5, "ERROR_ACCESS_DENIED"},
    {6, "ERROR_INVALID_HANDLE"},
    {22, "ERROR_BAD_COMMAND"},
    {23, "ERROR_CRC"},
    {31, "ERROR_GEN_FAILURE"},
    {50, "ERROR_NOT_SUPPORTED"},
    {87, "ERROR_INVALID_PARAMETER"},
    {121, "ERROR_SEM_TIMEOUT"},
    {995, "ERROR_OPERATION_ABORTED"},
    {997, "ERROR_IO_PENDING"},
    {1167, "ERROR_DEVICE_NOT_CONNECTED"},
    {1460, "ERROR_TIMEOUT"},
    {1784, "ERROR_INVALID_USER_BUFFER"},
}};

// Empty for no error; the winerror.h name where it is one of the above; else "win32:<code>".
inline std::string osErrorName(std::uint32_t code) {
    if (code == 0) { return {}; }
    for (const OsErrorName& entry : kOsErrorNames) {
        if (entry.code == code) { return std::string(entry.name); }
    }
    return "win32:" + std::to_string(code);
}

// A report id the way the bundle writes it: two lowercase hex digits.
inline std::string reportIdText(std::uint8_t reportId) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out(2, '0');
    out[0] = kDigits[static_cast<std::size_t>(reportId >> 4)];
    out[1] = kDigits[static_cast<std::size_t>(reportId & 0x0F)];
    return out;
}

} // namespace dish::diagnostics
