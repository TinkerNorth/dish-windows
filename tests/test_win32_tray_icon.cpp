// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The item's event dispatch, tooltip, shell traffic and window, pinned without
// adding anything to the notification area: every item here speaks to a
// recording shell, never to Explorer.

#include "core/reducer/TrayPresentation.h"
#include "source/tray/NotifyIconShell.h"
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

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

using dish::reducer::TrayActivity;
using dish::reducer::TrayPresentation;
using dish::source::NotifyIconShell;
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

struct AvailabilitySpy {
    std::vector<bool> changes;
    explicit AvailabilitySpy(Win32TrayIcon* tray) {
        QObject::connect(tray, &Win32TrayIcon::availabilityChanged,
                         [this](bool available) { changes.push_back(available); });
    }
};

// What the item asked of the shell, verb by verb, and the window it named: the
// shell learns where to send the item's messages from the same field.
struct ShellLog {
    std::vector<DWORD> verbs;
    HWND window = nullptr;
    bool accepts = true;
};

// Stands in for Explorer: records every call and answers as the log says.
class RecordingShell final : public NotifyIconShell {
  public:
    explicit RecordingShell(ShellLog& log) : log_(log) {}

    bool notify(DWORD message, NOTIFYICONDATAW& data) override {
        log_.verbs.push_back(message);
        log_.window = data.hWnd;
        return log_.accepts;
    }

  private:
    ShellLog& log_;
};

// An item wired to the recording shell. The log is declared first so that it
// outlives the item, whose destructor still speaks to the shell.
struct RecordedTray {
    ShellLog shell;
    Win32TrayIcon tray{std::make_unique<RecordingShell>(shell)};
};

std::ptrdiff_t timesAsked(const ShellLog& shell, DWORD verb) {
    return std::count(shell.verbs.begin(), shell.verbs.end(), verb);
}

// Explorer's restart announcement, sent to the item's own window only:
// broadcast, it would make every app on the desktop re-add its icons.
void explorerComesBack(const ShellLog& shell) {
    SendMessageW(shell.window, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
}

// A message only this test registers, so broadcasting it asks nothing of any
// other window on the desktop.
constexpr const wchar_t* kBroadcastProbeName =
    L"DishTests.TrayBroadcastProbe.{87E623F7-1182-448A-9FEE-1D9BCD76BC98}";
constexpr UINT kBroadcastTimeoutPerWindowMs = 200;
constexpr const wchar_t* kMessageWatchProperty = L"DishTests.MessageWatch";

// Counts one message's deliveries to one window by standing in front of the
// window's procedure for as long as the watch lives, passing everything on.
// Only deliveries carrying `sender` count: another test process broadcasting
// the same probe at the same moment reaches this window too.
class MessageWatch {
  public:
    MessageWatch(HWND window, UINT message, WPARAM sender)
        : window_(window), message_(message), sender_(sender) {
        SetPropW(window_, kMessageWatchProperty, this);
        original_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MessageWatch::procedure)));
    }
    ~MessageWatch() {
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));
        RemovePropW(window_, kMessageWatchProperty);
    }
    MessageWatch(const MessageWatch&) = delete;
    MessageWatch& operator=(const MessageWatch&) = delete;

    int deliveries() const { return deliveries_; }

  private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* watch = static_cast<MessageWatch*>(GetPropW(window, kMessageWatchProperty));
        const bool isTheWatchedMessage = message == watch->message_;
        const bool isFromThisSender = wParam == watch->sender_;
        if (isTheWatchedMessage && isFromThisSender) { ++watch->deliveries_; }
        return CallWindowProcW(watch->original_, window, message, wParam, lParam);
    }

    HWND window_;
    UINT message_;
    WPARAM sender_;
    WNDPROC original_ = nullptr;
    int deliveries_ = 0;
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
    RecordedTray fixture;
    CommandSpy spy(&fixture.tray);
    fixture.tray.runCommand(Win32TrayIcon::CommandShowWindow);
    REQUIRE(spy.shows == 1);
    REQUIRE(spy.quits == 0);
    fixture.tray.runCommand(Win32TrayIcon::CommandQuit);
    REQUIRE(spy.shows == 1);
    REQUIRE(spy.quits == 1);
    fixture.tray.runCommand(Win32TrayIcon::CommandNone);
    REQUIRE(spy.shows == 1);
    REQUIRE(spy.quits == 1);
}

