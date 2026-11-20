// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The support report's second cut (docs/diagnostics-bundle.md, "Second cut"):
// the report's category, subject and body, the decisions about each host's
// snapshot and how they are gathered, the session's wire counts, each pad's
// output lanes and voices, the PC's power facts and the Bluetooth link a HID
// path names. Every one of them is a decision the Qt layer only carries out.

#include "composer/HostSnapshotCollector.h"
#include "core/diagnostics/DeviceFacts.h"
#include "core/diagnostics/HostSnapshot.h"
#include "core/diagnostics/Json.h"
#include "core/diagnostics/PadLanes.h"
#include "core/diagnostics/ReportForm.h"
#include "core/diagnostics/WireCounters.h"
#include "core/input/HidTransport.h"
#include "core/model/Protocol.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using dish::composer::HostSnapshotCollector;
using dish::composer::HostSnapshotGateway;
using dish::composer::HostSnapshots;
using dish::composer::SnapshotFetchReply;
using dish::diagnostics::Json;
using dish::diagnostics::OutputLane;
using dish::diagnostics::PadOutputCounters;
using dish::diagnostics::PowerStatusBytes;
using dish::diagnostics::ReceivedKind;
using dish::diagnostics::ReportCategory;
using dish::diagnostics::SentKind;
using dish::diagnostics::SnapshotError;
using dish::diagnostics::SnapshotReply;
using dish::diagnostics::VoiceState;
using dish::diagnostics::WireCounters;

namespace {

// Answers each fetch only when the test says, so the collector's waiting is observable.
class HeldGateway : public HostSnapshotGateway {
  public:
    void fetch(const std::string& connectionKey, Reply reply) override {
        asked.push_back(connectionKey);
        pending.emplace(connectionKey, std::move(reply));
    }

    void answer(const std::string& connectionKey, SnapshotFetchReply reply) {
        const auto it = pending.find(connectionKey);
        REQUIRE(it != pending.end());
        Reply waiting = std::move(it->second);
        pending.erase(it);
        waiting(std::move(reply));
    }

    std::vector<std::string> asked;
    std::map<std::string, Reply> pending;
};

struct CollectedSnapshots {
    int calls = 0;
    HostSnapshots last;

    void take(HostSnapshots snapshots) {
        ++calls;
        last = std::move(snapshots);
    }
};

Json connectionSnapshot() {
    Json snapshot = Json::object();
    snapshot.set("format", "satellite-connection-diagnostics.v1");
    return snapshot;
}

} // namespace

// ── The report ──────────────────────────────────────────────────────────────

TEST_CASE("report: each category has the contract's token and reads back from it",
          "[diagnostics][report]") {
    CHECK(reportCategoryToken(ReportCategory::NotRecognised) == "notRecognised");
    CHECK(reportCategoryToken(ReportCategory::NoFeedback) == "noFeedback");
    CHECK(reportCategoryToken(ReportCategory::Connection) == "connection");
    CHECK(reportCategoryToken(ReportCategory::Audio) == "audio");
    CHECK(reportCategoryToken(ReportCategory::Other) == "other");
    CHECK(reportCategoryToken(ReportCategory::Question) == "question");
    for (const auto& row : dish::diagnostics::kReportCategories) {
        CHECK(dish::diagnostics::reportCategoryFromToken(row.token) ==
              std::optional<ReportCategory>(row.category));
    }
    CHECK_FALSE(dish::diagnostics::reportCategoryFromToken("bug").has_value());
}

TEST_CASE("report: the subject names the category in the person's words, then the app and PC",
          "[diagnostics][report]") {
    CHECK(dish::diagnostics::reportSubject("Keine Vibration", "2.8.0", "AYN Thor") ==
          "Dish support: Keine Vibration \xC2\xB7 Dish for Windows 2.8.0 on AYN Thor");
}

TEST_CASE("report: the person's words go first in the body, the summary after",
          "[diagnostics][report]") {
    SECTION("their text, a blank line, the summary") {
        CHECK(dish::diagnostics::reportMailBody("  It never rumbles.\n", "Dish 2.8.0") ==
              "It never rumbles.\n\nDish 2.8.0");
    }
    SECTION("nothing written leaves the summary alone") {
        CHECK(dish::diagnostics::reportMailBody(" \n\t", "Dish 2.8.0") == "Dish 2.8.0");
    }
}

