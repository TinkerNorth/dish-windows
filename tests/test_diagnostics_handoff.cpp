// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Windows mail hand-off (source/system/DiagnosticsHandoff.h) with the
// machine replaced: MAPISendMailW answers what each case asks for, and the
// mailto: link and Explorer are recorded instead of opened. MAPI's answer comes
// from the hand-off's own worker thread, as it does on a real machine.

#include "source/system/DiagnosticsHandoff.h"

#include "InstalledCatalog.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QString>

#include <atomic>
#include <optional>
#include <vector>

using dish::source::DiagnosticsHandoff;
using dish::source::DiagnosticsMail;
using dish::source::MailShell;

namespace {

// mapi.h's answers this test plays back.
constexpr unsigned long kMapiSuccess = 0;
constexpr unsigned long kMapiUserAbort = 1;
// What the new Outlook and the Mail app give: neither is a Simple MAPI client.
constexpr unsigned long kMapiFailure = 2;

// What the hand-off did on the machine, and what it said after.
struct MailRig {
    std::optional<unsigned long> mapiAnswer;
    std::atomic<int> mapiCalls{0};
    std::vector<QByteArray> mailtoUrls;
    std::vector<QString> revealed;
    std::optional<bool> handedOff;
    QString handedFileName;

    std::optional<unsigned long> answerMapi(const DiagnosticsMail& /*mail*/) {
        ++mapiCalls;
        return mapiAnswer;
    }
    void onHandedOff(bool viaMailClient, const QString& fileName) {
        handedOff = viaMailClient;
        handedFileName = fileName;
    }
};

MailShell shellOf(MailRig& rig) {
    return MailShell{[&rig](const DiagnosticsMail& mail) { return rig.answerMapi(mail); },
                     [&rig](const QByteArray& url) { rig.mailtoUrls.push_back(url); },
                     [&rig](const QString& path) { rig.revealed.push_back(path); }};
}

DiagnosticsMail aMail() {
    return DiagnosticsMail{
        QStringLiteral("support@tinkernorth.com"),
        QStringLiteral("Dish support: Other · Dish for Windows 2.8.0 on AYN Thor"),
        QStringLiteral("line 1\nline 2"),
        QStringLiteral("C:/Users/x/AppData/Local/Dish/diagnostics/"
                       "dish-diagnostics-dish-windows-20261122-101500.json")};
}

bool answered(const MailRig& rig) { return rig.handedOff.has_value(); }

// Runs the event loop until the hand-off has answered, ten seconds at most.
bool waitForAnswer(const MailRig& rig) {
    const QDeadlineTimer deadline(10000);
    while (!answered(rig)) {
        if (deadline.hasExpired()) { return false; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

// Sends one mail through a hand-off over `rig` and waits for its answer.
void sendThrough(MailRig& rig) {
    dish::test::ensureApp();
    DiagnosticsHandoff handoff(shellOf(rig), nullptr);
    QObject::connect(&handoff, &DiagnosticsHandoff::handedOff, &handoff,
                     [&rig](bool via, const QString& name) { rig.onHandedOff(via, name); });
    REQUIRE(handoff.send(aMail()));
    REQUIRE(waitForAnswer(rig));
}

} // namespace

// A MAPI provider that takes the message may still leave the attachment out, and the person
// cannot tell from here: the file is shown, and the page names it.
TEST_CASE("handoff: a mail client that takes the mail also shows the file", "[diagnostics][mail]") {
    MailRig rig;
    rig.mapiAnswer = kMapiSuccess;
    sendThrough(rig);
    CHECK(*rig.handedOff);
    CHECK(rig.mailtoUrls.empty());
    CHECK(rig.revealed == std::vector<QString>{aMail().attachmentPath});
    CHECK(rig.handedFileName ==
          QStringLiteral("dish-diagnostics-dish-windows-20261122-101500.json"));
}

// A closed compose window is the person's answer, not a missing client: no mailto: of its own.
// Classic Outlook installed beside the new one takes the call and can end it this way too.
TEST_CASE("handoff: a compose window closed unsent shows the file and opens nothing else",
          "[diagnostics][mail]") {
    MailRig rig;
    rig.mapiAnswer = kMapiUserAbort;
    sendThrough(rig);
    CHECK(*rig.handedOff);
    CHECK(rig.mailtoUrls.empty());
    CHECK(rig.revealed == std::vector<QString>{aMail().attachmentPath});
}

TEST_CASE("handoff: no Simple MAPI client opens mailto and shows the file", "[diagnostics][mail]") {
    MailRig rig;
    rig.mapiAnswer = kMapiFailure;
    sendThrough(rig);
    CHECK_FALSE(*rig.handedOff);
    REQUIRE(rig.mailtoUrls.size() == 1U);
    CHECK(rig.mailtoUrls[0].startsWith("mailto:support@tinkernorth.com?subject="));
    CHECK(rig.mailtoUrls[0].contains("&body=line%201%0Aline%202"));
    CHECK(rig.revealed == std::vector<QString>{aMail().attachmentPath});
    CHECK(rig.handedFileName ==
          QStringLiteral("dish-diagnostics-dish-windows-20261122-101500.json"));
}

TEST_CASE("handoff: no MAPI at all opens mailto and shows the file", "[diagnostics][mail]") {
    MailRig rig;
    sendThrough(rig);
    CHECK_FALSE(*rig.handedOff);
    CHECK(rig.mailtoUrls.size() == 1U);
    CHECK(rig.revealed == std::vector<QString>{aMail().attachmentPath});
    CHECK(rig.mapiCalls.load() == 1);
}
