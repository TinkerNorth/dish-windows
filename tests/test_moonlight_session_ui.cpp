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
using dish::qml::tokens::moonlightBindOutcomeToken;
using dish::qml::tokens::moonlightSessionToken;
using dish::qml::tokens::moonlightTrustToken;

namespace {

// A host that answered, is paired, and has nothing in flight. Every case below
// is this plus the one thing that makes it that state.
SessionUiInputs paired() {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.hostPairStatus = true;
    in.pairingHeld = true;
    return in;
}

QString tokenOf(const SessionUiInputs& in) { return moonlightSessionToken(sessionUiState(in)); }

} // namespace

TEST_CASE("M1 checking: a probe is in flight and nothing is cached", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeInFlight = true;
    REQUIRE(sessionUiState(in) == SessionUiState::Checking);
    REQUIRE(tokenOf(in) == QStringLiteral("checking"));
}

TEST_CASE("M2 not paired: the host answered and has never met us", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.hostPairStatus = false;
    in.pairingHeld = false;
    REQUIRE(sessionUiState(in) == SessionUiState::NotPaired);
    REQUIRE(tokenOf(in) == QStringLiteral("notPaired"));
}

TEST_CASE("M3 pairing outranks not paired: the PIN is on screen", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.pairingActive = true;
    REQUIRE(sessionUiState(in) == SessionUiState::PairingPin);
    REQUIRE(tokenOf(in) == QStringLiteral("pairingPin"));
}

TEST_CASE("M4 pairing refused", "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeAnswered = true;
    in.pairingRefused = true;
    REQUIRE(sessionUiState(in) == SessionUiState::PairingRefused);
    REQUIRE(tokenOf(in) == QStringLiteral("pairingRefused"));
}

TEST_CASE("M5 and M6: a silent host reads differently once it is remembered",
          "[moonlight][sessionui]") {
    SessionUiInputs in;
    in.probeTimedOut = true;
    in.pairingHeld = false;
    REQUIRE(sessionUiState(in) == SessionUiState::Unreachable);
    REQUIRE(tokenOf(in) == QStringLiteral("unreachable"));

    in.pairingHeld = true;
    REQUIRE(sessionUiState(in) == SessionUiState::Remembered);
    REQUIRE(tokenOf(in) == QStringLiteral("remembered"));
}

TEST_CASE("M7 trust lost, from either side of the evidence", "[moonlight][sessionui]") {
    SessionUiInputs answered;
    answered.probeAnswered = true;
    answered.hostPairStatus = false;
    answered.pairingHeld = true;
    REQUIRE(sessionUiState(answered) == SessionUiState::TrustLost);
    REQUIRE(tokenOf(answered) == QStringLiteral("trustLost"));

    // A mutual-TLS 401 says the same thing whatever the plaintext probe claimed.
    SessionUiInputs unauthorized = paired();
    unauthorized.unauthorized = true;
    REQUIRE(sessionUiState(unauthorized) == SessionUiState::TrustLost);

    // But only where there was trust to lose: a host we have never paired with
    // refuses in exactly the same way and is simply not paired.
    SessionUiInputs stranger;
    stranger.probeAnswered = true;
    stranger.unauthorized = true;
    stranger.pairingHeld = false;
    REQUIRE(sessionUiState(stranger) == SessionUiState::NotPaired);
}

TEST_CASE("A forgotten host the host still answers for is NOT paired, and can pair again",
          "[moonlight][sessionui]") {
    // THE REPORTED SYMPTOM. The client identity outlives a forget, and a host
    // reports PairStatus against the uniqueid on the request, so a box that still
    // holds this install answers 1 to a client that has thrown its half away.
    // Reading that word alone rendered the host Paired, and Paired is the one
    // word that hides the Pair button: the user was left looking at a host that
    // claimed to be paired, with no certificate to open a channel with and no
    // control anywhere that could repair it.
    SessionUiInputs forgotten;
    forgotten.probeAnswered = true;
    forgotten.hostPairStatus = true; // the host's half, still on file there
    forgotten.pairingHeld = false;   // ours, deleted by the forget

    REQUIRE(sessionUiState(forgotten) == SessionUiState::NotPaired);
    REQUIRE(tokenOf(forgotten) == QStringLiteral("notPaired"));
    // NOT TrustLost: nothing was lost, we simply do not hold the anchor, and the
    // way back in is the same PIN a stranger needs.
    REQUIRE(trustFor(forgotten) == TrustState::NotPaired);
    REQUIRE(moonlightTrustToken(trustFor(forgotten)) == QStringLiteral("notPaired"));

    // Both halves is what Paired means, and only both.
    SessionUiInputs whole = forgotten;
    whole.pairingHeld = true;
    REQUIRE(trustFor(whole) == TrustState::Paired);
    REQUIRE(sessionUiState(whole) != SessionUiState::NotPaired);

    // The host's half alone is not enough in either direction: a certificate we
    // hold for a host that says it does not know us is trust LOST, which is a
    // different sentence and a different thing to have happened.
    SessionUiInputs lost;
    lost.probeAnswered = true;
    lost.hostPairStatus = false;
    lost.pairingHeld = true;
    REQUIRE(sessionUiState(lost) == SessionUiState::TrustLost);
    REQUIRE(trustFor(lost) == TrustState::NotPaired);

    // A host that did not answer this visit still falls back on the memory.
    SessionUiInputs silent;
    silent.probeTimedOut = true;
    silent.pairingHeld = true;
    REQUIRE(trustFor(silent) == TrustState::Remembered);
    REQUIRE(sessionUiState(silent) == SessionUiState::Remembered);
}

