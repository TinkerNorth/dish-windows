// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A record's `expect` against what the Direct path observed at attach
// (docs/controller-db.md section 4.2). Each disagreement is one line in the
// contract's form, "endpoints.out: expected true, found false", which the
// Diagnostics card and the bundle carry verbatim. A side either party does not
// know is not compared.
//
// Windows' HID class API never shows a USB endpoint, only the collection's
// report lengths: an input report length above zero stands for the IN
// endpoint, an output report length above zero for the OUT one. The HID class
// driver sends an output report over the interrupt OUT pipe when the interface
// has one and as a SET_REPORT otherwise, so the second is the observable, not
// the endpoint itself (tests/PARITY.md says so).

#pragma once

#include "core/controllerdb/ControllerRecord.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dish::controllerdb {

struct ObservedHardware {
    std::optional<bool> endpointIn;
    std::optional<bool> endpointOut;

    bool operator==(const ObservedHardware&) const = default;
};

inline ObservedHardware observedFromHidCaps(int inputReportBytes, int outputReportBytes) {
    return ObservedHardware{inputReportBytes > 0, outputReportBytes > 0};
}

namespace detail {

inline std::optional<bool> expectedPresence(Expectation expectation) {
    switch (expectation) {
    case Expectation::Present:
        return true;
    case Expectation::Absent:
        return false;
    case Expectation::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

inline std::string_view boolWord(bool value) { return value ? "true" : "false"; }

inline void compareOne(std::string_view field, Expectation expected, std::optional<bool> found,
                       std::vector<std::string>& out) {
    const std::optional<bool> wanted = expectedPresence(expected);
    if (!wanted.has_value() || !found.has_value()) { return; }
    if (*wanted == *found) { return; }
    std::string line(field);
    line += ": expected ";
    line += boolWord(*wanted);
    line += ", found ";
    line += boolWord(*found);
    out.push_back(std::move(line));
}

} // namespace detail

inline std::vector<std::string> expectMismatches(const Expect& expect,
                                                 const ObservedHardware& observed) {
    std::vector<std::string> out;
    detail::compareOne("endpoints.in", expect.endpointIn, observed.endpointIn, out);
    detail::compareOne("endpoints.out", expect.endpointOut, observed.endpointOut, out);
    return out;
}

} // namespace dish::controllerdb
