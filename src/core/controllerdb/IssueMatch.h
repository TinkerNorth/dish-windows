// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Which of a record's `support.issues` show on a pad's card here
// (docs/controller-db.md section 4.1): the issue's transport is the pad's,
// and its `device` and `os` are this machine's or absent. `device` is the
// machine's model as the bundle names it ("AYN Thor"), compared without case
// or surrounding space; an `os` map names its platforms, and only a `windows`
// entry whose version is this Windows' major version matches here.

#pragma once

#include "core/controllerdb/ControllerRecord.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dish::controllerdb {

struct HostFacts {
    std::string model;
    int windowsMajor = 0;
};

namespace detail {

inline std::string foldedForMatch(std::string_view text) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && isSpace(static_cast<unsigned char>(text[first]))) { ++first; }
    while (last > first && isSpace(static_cast<unsigned char>(text[last - 1]))) { --last; }
    std::string out(text.substr(first, last - first));
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

inline bool deviceMatches(const std::optional<std::string>& device, const HostFacts& host) {
    if (!device.has_value()) { return true; }
    return foldedForMatch(*device) == foldedForMatch(host.model);
}

inline bool osMatches(const std::vector<IssueOs>& os, const HostFacts& host) {
    if (os.empty()) { return true; }
    const std::string major = std::to_string(host.windowsMajor);
    return std::any_of(os.begin(), os.end(), [&major](const IssueOs& entry) {
        return entry.name == kPlatformName && entry.version == major;
    });
}

} // namespace detail

inline bool issueApplies(const Issue& issue, Transport padTransport, const HostFacts& host) {
    const bool sameTransport = issue.transport == padTransport;
    const bool thisMachine = detail::deviceMatches(issue.device, host);
    const bool thisSystem = detail::osMatches(issue.os, host);
    return sameTransport && thisMachine && thisSystem;
}

inline std::vector<Issue> applicableIssues(const ControllerRecord& record, Transport padTransport,
                                           const HostFacts& host) {
    std::vector<Issue> out;
    std::copy_if(record.issues.begin(), record.issues.end(), std::back_inserter(out),
                 [padTransport, &host](const Issue& issue) {
                     return issueApplies(issue, padTransport, host);
                 });
    return out;
}

} // namespace dish::controllerdb
