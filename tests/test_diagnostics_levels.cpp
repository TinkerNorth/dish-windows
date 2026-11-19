// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The levels of Diagnostics below the bundle (docs/controller-db.md section 9):
// the last write's verdict and how it is worded, the test outcomes and their
// bound, the mail hand-off's decisions, how the machine is named, and the JSON
// writer the bundle is printed with.

#include "core/diagnostics/HostIdentity.h"
#include "core/diagnostics/Json.h"
#include "core/diagnostics/MailHandoff.h"
#include "core/diagnostics/PadTestOutcomes.h"
#include "core/diagnostics/WriteVerdict.h"
#include "source/store/PadTestOutcomeStore.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

using namespace dish::diagnostics;

TEST_CASE("write verdict: a write is recorded with its route, report id and outcome",
          "[diagnostics][write]") {
    const std::array<std::uint8_t, 3> report{0x02, 0xFF, 0x00};
    const WriteVerdict ok =
        verdictOf(WriteRoute::InterruptOut, report.data(), report.size(), true, 0, 1000);
    CHECK(ok == WriteVerdict{true, WriteRoute::InterruptOut, 0x02, true, 0, 1000});

    SECTION("a success carries no error code, whatever the OS last said") {
        CHECK(
            verdictOf(WriteRoute::SetReport, report.data(), report.size(), true, 1167, 5).osError ==
            0U);
    }
    SECTION("a failure keeps its Win32 code") {
        const WriteVerdict failed =
            verdictOf(WriteRoute::SetReport, report.data(), report.size(), false, 1167, 5);
        CHECK_FALSE(failed.ok);
        CHECK(failed.osError == 1167U);
    }
    SECTION("an empty report has id 0") {
        CHECK(verdictOf(WriteRoute::InterruptOut, nullptr, 0, false, 0, 5).reportId == 0);
    }
    SECTION("a verdict never written is told apart from a failed one") {
        CHECK_FALSE(WriteVerdict{}.written);
    }
}

TEST_CASE("write verdict: worded the way the card and the bundle show it", "[diagnostics][write]") {
    CHECK(writeRouteToken(WriteRoute::InterruptOut) == "interruptOut");
    CHECK(writeRouteToken(WriteRoute::SetReport) == "setReport");
    CHECK(reportIdText(0x02) == "02");
    CHECK(reportIdText(0xA5) == "a5");
    CHECK(osErrorName(0).empty());
    CHECK(osErrorName(1167) == "ERROR_DEVICE_NOT_CONNECTED");
    CHECK(osErrorName(1460) == "ERROR_TIMEOUT");
    CHECK(osErrorName(31) == "ERROR_GEN_FAILURE");
    CHECK(osErrorName(424242) == "win32:424242");
}

TEST_CASE("test outcomes: the last twenty, oldest first", "[diagnostics][outcomes]") {
    OutcomeLog log;
    for (int i = 0; i < 25; ++i) {
        log = appendOutcome(log, TestOutcome{"rumble.weak", "worked", i});
    }
    REQUIRE(log.size() == kOutcomesKept);
    CHECK(kOutcomesKept == 20U);
    CHECK(log.front().atMs == 5);
    CHECK(log.back().atMs == 24);
    SECTION("below the bound nothing is dropped") {
        const OutcomeLog two = appendOutcome(appendOutcome({}, TestOutcome{"a", "worked", 1}),
                                             TestOutcome{"b", "didNotWork", 2});
        REQUIRE(two.size() == 2U);
        CHECK(two[0].test == "a");
        CHECK(two[1].test == "b");
    }
}

TEST_CASE("test outcomes: the bench's motors name the test, the answer names the result",
          "[diagnostics][outcomes]") {
    CHECK(rumbleTestName("weak") == std::optional<std::string>("rumble.weak"));
    CHECK(rumbleTestName("strong") == std::optional<std::string>("rumble.strong"));
    CHECK(rumbleTestName("both") == std::optional<std::string>("rumble.both"));
    CHECK_FALSE(rumbleTestName("lightbar").has_value());
    CHECK(outcomeResult(true) == "worked");
    CHECK(outcomeResult(false) == "didNotWork");
}

