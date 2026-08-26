// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The twenty-one states the Moonlight session section can render, one assertion
// per state against the token QML localizes from, plus the two questions the
// surrounding chrome asks of a state: does it block the bind, and is it a
// problem. Every state is reachable and no two inputs land on the same one by
// accident, which is what makes the section's copy table checkable.

#include "core/moonlight/MoonlightSessionUi.h"
#include "qml/RenderTokens.h"

#include <QSet>

#include <catch2/catch_test_macros.hpp>

#include <utility>

using namespace dish::moonlight;
using dish::qml::tokens::moonlightSessionToken;
using dish::qml::tokens::moonlightTrustToken;

namespace {

// A host that answered, is paired, and has nothing in flight. Every case below
// is this plus the one thing that makes it that state.
SessionUiInputs paired() {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.hostPairStatus = true;
    in.serverCertStored = true;
    return in;
}

QString tokenOf(const SessionUiInputs& in) { return moonlightSessionToken(resolveSessionUi(in)); }

} // namespace

TEST_CASE("M1 checking: a probe is in flight and nothing is cached", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeInFlight = true;
    REQUIRE(resolveSessionUi(in) == SessionUiState::Checking);
    REQUIRE(tokenOf(in) == QStringLiteral("checking"));
}

TEST_CASE("M2 not paired: the host answered and has never met us", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.hostPairStatus = false;
    in.serverCertStored = false;
    REQUIRE(resolveSessionUi(in) == SessionUiState::NotPaired);
    REQUIRE(tokenOf(in) == QStringLiteral("notPaired"));
}

TEST_CASE("M3 pairing outranks not paired: the PIN is on screen", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.pairingActive = true;
    REQUIRE(resolveSessionUi(in) == SessionUiState::PairingPin);
    REQUIRE(tokenOf(in) == QStringLiteral("pairingPin"));
}

TEST_CASE("M4 pairing refused", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.pairingRefused = true;
    REQUIRE(resolveSessionUi(in) == SessionUiState::PairRefused);
    REQUIRE(tokenOf(in) == QStringLiteral("pairRefused"));
}

TEST_CASE("M5 and M6: a silent host reads differently once it is remembered",
          "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeTimedOut = true;
    in.serverCertStored = false;
    REQUIRE(resolveSessionUi(in) == SessionUiState::Unreachable);
    REQUIRE(tokenOf(in) == QStringLiteral("unreachable"));

    in.serverCertStored = true;
    REQUIRE(resolveSessionUi(in) == SessionUiState::RememberedOffline);
    REQUIRE(tokenOf(in) == QStringLiteral("rememberedOffline"));
}

TEST_CASE("M7 trust lost, from either side of the evidence", "[moonlight][sessionui]") {
    SessionUiInputs answered;
    answered.probeAnswered = true;
    answered.hostPairStatus = false;
    answered.serverCertStored = true;
    REQUIRE(resolveSessionUi(answered) == SessionUiState::TrustLost);
    REQUIRE(tokenOf(answered) == QStringLiteral("trustLost"));

    // A mutual-TLS 401 says the same thing whatever the plaintext probe claimed.
    SessionUiInputs unauthorized = paired();
    unauthorized.unauthorized = true;
    REQUIRE(resolveSessionUi(unauthorized) == SessionUiState::TrustLost);
}

TEST_CASE("M8 host replaced beats every other trust answer", "[moonlight][sessionui]") {
    SessionUiInputs in = paired();
    in.uniqueIdChanged = true;
    REQUIRE(resolveSessionUi(in) == SessionUiState::HostReplaced);
    REQUIRE(tokenOf(in) == QStringLiteral("hostReplaced"));
}

TEST_CASE("M9 through M12: the app list, in flight and in every way it can end",
          "[moonlight][sessionui]") {
    SessionUiInputs loading = paired();
    loading.appsInFlight = true;
    REQUIRE(resolveSessionUi(loading) == SessionUiState::AppsLoading);
    REQUIRE(tokenOf(loading) == QStringLiteral("appsLoading"));

    SessionUiInputs ready = paired();
    ready.appsFetched = true;
    ready.appCount = 2;
    REQUIRE(resolveSessionUi(ready) == SessionUiState::NewSession);
    REQUIRE(tokenOf(ready) == QStringLiteral("newSession"));

    SessionUiInputs empty = paired();
    empty.appsFetched = true;
    empty.appCount = 0;
    REQUIRE(resolveSessionUi(empty) == SessionUiState::NoApps);
    REQUIRE(tokenOf(empty) == QStringLiteral("noApps"));

    SessionUiInputs failed = paired();
    failed.appsFailed = true;
    REQUIRE(resolveSessionUi(failed) == SessionUiState::AppsUnreadable);
    REQUIRE(tokenOf(failed) == QStringLiteral("appsUnreadable"));
}