TEST_CASE("report: the controller is its record, else its product string, else nothing",
          "[diagnostics][report]") {
    CHECK(dish::diagnostics::reportControllerOf(std::string("sony-dualsense"), "Wireless") ==
          std::optional<std::string>("sony-dualsense"));
    CHECK(dish::diagnostics::reportControllerOf(std::nullopt, "Wireless Controller") ==
          std::optional<std::string>("Wireless Controller"));
    CHECK_FALSE(dish::diagnostics::reportControllerOf(std::nullopt, "").has_value());
}

// ── Host snapshots ──────────────────────────────────────────────────────────

TEST_CASE("host snapshot: what each answer to the diagnostics route means",
          "[diagnostics][snapshot]") {
    using dish::diagnostics::snapshotErrorOf;
    CHECK(snapshotErrorOf(SnapshotReply{200, false, true}) == SnapshotError::None);
    // Satellite 2.3.0 answers 404 for a connection it does not know or that is another device's;
    // an older Satellite is told by its version and never asked.
    CHECK(snapshotErrorOf(SnapshotReply{404, false, false}) == SnapshotError::UnknownConnection);
    CHECK(snapshotErrorOf(SnapshotReply{401, false, false}) == SnapshotError::Unauthorized);
    CHECK(snapshotErrorOf(SnapshotReply{403, false, false}) == SnapshotError::Unauthorized);
    CHECK(snapshotErrorOf(SnapshotReply{0, true, false}) == SnapshotError::Timeout);
    CHECK(snapshotErrorOf(SnapshotReply{0, false, false}) == SnapshotError::Offline);
    SECTION("an answer that is not a snapshot is no answer") {
        CHECK(snapshotErrorOf(SnapshotReply{200, false, false}) == SnapshotError::Offline);
        CHECK(snapshotErrorOf(SnapshotReply{503, false, false}) == SnapshotError::Offline);
    }
}

TEST_CASE("host snapshot: the error tokens are the contract's, and no error writes none",
          "[diagnostics][snapshot]") {
    using dish::diagnostics::snapshotErrorToken;
    CHECK_FALSE(snapshotErrorToken(SnapshotError::None).has_value());
    CHECK(snapshotErrorToken(SnapshotError::Offline) == std::optional<std::string_view>("offline"));
    CHECK(snapshotErrorToken(SnapshotError::Unauthorized) ==
          std::optional<std::string_view>("unauthorized"));
    CHECK(snapshotErrorToken(SnapshotError::OlderSatellite) ==
          std::optional<std::string_view>("olderSatellite"));
    CHECK(snapshotErrorToken(SnapshotError::Timeout) == std::optional<std::string_view>("timeout"));
    CHECK(snapshotErrorToken(SnapshotError::UnknownConnection) ==
          std::optional<std::string_view>("unknownConnection"));
    CHECK(dish::diagnostics::kSnapshotTimeoutMs == 2000);
}

TEST_CASE("host snapshot: a Satellite below 2.3.0 has no route, told by the version it reported",
          "[diagnostics][snapshot]") {
    using dish::diagnostics::isOlderSatellite;
    CHECK(isOlderSatellite("2.2.9"));
    CHECK(isOlderSatellite("2.2"));
    CHECK(isOlderSatellite("1.9.0"));
    CHECK_FALSE(isOlderSatellite("2.3.0"));
    CHECK_FALSE(isOlderSatellite("2.3.0-rc.1"));
    CHECK_FALSE(isOlderSatellite("2.10.0"));
    CHECK_FALSE(isOlderSatellite("3.0.0"));
    SECTION("a version it cannot read is not taken for an old one, so the route is asked") {
        CHECK_FALSE(isOlderSatellite(""));
        CHECK_FALSE(isOlderSatellite("dev"));
    }
}

