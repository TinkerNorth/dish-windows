// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/reducer/Backoff.h"
#include "core/reducer/UpdateMachine.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("one file names the satellite reconnect ladder and the update-check ladder apart",
          "[backoff][update]") {
    CHECK(dish::reducer::kBackoffBaseMs == 1000);
    CHECK(dish::reducer::backoffDelayMs(2) == 2000);
    CHECK(dish::reducer::update_schedule::kBackoffBaseMs == 10 * 60 * 1000);
    CHECK(dish::reducer::update_schedule::backoffDelayMs(2) == 20 * 60 * 1000);
}