TEST_CASE("M13 joining our session, and it shows no picker", "[moonlight][sessionui]") {
    SessionUiInputs in = paired();
    in.hostSessionActive = true;
    in.appsFetched = true;
    in.appCount = 3;
    // The app is settled by whoever created the session, so a list that HAS
    // arrived still does not put the user in front of a choice.
    REQUIRE(resolveSessionUi(in) == SessionUiState::JoiningSession);
    REQUIRE(tokenOf(in) == QStringLiteral("joiningSession"));
}

TEST_CASE("M14 host full outranks joining: there is no fifth controller",
          "[moonlight][sessionui]") {
    SessionUiInputs in = paired();
    in.boundControllers = static_cast<int>(kMaxPads);
    in.hostSessionActive = true;
    REQUIRE(resolveSessionUi(in) == SessionUiState::HostFull);
    REQUIRE(tokenOf(in) == QStringLiteral("hostFull"));

    in.boundControllers = static_cast<int>(kMaxPads) - 1;
    REQUIRE(resolveSessionUi(in) == SessionUiState::JoiningSession);
}

TEST_CASE("M15 through M18: a refusal outranks the app list it arrived beside",
          "[moonlight][sessionui]") {
    const std::pair<SessionOutcome, const char*> cases[] = {
        {SessionOutcome::BusyOther, "busyOther"},
        {SessionOutcome::RejoinRefused, "rejoinRefused"},
        {SessionOutcome::Refused, "refused"},
        {SessionOutcome::SetupFailed, "setupFailed"},
    };
    for (const auto& [outcome, token] : cases) {
        SessionUiInputs in = paired();
        // Everything M10 needs is true as well; the refusal is what matters.
        in.appsFetched = true;
        in.appCount = 4;
        in.outcome = outcome;
        REQUIRE(tokenOf(in) == QString::fromLatin1(token));
    }
}

TEST_CASE("M19 through M21: live, dropped and ended are three different things",
          "[moonlight][sessionui]") {
    SessionUiInputs live = paired();
    live.outcome = SessionOutcome::Live;
    REQUIRE(resolveSessionUi(live) == SessionUiState::Live);
    REQUIRE(tokenOf(live) == QStringLiteral("live"));

    SessionUiInputs dropped = paired();
    dropped.outcome = SessionOutcome::Dropped;
    REQUIRE(resolveSessionUi(dropped) == SessionUiState::Dropped);
    REQUIRE(tokenOf(dropped) == QStringLiteral("dropped"));

    SessionUiInputs ended = paired();
    ended.outcome = SessionOutcome::EndedByHost;
    REQUIRE(resolveSessionUi(ended) == SessionUiState::EndedByHost);
    REQUIRE(tokenOf(ended) == QStringLiteral("endedByHost"));
}

TEST_CASE("Every state has a token and no two share one", "[moonlight][sessionui]") {
    const SessionUiState all[] = {
        SessionUiState::Checking,       SessionUiState::NotPaired,
        SessionUiState::PairingPin,     SessionUiState::PairRefused,
        SessionUiState::Unreachable,    SessionUiState::RememberedOffline,
        SessionUiState::TrustLost,      SessionUiState::HostReplaced,
        SessionUiState::AppsLoading,    SessionUiState::NewSession,
        SessionUiState::NoApps,         SessionUiState::AppsUnreadable,
        SessionUiState::JoiningSession, SessionUiState::HostFull,
        SessionUiState::BusyOther,      SessionUiState::RejoinRefused,
        SessionUiState::Refused,        SessionUiState::SetupFailed,
        SessionUiState::Live,           SessionUiState::Dropped,
        SessionUiState::EndedByHost,
    };
    QSet<QString> seen;
    for (const auto state : all) {
        const QString token = moonlightSessionToken(state);
        REQUIRE_FALSE(token.isEmpty());
        REQUIRE_FALSE(seen.contains(token));
        seen.insert(token);
    }
    REQUIRE(seen.size() == 21);
}

TEST_CASE("Only a full host blocks the bind", "[moonlight][sessionui]") {
    const SessionUiState all[] = {
        SessionUiState::Checking,       SessionUiState::NotPaired,
        SessionUiState::PairingPin,     SessionUiState::PairRefused,
        SessionUiState::Unreachable,    SessionUiState::RememberedOffline,
        SessionUiState::TrustLost,      SessionUiState::HostReplaced,
        SessionUiState::AppsLoading,    SessionUiState::NewSession,
        SessionUiState::NoApps,         SessionUiState::AppsUnreadable,
        SessionUiState::JoiningSession, SessionUiState::HostFull,
        SessionUiState::BusyOther,      SessionUiState::RejoinRefused,
        SessionUiState::Refused,        SessionUiState::SetupFailed,
        SessionUiState::Live,           SessionUiState::Dropped,
        SessionUiState::EndedByHost,
    };
    for (const auto state : all) {
        const bool blocked = sessionUiBlocksApply(state);
        REQUIRE(blocked == (state == SessionUiState::HostFull));
    }
}