TEST_CASE("tray item: not shown and not available until show() adds it", "[tray][windows]") {
    RecordedTray fixture;
    REQUIRE_FALSE(fixture.tray.isShown());
    REQUIRE_FALSE(fixture.tray.isAvailable());
    // A presentation before show() is remembered, not applied.
    fixture.tray.setPresentation(TrayPresentation{TrayActivity::Streaming, 2, false});
    REQUIRE_FALSE(fixture.tray.isShown());
    REQUIRE(fixture.shell.verbs.empty());
}

TEST_CASE("tray item: Explorer coming back puts a shown item back", "[tray][windows]") {
    RecordedTray fixture;
    fixture.tray.show();
    REQUIRE(timesAsked(fixture.shell, NIM_ADD) == 1);
    explorerComesBack(fixture.shell);
    REQUIRE(timesAsked(fixture.shell, NIM_ADD) == 2);
    REQUIRE(fixture.tray.isAvailable());
}

TEST_CASE("tray item: Explorer coming back leaves a hidden item out", "[tray][windows]") {
    RecordedTray fixture;
    fixture.tray.show();
    fixture.tray.hide();
    explorerComesBack(fixture.shell);
    REQUIRE(timesAsked(fixture.shell, NIM_ADD) == 1);
    REQUIRE_FALSE(fixture.tray.isAvailable());
}

TEST_CASE("tray item: an icon the shell refused at logon arrives when Explorer comes up",
          "[tray][windows]") {
    RecordedTray fixture;
    AvailabilitySpy spy(&fixture.tray);
    fixture.shell.accepts = false;
    fixture.tray.show();
    REQUIRE_FALSE(fixture.tray.isAvailable());
    fixture.shell.accepts = true;
    explorerComesBack(fixture.shell);
    REQUIRE(fixture.tray.isAvailable());
    REQUIRE(spy.changes == std::vector<bool>{true});
}

// Explorer announces its restart to every top-level window at once; a
// message-only window is not one and never hears it.
TEST_CASE("tray window: a broadcast reaches it, as Explorer's TaskbarCreated has to",
          "[tray][windows]") {
    RecordedTray fixture;
    fixture.tray.show();
    const UINT probe = RegisterWindowMessageW(kBroadcastProbeName);
    const WPARAM thisProcess = GetCurrentProcessId();
    MessageWatch watch(fixture.shell.window, probe, thisProcess);
    SendMessageTimeoutW(HWND_BROADCAST, probe, thisProcess, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL,
                        kBroadcastTimeoutPerWindowMs, nullptr);
    REQUIRE(watch.deliveries() == 1);
}

TEST_CASE("tray window: never shown, and a tool window, so it gets no taskbar button",
          "[tray][windows]") {
    RecordedTray fixture;
    fixture.tray.show();
    const HWND window = fixture.shell.window;
    const bool isAToolWindow = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    const bool isVisible = IsWindowVisible(window) != FALSE;
    REQUIRE(isAToolWindow);
    REQUIRE_FALSE(isVisible);
}

// taskkill without /F posts WM_CLOSE to every top-level window of the process,
// the item's among them; the main window is the one that answers it.
TEST_CASE("tray window: a close sent to every window of the process keeps the way back",
          "[tray][windows]") {
    RecordedTray fixture;
    fixture.tray.show();
    SendMessageW(fixture.shell.window, WM_CLOSE, 0, 0);
    REQUIRE(IsWindow(fixture.shell.window) != FALSE);
    explorerComesBack(fixture.shell);
    REQUIRE(timesAsked(fixture.shell, NIM_ADD) == 2);
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
