// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Which standing Moonlight bindings the view model puts back when a pad appears, and under
// which slot: a pad is followed by its identity, and SDL's instance id is the order pads were
// plugged, not the pad.

#include "core/moonlight/MoonlightBindingReattach.h"

#include <QList>
#include <QSet>
#include <QString>

#include <catch2/catch_test_macros.hpp>

using dish::models::MoonlightBinding;
using dish::moonlight::bindingsToReattach;
using dish::moonlight::PresentPad;

namespace {

const QString kPad = QStringLiteral("sdl:1");
const QString kOtherPad = QStringLiteral("sdl:2");
const QString kHost = QStringLiteral("ml:ip:192.168.0.2");
const QString kDualSense = QStringLiteral("guid:0300aabb/serial:11:22:33");
const QString kSecondDualSense = QStringLiteral("guid:0300aabb/serial:44:55:66");

MoonlightBinding standing(const QString& slot, const QString& identity = QString()) {
    MoonlightBinding binding;
    binding.slotId = slot;
    binding.hostId = kHost;
    binding.padIdentity = identity;
    return binding;
}

const QSet<QString> kNone;

} // namespace

TEST_CASE("a record without an identity follows its slot id and learns the pad's identity",
          "[moonlight][binding][reattach]") {
    const auto plan = bindingsToReattach({standing(kPad)}, {{kPad, kDualSense}}, kNone);
    REQUIRE(plan.size() == 1);
    CHECK(plan.front().binding.slotId == kPad);
    CHECK(plan.front().binding.hostId == kHost);
    CHECK(plan.front().slotId == kPad);
    CHECK(plan.front().identity == kDualSense);
}

TEST_CASE("a record with an identity follows its pad to the slot it holds now",
          "[moonlight][binding][reattach]") {
    const auto plan =
        bindingsToReattach({standing(kPad, kDualSense)}, {{kOtherPad, kDualSense}}, kNone);
    REQUIRE(plan.size() == 1);
    CHECK(plan.front().binding.slotId == kPad);
    CHECK(plan.front().slotId == kOtherPad);
    CHECK(plan.front().identity == kDualSense);
}

TEST_CASE("a record with an identity ignores another pad that took its old slot id",
          "[moonlight][binding][reattach]") {
    CHECK(bindingsToReattach({standing(kPad, kDualSense)}, {{kPad, kSecondDualSense}}, kNone)
              .isEmpty());
}

TEST_CASE("two records of one identity take two pads, never the same one",
          "[moonlight][binding][reattach]") {
    const auto plan =
        bindingsToReattach({standing(kPad, kDualSense), standing(kOtherPad, kDualSense)},
                           {{kOtherPad, kDualSense}, {kPad, kDualSense}}, kNone);
    REQUIRE(plan.size() == 2);
    CHECK(plan[0].slotId == kOtherPad);
    CHECK(plan[1].slotId == kPad);
}

TEST_CASE("a pad that is not here or already drives its host is left alone",
          "[moonlight][binding][reattach]") {
    CHECK(bindingsToReattach({standing(kPad)}, {}, kNone).isEmpty());
    CHECK(bindingsToReattach({standing(kPad)}, {{kPad, {}}}, {kPad}).isEmpty());
}

TEST_CASE("the driving check applies to the slot the pad holds now, not the one the record names",
          "[moonlight][binding][reattach]") {
    const auto standingPad = standing(kPad, kDualSense);
    CHECK(bindingsToReattach({standingPad}, {{kOtherPad, kDualSense}}, {kOtherPad}).isEmpty());
    CHECK(bindingsToReattach({standingPad}, {{kOtherPad, kDualSense}}, {kPad}).size() == 1);
}

TEST_CASE("only the bindings whose pads are here are put back", "[moonlight][binding][reattach]") {
    const auto plan =
        bindingsToReattach({standing(kPad), standing(kOtherPad)}, {{kOtherPad, {}}}, kNone);
    REQUIRE(plan.size() == 1);
    CHECK(plan.front().slotId == kOtherPad);
}
