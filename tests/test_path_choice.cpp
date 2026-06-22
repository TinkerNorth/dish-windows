// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PathChoiceTest (PURE, 2). 1:1 port of dish-android source/usb/PathChoiceTest.kt.
// Every PathChoice round-trips through its storage value; an absent or
// unrecognised stored value resolves to nullopt (== Auto) for forward-compat.

#include "core/reducer/PathChoice.h"

#include <catch2/catch_test_macros.hpp>

using dish::reducer::PathChoice;
using dish::reducer::pathChoiceFromStorageValue;
using dish::reducer::toStorageValue;

TEST_CASE("every choice round trips through its storage value", "[usb-pathchoice]") {
    for (PathChoice choice : {PathChoice::Direct, PathChoice::Standard}) {
        const auto decoded = pathChoiceFromStorageValue(toStorageValue(choice));
        REQUIRE(decoded.has_value());
        CHECK(*decoded == choice);
    }
}

TEST_CASE("an absent or unrecognised storage value resolves to nullopt (Auto)",
          "[usb-pathchoice]") {
    CHECK_FALSE(pathChoiceFromStorageValue("").has_value());
    CHECK_FALSE(pathChoiceFromStorageValue("legacy-unknown-value").has_value());
}
