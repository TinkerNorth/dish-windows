// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// How the bundle and the issue match name this machine: its model from the
// firmware's manufacturer and product strings, the Windows major version from
// the kernel's build number, and the app's build code from its version. The
// system layer reads the raw strings; the shaping is here, where it is tested.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dish::diagnostics {

inline constexpr std::string_view kUnknownModel = "Unknown PC";

namespace detail {

inline std::string trimmed(std::string_view text) {
    const auto isSpace = [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; };
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && isSpace(text[first])) { ++first; }
    while (last > first && isSpace(text[last - 1])) { --last; }
    return std::string(text.substr(first, last - first));
}

inline bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) { return false; }
    return std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

// Firmware placeholders that name nothing ("To Be Filled By O.E.M.", "System Product Name").
inline bool isPlaceholder(std::string_view text) {
    constexpr std::array<std::string_view, 4> kPlaceholders{
        "To Be Filled By O.E.M.", "System Product Name", "System manufacturer", "Default string"};
    return std::any_of(kPlaceholders.begin(), kPlaceholders.end(),
                       [text](std::string_view p) { return startsWithIgnoringCase(text, p); });
}

inline std::string meaningful(std::string_view text) {
    const std::string clean = trimmed(text);
    return isPlaceholder(clean) ? std::string() : clean;
}

} // namespace detail

// "AYN" and "Thor" make "AYN Thor"; a product that already names its maker is not doubled.
inline std::string machineModelOf(std::string_view manufacturer, std::string_view product) {
    // Not const: both are returned, and a const local cannot be moved out.
    std::string maker = detail::meaningful(manufacturer);
    std::string name = detail::meaningful(product);
    if (name.empty() && maker.empty()) { return std::string(kUnknownModel); }
    if (name.empty()) { return maker; }
    if (maker.empty() || detail::startsWithIgnoringCase(name, maker)) { return name; }
    return maker + " " + name;
}

// "10.0.22631" is Windows 11: Microsoft kept the 10.0 kernel and moved the build past 22000.
inline int windowsMajorFromKernel(std::string_view kernelVersion) {
    constexpr int kFirstWindows11Build = 22000;
    const std::size_t lastDot = kernelVersion.rfind('.');
    if (lastDot == std::string_view::npos) { return 0; }
    int build = 0;
    for (const char ch : kernelVersion.substr(lastDot + 1)) {
        if (ch < '0' || ch > '9') { return 0; }
        build = build * 10 + (ch - '0');
    }
    return build >= kFirstWindows11Build ? 11 : 10;
}

// "2.8.0" is build 20800, the way dish-android's versionCode reads.
inline std::string buildCodeOf(std::string_view version) {
    std::vector<int> parts{0};
    for (const char ch : version) {
        if (ch == '.') {
            parts.push_back(0);
            continue;
        }
        if (ch < '0' || ch > '9') { return std::string(version); }
        parts.back() = parts.back() * 10 + (ch - '0');
    }
    constexpr std::size_t kParts = 3;
    if (parts.size() != kParts) { return std::string(version); }
    return std::to_string(parts[0] * 10000 + parts[1] * 100 + parts[2]);
}

} // namespace dish::diagnostics