TEST_CASE("A host that refused this client is never promised back", "[moonlight][sessionui]") {
    // ORDERING, not the rule. The rule already said a rejection means unpaired,
    // but it was judged AFTER the block that returns on the strength of nobody
    // having answered yet, so a 401 arriving while the plaintext probe was still
    // out or had timed out never reached it. The section rendered Remembered,
    // whose copy promises a session when the host is back, about a host that had
    // just refused this client, while the row beside it said Not paired.
    //
    // The split is real: the 401 comes from the mutual-TLS call on 47984 and the
    // probe from plaintext on 47989, so one can answer while the other does not.
    SessionUiInputs rejectedWhileSilent;
    rejectedWhileSilent.unauthorized = true;
    rejectedWhileSilent.pairingHeld = true;
    rejectedWhileSilent.probeAnswered = false;
    rejectedWhileSilent.probeTimedOut = true;
    REQUIRE(sessionUiState(rejectedWhileSilent) == SessionUiState::TrustLost);
    REQUIRE(tokenOf(rejectedWhileSilent) == QStringLiteral("trustLost"));
    REQUIRE(trustFor(rejectedWhileSilent) == TrustState::NotPaired);

    // And with the probe still in flight, where it drew a spinner instead.
    SessionUiInputs rejectedWhileChecking = rejectedWhileSilent;
    rejectedWhileChecking.probeTimedOut = false;
    REQUIRE(sessionUiState(rejectedWhileChecking) == SessionUiState::TrustLost);

    // With nothing held it is NotPaired rather than TrustLost, because nothing
    // was lost, but it is never Unreachable: the host answered, with a refusal.
    SessionUiInputs strangerRejected;
    strangerRejected.unauthorized = true;
    strangerRejected.probeTimedOut = true;
    REQUIRE(sessionUiState(strangerRejected) == SessionUiState::NotPaired);
    REQUIRE(trustFor(strangerRejected) == TrustState::NotPaired);

    // A host carrying its four pads still outranks all of it: that is the one
    // state that blocks Apply, and no trust answer may enable a bind the
    // manager is going to refuse.
    SessionUiInputs full = rejectedWhileSilent;
    full.boundControllers = static_cast<int>(kMaxPads);
    REQUIRE(sessionUiState(full) == SessionUiState::HostFull);
}

TEST_CASE("The row and the section never disagree about trust", "[moonlight][sessionui]") {
    // The cross product, because the user's dead end was two surfaces answering
    // one question differently and no single case would have caught it. Every
    // combination of the seven inputs that bear on trust, against both readers.
    int checked = 0;
    for (int mask = 0; mask < 128; ++mask) {
        SessionUiInputs in;
        in.probeAnswered = (mask & 1) != 0;
        in.probeTimedOut = (mask & 2) != 0;
        in.hostPairStatus = (mask & 4) != 0;
        in.pairingHeld = (mask & 8) != 0;
        in.unauthorized = (mask & 16) != 0;
        in.uniqueIdChanged = (mask & 32) != 0;
        in.serverCertChanged = (mask & 64) != 0;

        const SessionUiState state = sessionUiState(in);
        const TrustState trust = trustFor(in);
        const bool sectionUnpaired = state == SessionUiState::NotPaired ||
                                     state == SessionUiState::TrustLost ||
                                     state == SessionUiState::HostReplaced;

        // CHECK, not REQUIRE: a REQUIRE stops the case at the first bad
        // combination, and when this fires the useful thing is every combination
        // that disagrees, not the lowest-numbered one.
        //
        // A section that says there is no usable trust must not sit under a row
        // claiming there is, and Paired is the word that hides the Pair button.
        if (sectionUnpaired) { CHECK(trust == TrustState::NotPaired); }
        if (trust == TrustState::Paired) { CHECK_FALSE(sectionUnpaired); }
        // Remembered promises a session when the host returns, which is a lie
        // over a host that has refused us or is not the one we paired with.
        if (trust == TrustState::Remembered) {
            CHECK_FALSE(in.unauthorized);
            CHECK(state != SessionUiState::NotPaired);
        }
        // A rejection leaves exactly one family of answers, whatever else is
        // known about the host.
        if (in.unauthorized) { CHECK(sectionUnpaired); }
        // AND THE DEAD END ITSELF, stated as an invariant: a row that says Not
        // paired over a host that HAS answered must sit above a section offering
        // the way back, never a spinner or a promise. The user was left with the
        // row saying one thing and the section below it saying nothing they
        // could act on.
        if (trust == TrustState::NotPaired && in.probeAnswered) { CHECK(sectionUnpaired); }
        ++checked;
    }
    REQUIRE(checked == 128);
}

