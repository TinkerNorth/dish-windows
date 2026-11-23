// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Diagnostics by email (docs/controller-db.md section 8,
// docs/diagnostics-bundle.md "The mail"): the file is saved under the app's
// data folder, then handed to the person's own mail client through Simple
// MAPI; when no MAPI client answers, a mailto: link opens with the subject and
// the body. Either way the folder is revealed, so the person can attach the
// file when the mail app leaves it out. Nothing leaves the machine until they
// press Send. The decisions are here; the Win32 calls are the system layer's.

#pragma once

#include "core/Bits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dish::diagnostics {

inline constexpr std::string_view kSupportAddress = "support@tinkernorth.com";
inline constexpr std::string_view kAppName = "dish-windows";
inline constexpr std::string_view kAppDisplayName = "Dish for Windows";

struct CivilTime {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;

    bool operator==(const CivilTime&) const = default;
};

// UTC from milliseconds since the epoch, by days-from-civil's inverse (Howard Hinnant's
// algorithm), so the bundle's times need no time-zone database.
inline CivilTime civilUtc(std::int64_t epochMs) {
    constexpr std::int64_t kMsPerSecond = 1000;
    constexpr std::int64_t kSecondsPerDay = 86400;
    std::int64_t seconds = epochMs / kMsPerSecond;
    if (epochMs % kMsPerSecond < 0) { --seconds; }
    std::int64_t days = seconds / kSecondsPerDay;
    std::int64_t secondOfDay = seconds % kSecondsPerDay;
    if (secondOfDay < 0) {
        secondOfDay += kSecondsPerDay;
        --days;
    }
    const std::int64_t shifted = days + 719468;
    const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const std::int64_t dayOfEra = shifted - era * 146097;
    const std::int64_t yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const std::int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const std::int64_t monthIndex = (5 * dayOfYear + 2) / 153;
    const std::int64_t day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    const std::int64_t month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    const std::int64_t year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
    return CivilTime{static_cast<int>(year),
                     static_cast<int>(month),
                     static_cast<int>(day),
                     static_cast<int>(secondOfDay / 3600),
                     static_cast<int>((secondOfDay % 3600) / 60),
                     static_cast<int>(secondOfDay % 60)};
}

namespace detail {

inline std::string padded(int value, int width) {
    std::string digits = std::to_string(value);
    while (static_cast<int>(digits.size()) < width) { digits.insert(digits.begin(), '0'); }
    return digits;
}

} // namespace detail

// "2026-11-19T21:04:11Z"
inline std::string isoUtc(std::int64_t epochMs) {
    const CivilTime t = civilUtc(epochMs);
    return detail::padded(t.year, 4) + "-" + detail::padded(t.month, 2) + "-" +
           detail::padded(t.day, 2) + "T" + detail::padded(t.hour, 2) + ":" +
           detail::padded(t.minute, 2) + ":" + detail::padded(t.second, 2) + "Z";
}

// "dish-diagnostics-dish-windows-20261119-210411.json", the person's local time.
inline std::string bundleFileName(const CivilTime& local) {
    return "dish-diagnostics-" + std::string(kAppName) + "-" + detail::padded(local.year, 4) +
           detail::padded(local.month, 2) + detail::padded(local.day, 2) + "-" +
           detail::padded(local.hour, 2) + detail::padded(local.minute, 2) +
           detail::padded(local.second, 2) + ".json";
}

// Simple MAPI's own answers (mapi.h): success, the person closed the compose window, and the
// codes a machine with no MAPI mail client gives.
inline constexpr unsigned long kMapiSuccess = 0;
inline constexpr unsigned long kMapiUserAbort = 1;

// RevealFolder follows a hand-off that went: a MAPI provider may still leave the attachment out,
// and the new Outlook and the Mail app answer no Simple MAPI at all.
enum class MailStep : std::uint8_t { RevealFolder, OpenMailtoAndRevealFolder };

// `mapiAnswer` is nullopt when no MAPI entry point could be loaded at all. A closed compose
// window is the person's answer, not a missing client, so it does not fall back.
inline MailStep nextStepAfterMapi(std::optional<unsigned long> mapiAnswer) {
    if (!mapiAnswer.has_value()) { return MailStep::OpenMailtoAndRevealFolder; }
    const bool handedOver = *mapiAnswer == kMapiSuccess || *mapiAnswer == kMapiUserAbort;
    return handedOver ? MailStep::RevealFolder : MailStep::OpenMailtoAndRevealFolder;
}

namespace detail {

inline bool isUnreserved(unsigned char ch) {
    const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    const bool digit = ch >= '0' && ch <= '9';
    return alpha || digit || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

inline void appendEncoded(std::string& out, unsigned char ch) {
    constexpr std::string_view kHex = "0123456789ABCDEF";
    if (isUnreserved(ch)) {
        out.push_back(static_cast<char>(ch));
        return;
    }
    out.push_back('%');
    out.push_back(kHex[bits::highNibble(ch)]);
    out.push_back(kHex[bits::lowNibble(ch)]);
}

// The byte length of the UTF-8 sequence a lead byte starts; a stray continuation byte is one.
inline std::size_t utf8SequenceLength(unsigned char lead) {
    if (lead >= 0xF0) { return 4; }
    if (lead >= 0xE0) { return 3; }
    if (lead >= 0xC0) { return 2; }
    return 1;
}

inline std::string encoded(std::string_view text) {
    std::string out;
    for (const char ch : text) { appendEncoded(out, static_cast<unsigned char>(ch)); }
    return out;
}

} // namespace detail

// A mailto: link a shell can open: past `maxLength` the body is cut at a whole character,
// because mail clients and the shell refuse longer links and a cut one still has its subject.
inline constexpr std::size_t kMailtoMaxLength = 2000;

inline std::string mailtoUrl(std::string_view to, std::string_view subject, std::string_view body,
                             std::size_t maxLength = kMailtoMaxLength) {
    std::string url = "mailto:" + std::string(to) + "?subject=" + detail::encoded(subject);
    const std::string bodyKey = "&body=";
    if (url.size() + bodyKey.size() >= maxLength) { return url; }
    url += bodyKey;
    std::size_t at = 0;
    while (at < body.size()) {
        const auto lead = static_cast<unsigned char>(body[at]);
        const std::size_t length = std::min(detail::utf8SequenceLength(lead), body.size() - at);
        const std::string piece = detail::encoded(body.substr(at, length));
        if (url.size() + piece.size() > maxLength) { break; }
        url += piece;
        at += length;
    }
    return url;
}

} // namespace dish::diagnostics
