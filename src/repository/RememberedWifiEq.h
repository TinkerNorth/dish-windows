// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Value-equality for models::RememberedWifi, declared in the dish::models
// namespace so ADL finds it. The remembered-satellite repository needs it for
// the RepositoryContract round-trip checks and for the ConnectionStore facade's
// idempotent "skip the write when the row is unchanged" guard (mirrors
// dish-android's data-class structural equality on RememberedSatellite).
//
// Lives here rather than in Models.h to keep Wave 1's frozen DTO header
// untouched; it is purely additive and field-wise.

#pragma once

#include "Models/Models.h"

namespace dish::models {

inline bool operator==(const RememberedWifi& a, const RememberedWifi& b) {
    return a.id == b.id && a.name == b.name && a.ip == b.ip && a.udpPort == b.udpPort &&
           a.pairPort == b.pairPort && a.httpPort == b.httpPort && a.machineId == b.machineId;
}

inline bool operator!=(const RememberedWifi& a, const RememberedWifi& b) { return !(a == b); }

} // namespace dish::models