TEST_CASE("A readable mutual-TLS reply outranks a plaintext PairStatus of 0",
          "[moonlight][sessionui]") {
    // A host with no client certificate in front of it reports PairStatus 0 to
    // everyone, so the plaintext probe cannot be the last word on trust. The
    // manager folds a readable /applist into hostPairStatus before this runs.
    SessionUiInputs in;
    in.probeAnswered = true;
    in.pairingHeld = true;
    in.hostPairStatus = false;
    REQUIRE(sessionUiState(in) == SessionUiState::TrustLost);
    REQUIRE(trustFor(in) == TrustState::NotPaired);

    in.hostPairStatus = true;
    in.appsFetched = true;
    in.appCount = 2;
    REQUIRE(sessionUiState(in) == SessionUiState::NewSession);
    REQUIRE(trustFor(in) == TrustState::Paired);
}

TEST_CASE("M8 host replaced beats every other trust answer", "[moonlight][sessionui]") {
    SessionUiInputs in = paired();
    in.uniqueIdChanged = true;
    REQUIRE(sessionUiState(in) == SessionUiState::HostReplaced);
    REQUIRE(tokenOf(in) == QStringLiteral("hostReplaced"));
}

TEST_CASE("A certificate that is not the pinned one is the same answer as a new uniqueid",
          "[moonlight][sessionui]") {
    // The second witness, and the one that arrives FIRST: the pin is checked
    // during the TLS handshake and the uniqueid only once a plaintext probe
    // answers. Without it the refused handshake reads as an unreachable host and
    // sends the user looking at their network for a trust problem.
    SessionUiInputs in = paired();
    in.serverCertChanged = true;
    REQUIRE(sessionUiState(in) == SessionUiState::HostReplaced);
    REQUIRE(tokenOf(in) == QStringLiteral("hostReplaced"));
    REQUIRE(trustFor(in) == TrustState::NotPaired);

    // Even with nothing else known about the host, which is the state a forgotten
    // and rebuilt host is in.
    SessionUiInputs bare;
    bare.serverCertChanged = true;
    REQUIRE(sessionUiState(bare) == SessionUiState::HostReplaced);

    // And it outranks a host that is carrying its four controllers, because a pad
    // cannot join a session on a host nothing can authenticate to.
    SessionUiInputs full = paired();
    full.serverCertChanged = true;
    full.boundControllers = static_cast<int>(kMaxPads);
    REQUIRE(sessionUiState(full) == SessionUiState::HostReplaced);
}

TEST_CASE("M9 through M12: the app list, in flight and in every way it can end",
          "[moonlight][sessionui]") {
    SessionUiInputs loading = paired();
    loading.appsInFlight = true;
    REQUIRE(sessionUiState(loading) == SessionUiState::AppsLoading);
    REQUIRE(tokenOf(loading) == QStringLiteral("appsLoading"));

    SessionUiInputs ready = paired();
    ready.appsFetched = true;
    ready.appCount = 2;
    REQUIRE(sessionUiState(ready) == SessionUiState::NewSession);
    REQUIRE(tokenOf(ready) == QStringLiteral("newSession"));

    SessionUiInputs empty = paired();
    empty.appsFetched = true;
    empty.appCount = 0;
    REQUIRE(sessionUiState(empty) == SessionUiState::NoApps);
    REQUIRE(tokenOf(empty) == QStringLiteral("noApps"));

    SessionUiInputs failed = paired();
    failed.appsFailed = true;
    REQUIRE(sessionUiState(failed) == SessionUiState::AppsFailed);
    REQUIRE(tokenOf(failed) == QStringLiteral("appsFailed"));
}