TEST_CASE("test outcomes: the store keeps each slot's apart", "[diagnostics][outcomes]") {
    dish::source::PadTestOutcomeStore store;
    CHECK_FALSE(store.lastFor("a").has_value());
    store.record("a", TestOutcome{"rumble.weak", "worked", 1});
    store.record("b", TestOutcome{"rumble.both", "didNotWork", 2});
    store.record("a", TestOutcome{"rumble.strong", "didNotWork", 3});
    CHECK(store.outcomesFor("a").size() == 2U);
    CHECK(store.lastFor("a")->test == "rumble.strong");
    CHECK(store.lastFor("b")->result == "didNotWork");
    for (int i = 0; i < 30; ++i) { store.record("a", TestOutcome{"rumble.weak", "worked", i}); }
    CHECK(store.outcomesFor("a").size() == kOutcomesKept);
}

TEST_CASE("mail: the subject and the attachment's name are the contract's", "[diagnostics][mail]") {
    CHECK(mailSubject("2.8.0", "AYN Thor") ==
          "Dish diagnostics: Dish for Windows 2.8.0 on AYN Thor");
    CHECK(kSupportAddress == "support@tinkernorth.com");
    CHECK(bundleFileName(CivilTime{2026, 11, 19, 21, 4, 11}) ==
          "dish-diagnostics-dish-windows-20261119-210411.json");
}

TEST_CASE("mail: UTC times from epoch milliseconds", "[diagnostics][mail]") {
    CHECK(isoUtc(0) == "1970-01-01T00:00:00Z");
    CHECK(isoUtc(1763586251000) == "2025-11-19T21:04:11Z");
    CHECK(isoUtc(1795122251000) == "2026-11-19T21:04:11Z");
    CHECK(isoUtc(951782400000) == "2000-02-29T00:00:00Z");
    CHECK(civilUtc(-1000) == CivilTime{1969, 12, 31, 23, 59, 59});
}

TEST_CASE("mail: which way the mail goes after Simple MAPI answers", "[diagnostics][mail]") {
    CHECK(nextStepAfterMapi(0UL) == MailStep::Done);
    // The person closed the compose window: their answer, not a missing client.
    CHECK(nextStepAfterMapi(1UL) == MailStep::Done);
    // MAPI_E_FAILURE, MAPI_E_LOGIN_FAILURE, MAPI_E_NOT_SUPPORTED: no client answered.
    CHECK(nextStepAfterMapi(2UL) == MailStep::OpenMailtoAndRevealFolder);
    CHECK(nextStepAfterMapi(3UL) == MailStep::OpenMailtoAndRevealFolder);
    CHECK(nextStepAfterMapi(26UL) == MailStep::OpenMailtoAndRevealFolder);
    CHECK(nextStepAfterMapi(std::nullopt) == MailStep::OpenMailtoAndRevealFolder);
}

TEST_CASE("mail: a mailto link encodes everything a URL could misread", "[diagnostics][mail]") {
    CHECK(mailtoUrl("support@tinkernorth.com", "Dish diagnostics: a b", "line 1\nline 2 & 3") ==
          "mailto:support@tinkernorth.com?subject=Dish%20diagnostics%3A%20a%20b"
          "&body=line%201%0Aline%202%20%26%203");
    CHECK(mailtoUrl("x@y", "s", "\xC3\xA4") == "mailto:x@y?subject=s&body=%C3%A4");
}