TEST_CASE("host snapshot collector: every live link is asked once, and the answers wait for all",
          "[diagnostics][snapshot]") {
    HeldGateway gateway;
    CollectedSnapshots collected;
    HostSnapshotCollector collector(gateway);
    collector.collect({"sat-a", "sat-b"}, [&collected](HostSnapshots snapshots) {
        collected.take(std::move(snapshots));
    });
    CHECK(gateway.asked == std::vector<std::string>{"sat-a", "sat-b"});

    gateway.answer("sat-a", SnapshotFetchReply{200, false, connectionSnapshot()});
    CHECK(collected.calls == 0);
    gateway.answer("sat-b", SnapshotFetchReply{401, false, std::nullopt});
    REQUIRE(collected.calls == 1);

    const auto& first = collected.last.at("sat-a");
    CHECK(first.error == SnapshotError::None);
    REQUIRE(first.snapshot.has_value());
    CHECK(first.snapshot->find("format")->stringValue() == "satellite-connection-diagnostics.v1");
    const auto& second = collected.last.at("sat-b");
    CHECK(second.error == SnapshotError::Unauthorized);
    CHECK_FALSE(second.snapshot.has_value());
}

TEST_CASE("host snapshot collector: with no live link the answer is at once and empty",
          "[diagnostics][snapshot]") {
    HeldGateway gateway;
    CollectedSnapshots collected;
    HostSnapshotCollector collector(gateway);
    collector.collect(
        {}, [&collected](HostSnapshots snapshots) { collected.take(std::move(snapshots)); });
    CHECK(collected.calls == 1);
    CHECK(collected.last.empty());
    CHECK(gateway.asked.empty());
}

TEST_CASE("host snapshot collector: a body that is no snapshot is dropped, never embedded",
          "[diagnostics][snapshot]") {
    HeldGateway gateway;
    CollectedSnapshots collected;
    HostSnapshotCollector collector(gateway);
    collector.collect(
        {"sat-a"}, [&collected](HostSnapshots snapshots) { collected.take(std::move(snapshots)); });
    gateway.answer("sat-a", SnapshotFetchReply{200, false, std::nullopt});
    REQUIRE(collected.calls == 1);
    CHECK(collected.last.at("sat-a").error == SnapshotError::Offline);
    CHECK_FALSE(collected.last.at("sat-a").snapshot.has_value());
}

TEST_CASE("host snapshot collector: an answer after the collector is gone reaches nobody",
          "[diagnostics][snapshot]") {
    HeldGateway gateway;
    CollectedSnapshots collected;
    {
        HostSnapshotCollector collector(gateway);
        collector.collect({"sat-a"}, [&collected](HostSnapshots snapshots) {
            collected.take(std::move(snapshots));
        });
    }
    gateway.answer("sat-a", SnapshotFetchReply{0, true, std::nullopt});
    CHECK(collected.calls == 0);
}

// ── The session's own view ──────────────────────────────────────────────────

TEST_CASE("wire counters: each message kind counts under its own name", "[diagnostics][wire]") {
    WireCounters counters;
    counters.countSent(dish::proto::kMsgInput);
    counters.countSent(dish::proto::kMsgInput);
    counters.countSent(dish::proto::kMsgTouchpad);
    counters.countSent(dish::proto::kMsgMicAudio);
    counters.countReceived(dish::proto::kMsgRumble);
    counters.countReceived(dish::proto::kMsgHapticAudio);
    counters.countReceived(dish::proto::kMsgSessionClose);
    const auto counts = counters.counts();
    CHECK(counts.sent[static_cast<std::size_t>(SentKind::Input)] == 2);
    CHECK(counts.sent[static_cast<std::size_t>(SentKind::Pointer)] == 1);
    CHECK(counts.sent[static_cast<std::size_t>(SentKind::Mic)] == 1);
    CHECK(counts.sent[static_cast<std::size_t>(SentKind::Motion)] == 0);
    CHECK(counts.received[static_cast<std::size_t>(ReceivedKind::Rumble)] == 1);
    CHECK(counts.received[static_cast<std::size_t>(ReceivedKind::Haptic)] == 1);
    CHECK(counts.received[static_cast<std::size_t>(ReceivedKind::SessionClose)] == 1);
}

TEST_CASE("wire counters: the heartbeat and kinds no table names are not counted",
          "[diagnostics][wire]") {
    WireCounters counters;
    counters.countSent(dish::proto::kMsgHeartbeat);
    counters.countReceived(dish::proto::kMsgHeartbeatAck);
    counters.countReceived(0x7FFF);
    CHECK(counters.counts() == dish::diagnostics::WireCounts{});
}