TEST_CASE("M13 joining our session, and it shows no picker", "[moonlight][sessionui]") {
    SessionUiInputs in = paired();
    in.sessionLive = true;
    in.outcome = SessionOutcome::Live;
    in.appsFetched = true;
    in.appCount = 3;
    // The app is settled by whoever created the session, so a list that HAS
    // arrived still does not put the user in front of a choice.
    REQUIRE(sessionUiState(in) == SessionUiState::Joining);
    REQUIRE(tokenOf(in) == QStringLiteral("joining"));

    // The same host, asked by the binding that is actually on the stream.
    in.bindingLive = true;
    REQUIRE(sessionUiState(in) == SessionUiState::Live);
}

TEST_CASE("A live session outranks a probe that has not answered yet", "[moonlight][sessionui]") {
    // The session is its own proof that the host is there, so a spinner must not
    // be drawn over it.
    SessionUiInputs in;
    in.probeInFlight = true;
    in.sessionLive = true;
    in.outcome = SessionOutcome::Live;
    REQUIRE(sessionUiState(in) == SessionUiState::Joining);
    in.bindingLive = true;
    REQUIRE(sessionUiState(in) == SessionUiState::Live);
}

TEST_CASE("M14 host full is judged before anything the network could change",
          "[moonlight][sessionui]") {
    SessionUiInputs in = paired();
    in.boundControllers = static_cast<int>(kMaxPads);
    in.sessionLive = true;
    in.outcome = SessionOutcome::Live;
    REQUIRE(sessionUiState(in) == SessionUiState::HostFull);
    REQUIRE(tokenOf(in) == QStringLiteral("hostFull"));

    in.boundControllers = static_cast<int>(kMaxPads) - 1;
    REQUIRE(sessionUiState(in) == SessionUiState::Joining);

    // It is the ONLY state that blocks Apply and the only one derived purely
    // from local bookkeeping, so no network answer may hide it: a spinner or an
    // unreachable host over it would enable an Apply the bind will refuse.
    SessionUiInputs checking;
    checking.probeInFlight = true;
    checking.boundControllers = static_cast<int>(kMaxPads);
    REQUIRE(sessionUiState(checking) == SessionUiState::HostFull);

    SessionUiInputs silent;
    silent.probeTimedOut = true;
    silent.pairingHeld = true;
    silent.boundControllers = static_cast<int>(kMaxPads);
    REQUIRE(sessionUiState(silent) == SessionUiState::HostFull);

    SessionUiInputs stranger;
    stranger.probeAnswered = true;
    stranger.boundControllers = static_cast<int>(kMaxPads);
    REQUIRE(sessionUiState(stranger) == SessionUiState::HostFull);
}

TEST_CASE("M15 through M18: a refusal outranks the app list it arrived beside",
          "[moonlight][sessionui]") {
    const std::pair<SessionOutcome, const char*> cases[] = {
        {SessionOutcome::BusyOther, "busyOther"},
        {SessionOutcome::ResumeFailed, "resumeFailed"},
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
    live.sessionLive = true;
    live.bindingLive = true;
    REQUIRE(sessionUiState(live) == SessionUiState::Live);
    REQUIRE(tokenOf(live) == QStringLiteral("live"));

    SessionUiInputs dropped = paired();
    dropped.outcome = SessionOutcome::Dropped;
    REQUIRE(sessionUiState(dropped) == SessionUiState::Dropped);
    REQUIRE(tokenOf(dropped) == QStringLiteral("dropped"));

    SessionUiInputs ended = paired();
    ended.outcome = SessionOutcome::EndedByHost;
    REQUIRE(sessionUiState(ended) == SessionUiState::EndedByHost);
    REQUIRE(tokenOf(ended) == QStringLiteral("endedByHost"));
}

TEST_CASE("Every state has a token and no two share one", "[moonlight][sessionui]") {
    const SessionUiState all[] = {
        SessionUiState::Checking,       SessionUiState::NotPaired,    SessionUiState::PairingPin,
        SessionUiState::PairingRefused, SessionUiState::Unreachable,  SessionUiState::Remembered,
        SessionUiState::TrustLost,      SessionUiState::HostReplaced, SessionUiState::AppsLoading,
        SessionUiState::NewSession,     SessionUiState::NoApps,       SessionUiState::AppsFailed,
        SessionUiState::Joining,        SessionUiState::HostFull,     SessionUiState::BusyOther,
        SessionUiState::ResumeFailed,   SessionUiState::Refused,      SessionUiState::SetupFailed,
        SessionUiState::Live,           SessionUiState::Dropped,      SessionUiState::EndedByHost,
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
        SessionUiState::Checking,       SessionUiState::NotPaired,    SessionUiState::PairingPin,
        SessionUiState::PairingRefused, SessionUiState::Unreachable,  SessionUiState::Remembered,
        SessionUiState::TrustLost,      SessionUiState::HostReplaced, SessionUiState::AppsLoading,
        SessionUiState::NewSession,     SessionUiState::NoApps,       SessionUiState::AppsFailed,
        SessionUiState::Joining,        SessionUiState::HostFull,     SessionUiState::BusyOther,
        SessionUiState::ResumeFailed,   SessionUiState::Refused,      SessionUiState::SetupFailed,
        SessionUiState::Live,           SessionUiState::Dropped,      SessionUiState::EndedByHost,
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
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::AppsFailed));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::Joining));
    REQUIRE_FALSE(sessionUiIsProblem(SessionUiState::Live));

    REQUIRE(sessionUiIsProblem(SessionUiState::PairingRefused));
    REQUIRE(sessionUiIsProblem(SessionUiState::Unreachable));
    REQUIRE(sessionUiIsProblem(SessionUiState::Remembered));
    REQUIRE(sessionUiIsProblem(SessionUiState::TrustLost));
    REQUIRE(sessionUiIsProblem(SessionUiState::HostReplaced));
    REQUIRE(sessionUiIsProblem(SessionUiState::HostFull));
    REQUIRE(sessionUiIsProblem(SessionUiState::BusyOther));
    REQUIRE(sessionUiIsProblem(SessionUiState::ResumeFailed));
    REQUIRE(sessionUiIsProblem(SessionUiState::Refused));
    REQUIRE(sessionUiIsProblem(SessionUiState::SetupFailed));
    REQUIRE(sessionUiIsProblem(SessionUiState::Dropped));
    REQUIRE(sessionUiIsProblem(SessionUiState::EndedByHost));
}

