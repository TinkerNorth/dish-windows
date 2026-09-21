// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The item's event dispatch and tooltip, pinned without adding anything to the
// notification area: constructing the item only creates its message-only
// window, and no test here calls show().

#include "core/reducer/TrayPresentation.h"
#include "source/tray/Win32TrayIcon.h"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QString>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

using dish::reducer::TrayActivity;
using dish::reducer::TrayPresentation;
using dish::source::Win32TrayIcon;

namespace {

struct CommandSpy {
    int shows = 0;
    int quits = 0;
    explicit CommandSpy(Win32TrayIcon* tray) {
        QObject::connect(tray, &Win32TrayIcon::showWindowRequested, [this]() { ++shows; });
        QObject::connect(tray, &Win32TrayIcon::quitRequested, [this]() { ++quits; });
    }
};

} // namespace

TEST_CASE("tray dispatch: a select (click or keyboard) is the way back to the window",
          "[tray][windows]") {
    REQUIRE(Win32TrayIcon::commandForEvent(NIN_SELECT) == Win32TrayIcon::CommandShowWindow);
    REQUIRE(Win32TrayIcon::commandForEvent(NIN_KEYSELECT) == Win32TrayIcon::CommandShowWindow);
}

TEST_CASE("tray dispatch: the mouse messages the shell sends beside a select do nothing",
          "[tray][windows]") {
    // Version 4 delivers NIN_SELECT for a click AND the raw button messages;
    // handling both would show the window twice, or once for a click that was
    // only a mouse-down.
    REQUIRE(Win32TrayIcon::commandForEvent(WM_LBUTTONDOWN) == Win32TrayIcon::CommandNone);
    REQUIRE(Win32TrayIcon::commandForEvent(WM_LBUTTONUP) == Win32TrayIcon::CommandNone);
    REQUIRE(Win32TrayIcon::commandForEvent(WM_MOUSEMOVE) == Win32TrayIcon::CommandNone);
    REQUIRE(Win32TrayIcon::commandForEvent(WM_RBUTTONUP) == Win32TrayIcon::CommandNone);
}

TEST_CASE("tray dispatch: only the context-menu request opens the menu", "[tray][windows]") {
    REQUIRE(Win32TrayIcon::eventOpensMenu(WM_CONTEXTMENU));
    REQUIRE_FALSE(Win32TrayIcon::eventOpensMenu(WM_RBUTTONUP));
    REQUIRE_FALSE(Win32TrayIcon::eventOpensMenu(NIN_SELECT));
}

TEST_CASE("tray commands: show and quit surface as the TrayIcon signals", "[tray][windows]") {
    Win32TrayIcon tray;
    CommandSpy spy(&tray);
    tray.runCommand(Win32TrayIcon::CommandShowWindow);
    REQUIRE(spy.shows == 1);
    REQUIRE(spy.quits == 0);
    tray.runCommand(Win32TrayIcon::CommandQuit);
    REQUIRE(spy.shows == 1);
    REQUIRE(spy.quits == 1);
    tray.runCommand(Win32TrayIcon::CommandNone);
    REQUIRE(spy.shows == 1);
    REQUIRE(spy.quits == 1);
}

TEST_CASE("tray item: not shown and not available until show() adds it", "[tray][windows]") {
    Win32TrayIcon tray;
    REQUIRE_FALSE(tray.isShown());
    REQUIRE_FALSE(tray.isAvailable());
    // A presentation before show() is remembered, not applied.
    tray.setPresentation(TrayPresentation{TrayActivity::Streaming, 2, false});
    REQUIRE_FALSE(tray.isShown());
}

TEST_CASE("tray tooltip: the app name idle, the streaming count otherwise", "[tray][windows]") {
    REQUIRE(Win32TrayIcon::tooltipFor(TrayPresentation{TrayActivity::Idle, 0, true}) ==
            QStringLiteral("Dish"));
    const QString one =
        Win32TrayIcon::tooltipFor(TrayPresentation{TrayActivity::Streaming, 1, true});
    const QString two =
        Win32TrayIcon::tooltipFor(TrayPresentation{TrayActivity::Streaming, 2, true});
    REQUIRE(one.startsWith(QStringLiteral("Dish")));
    REQUIRE(one.contains(QStringLiteral("1")));
    REQUIRE(two.contains(QStringLiteral("2")));
    REQUIRE(one != two);
}
