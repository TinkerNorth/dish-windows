// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The per-type capability gate: a client must not claim a proto::kCap* bit the
// type's catalog features do not offer. The stated example is switchpro, which
// advertises analogTriggers supported:false, so a pad with real triggers still
// must not claim the cap on that type.
//
// This reducer had no test file at all. Its two rules are worth pinning
// separately: which bits the per-slug intersection owns, and which ones pass
// through it untouched because they are outside the protocol-1 vocabulary.

#include "core/reducer/CatalogFeatureGate.h"

#include <catch2/catch_test_macros.hpp>

using dish::models::CatalogFeatureDto;
using dish::models::CatalogTypeDto;
using dish::reducer::allowedCapsForType;
using dish::reducer::typeOffersTouchpadDs4;
namespace catalog = dish::catalog;
namespace proto = dish::proto;

namespace {

CatalogTypeDto typeWith(std::initializer_list<std::pair<QString, bool>> features) {
    CatalogTypeDto type;
    for (const auto& [slug, supported] : features) {
        CatalogFeatureDto f;
        f.supported = supported;
        type.features.insert(slug, f);
    }
    return type;
}

CatalogTypeDto typeWithTouchpad(bool supported, const QStringList& modes) {
    CatalogTypeDto type;
    CatalogFeatureDto f;
    f.supported = supported;
    f.modes = modes;
    type.features.insert(catalog::kFeatureTouchpad, f);
    return type;
}

constexpr std::uint16_t kAllGatedCaps = static_cast<std::uint16_t>(
    proto::kCapAnalogTriggers | proto::kCapRumble | proto::kCapMotion | proto::kCapLightbar);

} // namespace

TEST_CASE("caps gate: a type that offers everything keeps every detected bit", "[catalog][gate]") {
    const auto type = typeWith({{catalog::kFeatureAnalogTriggers, true},
                                {catalog::kFeatureRumble, true},
                                {catalog::kFeatureMotion, true},
                                {catalog::kFeatureLightbar, true}});
    CHECK(allowedCapsForType(kAllGatedCaps, type) == kAllGatedCaps);
}

TEST_CASE("caps gate: a type that offers nothing strips every gated bit", "[catalog][gate]") {
    const CatalogTypeDto bare;
    CHECK(allowedCapsForType(kAllGatedCaps, bare) == 0);
}

TEST_CASE("caps gate: the gate can only take bits away", "[catalog][gate]") {
    // A type offering a feature the pad did not detect does not conjure the bit.
    const auto type = typeWith({{catalog::kFeatureAnalogTriggers, true},
                                {catalog::kFeatureRumble, true},
                                {catalog::kFeatureMotion, true},
                                {catalog::kFeatureLightbar, true}});
    CHECK(allowedCapsForType(0, type) == 0);
    CHECK(allowedCapsForType(proto::kCapRumble, type) == proto::kCapRumble);
}

TEST_CASE("caps gate: supported:false is a refusal, not an absence", "[catalog][gate]") {
    // The switchpro case the header names: the pad has real triggers and the
    // type says analogTriggers supported:false, so the bit goes.
    const auto switchpro = typeWith({{catalog::kFeatureAnalogTriggers, false},
                                     {catalog::kFeatureRumble, true},
                                     {catalog::kFeatureMotion, true}});
    const std::uint16_t detected = static_cast<std::uint16_t>(
        proto::kCapAnalogTriggers | proto::kCapRumble | proto::kCapMotion);
    const std::uint16_t allowed = allowedCapsForType(detected, switchpro);
    CHECK((allowed & proto::kCapAnalogTriggers) == 0);
    CHECK((allowed & proto::kCapRumble) != 0);
    CHECK((allowed & proto::kCapMotion) != 0);
}

TEST_CASE("caps gate: each slug gates only its own bit", "[catalog][gate]") {
    // One feature at a time, so a wrong slug-to-bit pairing shows up as the
    // wrong survivor rather than as a plausible-looking word.
    struct Case {
        QString slug;
        std::uint16_t bit;
    };
    const Case cases[] = {{catalog::kFeatureAnalogTriggers, proto::kCapAnalogTriggers},
                          {catalog::kFeatureRumble, proto::kCapRumble},
                          {catalog::kFeatureMotion, proto::kCapMotion},
                          {catalog::kFeatureLightbar, proto::kCapLightbar}};
    for (const auto& c : cases) {
        const auto only = typeWith({{c.slug, true}});
        INFO("slug " << c.slug.toStdString());
        CHECK(allowedCapsForType(kAllGatedCaps, only) == c.bit);
    }
}

TEST_CASE("caps gate: bits outside the protocol-1 vocabulary pass through", "[catalog][gate]") {
    // This owns the per-slug intersection, not the word's full shape. The audio
    // and feedback caps are deliberately outside knownFeatureSlugs(), so a type
    // that mentions none of them must not lose them.
    const CatalogTypeDto bare;
    const std::uint16_t audio =
        static_cast<std::uint16_t>(proto::kCapMic | proto::kCapSpeaker | proto::kCapTriggerEffects);
    CHECK(allowedCapsForType(audio, bare) == audio);
}

TEST_CASE("caps gate: the gate is idempotent", "[catalog][gate]") {
    // Running it twice must not take a second pass at anything: a caller that
    // re-gates an already-gated word gets the same word back.
    const auto type = typeWith({{catalog::kFeatureRumble, true}});
    const std::uint16_t once = allowedCapsForType(kAllGatedCaps, type);
    CHECK(allowedCapsForType(once, type) == once);
}

TEST_CASE("touchpad gate: an absent touchpad feature offers no DS4 mode", "[catalog][gate]") {
    const CatalogTypeDto bare;
    CHECK_FALSE(typeOffersTouchpadDs4(bare));
}

TEST_CASE("touchpad gate: supported:false offers no DS4 mode whatever the modes say",
          "[catalog][gate]") {
    const QString ds4 = QString::fromLatin1(proto::touchpadModeName(proto::kTouchpadModeDs4).data());
    CHECK_FALSE(typeOffersTouchpadDs4(typeWithTouchpad(false, {ds4})));
}

TEST_CASE("touchpad gate: an empty mode list is a pre-modes catalog and reads as DS4",
          "[catalog][gate]") {
    // Empty means the satellite predates the modes field. The client falls back
    // to its prior assumption rather than gating the feature off.
    CHECK(typeOffersTouchpadDs4(typeWithTouchpad(true, {})));
}

TEST_CASE("touchpad gate: a mode list that names DS4 offers it, one that does not does not",
          "[catalog][gate]") {
    const QString ds4 = QString::fromLatin1(proto::touchpadModeName(proto::kTouchpadModeDs4).data());
    CHECK(typeOffersTouchpadDs4(typeWithTouchpad(true, {ds4})));
    CHECK(typeOffersTouchpadDs4(typeWithTouchpad(true, {QStringLiteral("mouse"), ds4})));
    CHECK_FALSE(typeOffersTouchpadDs4(typeWithTouchpad(true, {QStringLiteral("mouse")})));
    CHECK_FALSE(typeOffersTouchpadDs4(typeWithTouchpad(true, {QStringLiteral("")})));
}