TEST_CASE("Amber is the problem colour and never the working one", "[moonlight][sessionui]") {
    // In progress is not a problem, and neither is a host nobody has paired yet:
    // its next step is simply the next step.
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::Checking));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::NotPaired));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::PairingPin));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::AppsLoading));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::NewSession));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::NoApps));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::AppsUnreadable));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::JoiningSession));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::Live));

    REQUIRE(sessionUiIsProblem(SessionUiState::PairRefused));
    REQUIRE(sessionUiIsProblem(SessionUiState::Unreachable));
    REQUIRE(sessionUiIsProblem(SessionUiState::RememberedOffline));
    REQUIRE(sessionUiIsProblem(SessionUiState::TrustLost));
    REQUIRE(sessionUiIsProblem(SessionUiState::HostReplaced));
    REQUIRE(sessionUiIsProblem(SessionUiState::HostFull));
    REQUIRE(sessionUiIsProblem(SessionUiState::BusyOther));
    REQUIRE(sessionUiIsProblem(SessionUiState::RejoinRefused));
    REQUIRE(sessionUiIsProblem(SessionUiState::Refused));
    REQUIRE(sessionUiIsProblem(SessionUiState::SetupFailed));
    REQUIRE(sessionUiIsProblem(SessionUiState::Dropped));
    REQUIRE(sessionUiIsProblem(SessionUiState::EndedByHost));
}

TEST_CASE("Only the two refusals and a live session offer to close the app",
          "[moonlight][sessionui]") {
    REQUIRE(sessionUiOffersQuit(SessionUiState::BusyOther));
    REQUIRE(sessionUiOffersQuit(SessionUiState::RejoinRefused));
    REQUIRE(sessionUiOffersQuit(SessionUiState::Live));
    REQUIRE_FALSE(sessionUiOffersQuit(SessionUiState::NewSession));
    REQUIRE_FALSE(sessionUiOffersQuit(SessionUiState::Dropped));
    REQUIRE_FALSE(sessionUiOffersQuit(SessionUiState::NotPaired));
}

TEST_CASE("Trust is three words, and a remembered host that is silent keeps its pairing",
          "[moonlight][sessionui]") {
    SessionUiInputs answeredPaired;
    answeredPaired.probeAnswered = true;
    answeredPaired.hostPairStatus = true;
    REQUIRE(trustFor(answeredPaired) == TrustState::Paired);
    REQUIRE(moonlightTrustToken(trustFor(answeredPaired)) == QStringLiteral("paired"));

    SessionUiInputs silent;
    silent.serverCertStored = true;
    REQUIRE(trustFor(silent) == TrustState::Remembered);
    REQUIRE(moonlightTrustToken(trustFor(silent)) == QStringLiteral("remembered"));

    SessionUiInputs stranger;
    stranger.probeAnswered = true;
    REQUIRE(trustFor(stranger) == TrustState::NotPaired);
    REQUIRE(moonlightTrustToken(trustFor(stranger)) == QStringLiteral("notPaired"));

    // A host that came back with a new identity is a stranger however much is
    // remembered about the old one.
    SessionUiInputs replaced;
    replaced.probeAnswered = true;
    replaced.hostPairStatus = true;
    replaced.serverCertStored = true;
    replaced.uniqueIdChanged = true;
    REQUIRE(trustFor(replaced) == TrustState::NotPaired);
}

TEST_CASE("The wire lifecycle maps onto exactly one outcome each", "[moonlight][sessionui]") {
    REQUIRE(sessionOutcomeFor({SessionPhase::Streaming, SessionFailure::None}) ==
            SessionOutcome::Live);
    REQUIRE(sessionOutcomeFor({SessionPhase::Faltering, SessionFailure::None}) ==
            SessionOutcome::Live);
    REQUIRE(sessionOutcomeFor({SessionPhase::Launching, SessionFailure::None}) ==
            SessionOutcome::None);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::AppAlreadyRunning}) ==
            SessionOutcome::BusyOther);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::ResumeRejected}) ==
            SessionOutcome::RejoinRefused);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::LaunchRejected}) ==
            SessionOutcome::Refused);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::RtspFailed}) ==
            SessionOutcome::SetupFailed);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::ControlFailed}) ==
            SessionOutcome::SetupFailed);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::LinkDropped}) ==
            SessionOutcome::Dropped);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::ServerTerminated}) ==
            SessionOutcome::EndedByHost);
    // A pairing that failed is answered by the trust states, not by an outcome.
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::PairRejected}) ==
            SessionOutcome::None);
    REQUIRE(sessionOutcomeFor({SessionPhase::Failed, SessionFailure::Unreachable}) ==
            SessionOutcome::None);
}