TEST_CASE("wire counters: the names are the contract's, in its order", "[diagnostics][wire]") {
    std::vector<std::string> sent;
    for (const auto& row : dish::diagnostics::kSentKinds) { sent.emplace_back(row.token); }
    CHECK(sent == std::vector<std::string>{"input", "motion", "battery", "pointer", "mic"});
    std::vector<std::string> received;
    for (const auto& row : dish::diagnostics::kReceivedKinds) { received.emplace_back(row.token); }
    CHECK(received == std::vector<std::string>{"rumble", "lightbar", "triggerEffects", "playerLeds",
                                               "micLed", "speaker", "haptic", "sessionClose"});
}

// ── Each pad's lanes ────────────────────────────────────────────────────────

TEST_CASE("pad lanes: each lane keeps its count, its last value and when", "[diagnostics][lanes]") {
    PadOutputCounters counters;
    counters.record(OutputLane::Rumble, dish::diagnostics::packRumble(32768, 0), 1000);
    counters.record(OutputLane::Rumble, dish::diagnostics::packRumble(0, 12), 2000);
    counters.record(OutputLane::Lightbar, dish::diagnostics::packRgb(0, 0, 255), 3000);
    const auto rumble = counters.sample(OutputLane::Rumble);
    CHECK(rumble.count == 2);
    CHECK(rumble.lastAtMs == 2000);
    CHECK(dish::diagnostics::rumbleStrongOf(rumble.last) == 0);
    CHECK(dish::diagnostics::rumbleWeakOf(rumble.last) == 12);
    const auto lightbar = counters.sample(OutputLane::Lightbar);
    CHECK(lightbar.count == 1);
    CHECK(dish::diagnostics::rgbRedOf(lightbar.last) == 0);
    CHECK(dish::diagnostics::rgbBlueOf(lightbar.last) == 255);
    CHECK(counters.sample(OutputLane::MicLed).count == 0);
    CHECK(counters.sample(OutputLane::MicLed).lastAtMs == 0);
}

TEST_CASE("pad lanes: the packed values survive their extremes", "[diagnostics][lanes]") {
    constexpr std::uint16_t kMax = std::numeric_limits<std::uint16_t>::max();
    const std::uint64_t packed = dish::diagnostics::packRumble(kMax, 1);
    CHECK(dish::diagnostics::rumbleStrongOf(packed) == kMax);
    CHECK(dish::diagnostics::rumbleWeakOf(packed) == 1);
    const std::uint64_t rgb = dish::diagnostics::packRgb(255, 128, 1);
    CHECK(dish::diagnostics::rgbRedOf(rgb) == 255);
    CHECK(dish::diagnostics::rgbGreenOf(rgb) == 128);
    CHECK(dish::diagnostics::rgbBlueOf(rgb) == 1);
}

TEST_CASE("pad lanes: where a lane's output goes, in the contract's words",
          "[diagnostics][lanes]") {
    using dish::reducer::FeedbackTarget;
    CHECK(dish::diagnostics::feedbackTargetToken(FeedbackTarget::DirectUsb) == "directUsb");
    CHECK(dish::diagnostics::feedbackTargetToken(FeedbackTarget::Standard) == "framework");
    CHECK(dish::diagnostics::feedbackTargetToken(FeedbackTarget::None) == "none");
}

TEST_CASE("pad lanes: a voice is refused, open or closed, in that order of precedence",
          "[diagnostics][lanes]") {
    using dish::diagnostics::voiceStateOf;
    CHECK(voiceStateOf(false, true) == VoiceState::Refused);
    CHECK(voiceStateOf(true, true) == VoiceState::Refused);
    CHECK(voiceStateOf(true, false) == VoiceState::Open);
    CHECK(voiceStateOf(false, false) == VoiceState::Closed);
    CHECK(dish::diagnostics::voiceStateToken(VoiceState::Open) == "open");
    CHECK(dish::diagnostics::voiceStateToken(VoiceState::Closed) == "closed");
    CHECK(dish::diagnostics::voiceStateToken(VoiceState::Refused) == "refused");
}

