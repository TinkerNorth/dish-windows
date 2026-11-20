// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// GetSystemPowerStatus's four bytes, read when the support report is prepared:
// the battery and whether battery saver is on, which explain a latency or
// rumble complaint on a handheld. What they mean is
// core/diagnostics/DeviceFacts.h's to decide.

#pragma once

#include "core/diagnostics/DeviceFacts.h"

#include <optional>

namespace dish::source {

// Empty when the call failed.
std::optional<diagnostics::PowerStatusBytes> readPowerStatus();

} // namespace dish::source