TEST_CASE("Only the two refusals and a live session offer to close the app",
          "[moonlight][sessionui]") {
    REQUIRE(sessionUiOffersQuit(SessionUiState::BusyOther));
    REQUIRE(sessionUiOffersQuit(SessionUiState::ResumeFailed));
    REQUIRE(sessionUiOffersQuit(SessionUiState::Live));
    REQUIRE_FALSE(sessionUiOffersQuit(SessionUiState::NewSession));
    REQUIRE_FALSE(sessionUiOffersQuit(SessionUiState::Dropped));
    REQUIRE_FALSE(sessionUiOffersQuit(SessionUiState::NotPaired));
}

TEST_CASE("Trust is three words, and a remembered host that is silent keeps its pairing",
          "[moonlight][sessionui]") {
    // Both halves. The host's word on its own was what stranded a forgotten host
    // behind a Paired chip with no Pair button; see the recovery case above.
    SessionUiInputs answeredPaired;
    answeredPaired.probeAnswered = true;
    answeredPaired.hostPairStatus = true;
    answeredPaired.pairingHeld = true;
    REQUIRE(trustFor(answeredPaired) == TrustState::Paired);
    REQUIRE(moonlightTrustToken(trustFor(answeredPaired)) == QStringLiteral("paired"));

    SessionUiInputs silent;
    silent.pairingHeld = true;
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
    replaced.pairingHeld = true;
    replaced.uniqueIdChanged = true;
    REQUIRE(trustFor(replaced) == TrustState::NotPaired);
}

TEST_CASE("A bind that refused says which refusal it was", "[moonlight][sessionui]") {
    // A bind that returns nothing is one the user cannot tell from a bind that
    // worked, so only the outcome that WORKED has no token to report.
    REQUIRE(moonlightBindOutcomeToken(BindOutcome::Bound).isEmpty());
    REQUIRE(moonlightBindOutcomeToken(BindOutcome::UnknownHost) == QStringLiteral("hostGone"));
    REQUIRE(moonlightBindOutcomeToken(BindOutcome::NoIdentity) == QStringLiteral("identityFailed"));
    REQUIRE(moonlightBindOutcomeToken(BindOutcome::HostFull) == QStringLiteral("hostFull"));

    QSet<QString> seen;
    for (auto outcome :
         {BindOutcome::UnknownHost, BindOutcome::NoIdentity, BindOutcome::HostFull}) {
        seen.insert(moonlightBindOutcomeToken(outcome));
    }
    REQUIRE(seen.size() == 3); // no two refusals share a sentence
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
            SessionOutcome::ResumeFailed);
    REQUIRE(detail::outcomeState(SessionOutcome::ResumeFailed) == SessionUiState::ResumeFailed);
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