TEST_CASE("mail: a long body is cut at a whole character to keep the link openable",
          "[diagnostics][mail]") {
    const std::string body(5000, 'a');
    const std::string url = mailtoUrl("x@y", "s", body, 100);
    CHECK(url.size() == 100U);
    // "ä" is two bytes and six encoded characters: never half of one.
    const std::string umlauts = mailtoUrl("x@y", "s", "\xC3\xA4\xC3\xA4", 27);
    CHECK(umlauts == "mailto:x@y?subject=s&body=");
    CHECK(mailtoUrl("x@y", "s", "\xC3\xA4\xC3\xA4", 33) == "mailto:x@y?subject=s&body=%C3%A4");
}

TEST_CASE("host identity: the machine's model from its firmware strings",
          "[diagnostics][identity]") {
    CHECK(machineModelOf("AYN", "Thor") == "AYN Thor");
    CHECK(machineModelOf("LENOVO", "LENOVO ThinkPad X1") == "LENOVO ThinkPad X1");
    CHECK(machineModelOf("  Dell Inc. ", " XPS 13 9310 ") == "Dell Inc. XPS 13 9310");
    CHECK(machineModelOf("System manufacturer", "System Product Name") == "Unknown PC");
    CHECK(machineModelOf("To Be Filled By O.E.M.", "Thor") == "Thor");
    CHECK(machineModelOf("AYN", "") == "AYN");
    CHECK(machineModelOf("", "") == "Unknown PC");
}

TEST_CASE("host identity: Windows 11 is the 10.0 kernel past build 22000",
          "[diagnostics][identity]") {
    CHECK(windowsMajorFromKernel("10.0.22631") == 11);
    CHECK(windowsMajorFromKernel("10.0.22000") == 11);
    CHECK(windowsMajorFromKernel("10.0.19045") == 10);
    CHECK(windowsMajorFromKernel("garbage") == 0);
    CHECK(windowsMajorFromKernel("10.0.x") == 0);
}

TEST_CASE("host identity: the build code reads the way Android's versionCode does",
          "[diagnostics][identity]") {
    CHECK(buildCodeOf("2.8.0") == "20800");
    CHECK(buildCodeOf("10.12.3") == "101203");
    CHECK(buildCodeOf("2.8") == "2.8");
    CHECK(buildCodeOf("2.8.0-dev") == "2.8.0-dev");
}

TEST_CASE("json: values, ordered keys and two-space indentation", "[diagnostics][json]") {
    Json object = Json::object();
    object.set("b", 1);
    object.set("a", "x");
    object.set("n", Json());
    object.set("t", true);
    Json list = Json::array();
    list.push(std::int64_t{1763586000000});
    object.set("list", std::move(list));
    object.set("empty", Json::array());
    CHECK(object.dump() == "{\n"
                           "  \"b\": 1,\n"
                           "  \"a\": \"x\",\n"
                           "  \"n\": null,\n"
                           "  \"t\": true,\n"
                           "  \"list\": [\n"
                           "    1763586000000\n"
                           "  ],\n"
                           "  \"empty\": []\n"
                           "}\n");
    SECTION("a key set twice keeps its place and its last value") {
        object.set("b", 2);
        CHECK(object.find("b")->integerValue() == 2);
        CHECK(object.dump().find("\"b\": 2") < object.dump().find("\"a\""));
    }
}

TEST_CASE("json: strings are escaped, UTF-8 passes through", "[diagnostics][json]") {
    CHECK(jsonQuoted("a\"b\\c") == "\"a\\\"b\\\\c\"");
    CHECK(jsonQuoted("line\nnext\ttab") == "\"line\\nnext\\ttab\"");
    CHECK(jsonQuoted(std::string("\x01", 1)) == "\"\\u0001\"");
    CHECK(jsonQuoted("\xC3\xA4") == "\"\xC3\xA4\"");
    CHECK(Json::orNull(std::optional<std::string>()).kind() == Json::Kind::Null);
    CHECK(Json::orNull(std::optional<int>(3)).integerValue() == 3);
}