// ── Device facts ────────────────────────────────────────────────────────────

TEST_CASE("device facts: a laptop on battery with the saver on", "[diagnostics][device]") {
    // GetSystemPowerStatus: off the mains, battery flag "low", 18 %, battery saver on.
    const auto facts = dish::diagnostics::powerFactsOf(PowerStatusBytes{0, 2, 18, 1});
    REQUIRE(facts.battery.has_value());
    CHECK(facts.battery->level == std::optional<int>(18));
    CHECK_FALSE(facts.battery->charging);
    CHECK(facts.powerSave == std::optional<bool>(true));
}

TEST_CASE("device facts: charging is the battery flag's own bit", "[diagnostics][device]") {
    const auto facts = dish::diagnostics::powerFactsOf(PowerStatusBytes{1, 8, 64, 0});
    REQUIRE(facts.battery.has_value());
    CHECK(facts.battery->charging);
    CHECK(facts.powerSave == std::optional<bool>(false));
}

TEST_CASE("device facts: a desktop with no battery reports none, and unknowns stay unknown",
          "[diagnostics][device]") {
    CHECK_FALSE(
        dish::diagnostics::powerFactsOf(PowerStatusBytes{1, 128, 255, 0}).battery.has_value());
    CHECK_FALSE(
        dish::diagnostics::powerFactsOf(PowerStatusBytes{255, 255, 255, 0}).battery.has_value());
    const auto unknownLevel = dish::diagnostics::powerFactsOf(PowerStatusBytes{0, 1, 255, 0});
    REQUIRE(unknownLevel.battery.has_value());
    CHECK_FALSE(unknownLevel.battery->level.has_value());
}

TEST_CASE("device facts: the power-status call that failed says nothing", "[diagnostics][device]") {
    const auto facts = dish::diagnostics::powerFactsOf(std::nullopt);
    CHECK_FALSE(facts.battery.has_value());
    CHECK_FALSE(facts.powerSave.has_value());
}

// ── Bluetooth ───────────────────────────────────────────────────────────────

TEST_CASE("bluetooth: the link a HID device path names, never more of the path",
          "[diagnostics][bluetooth]") {
    using dish::input::BluetoothLink;
    using dish::input::bluetoothLinkOfHidPath;
    CHECK(bluetoothLinkOfHidPath(
              "\\\\?\\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002054c_"
              "PID&09cc#9&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}") ==
          BluetoothLink::Classic);
    CHECK(bluetoothLinkOfHidPath("\\\\?\\HID#{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02045e_"
                                 "PID&0b13_REV&0509_c8d3ffa1b2c3#a&1&0000#{4d1e55b2}") ==
          BluetoothLink::LowEnergy);
    CHECK(bluetoothLinkOfHidPath("\\\\?\\HID#BTHLEDEVICE#x") == BluetoothLink::LowEnergy);
    CHECK(bluetoothLinkOfHidPath("\\\\?\\BTHENUM#x") == BluetoothLink::Classic);
    CHECK(bluetoothLinkOfHidPath("\\\\?\\HID#VID_054C&PID_0CE6&MI_03#8&1&0&0000") ==
          BluetoothLink::None);
    CHECK(bluetoothLinkOfHidPath("") == BluetoothLink::None);
}

// ── The JSON writer's numbers ───────────────────────────────────────────────

TEST_CASE("json: a fraction is written to three places, whatever the C locale says",
          "[diagnostics][json]") {
    CHECK(Json(2.1).dump() == "2.1\n");
    CHECK(Json(4.8125).dump() == "4.813\n");
    CHECK(Json(3.0).dump() == "3\n");
    CHECK(Json(-0.25).dump() == "-0.25\n");
    CHECK(Json(0.0004).dump() == "0\n");
    CHECK(Json(std::numeric_limits<double>::quiet_NaN()).dump() == "null\n");
    CHECK(Json(std::numeric_limits<double>::infinity()).dump() == "null\n");
    CHECK(Json(2.5).doubleValue() == 2.5);
}

TEST_CASE("json: an object's keys read back in their order", "[diagnostics][json]") {
    Json object = Json::object();
    object.set("b", 1);
    object.set("a", 2);
    CHECK(object.keys() == std::vector<std::string>{"b", "a"});
}
