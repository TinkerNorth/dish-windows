// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The notification-area item, spoken to Shell_NotifyIcon directly.
// QSystemTrayIcon lives in QtWidgets and main() deliberately keeps that module
// out of the process, the same call dish-linux made when it spoke
// StatusNotifier over the bus by hand.
//
// The item needs an HWND to receive its callback message; a message-only
// window (HWND_MESSAGE) serves, invisible and owned here. Explorer restarts
// drop every icon, so the TaskbarCreated broadcast re-adds it. Availability is
// whether the shell accepted the icon: Windows always has a notification area,
// but the add can fail (Explorer not up yet at logon), and a failed add must
// not let the window hide, or a running Dish is stranded with no way back.
//
// The menu is the way back and the way out: a click on the item shows the
// window, and the context menu offers Show Dish and Quit. Both surface as the
// TrayIcon signals; nothing here reaches into the app.

#pragma once

#include "core/reducer/TrayPresentation.h"
#include "source/tray/TrayIcon.h"

#include <QString>

#include <memory>

namespace dish::source {

class Win32TrayIcon final : public TrayIcon {
    Q_OBJECT
  public:
    explicit Win32TrayIcon(QObject* parent = nullptr);
    ~Win32TrayIcon() override;

    void show() override;
    void hide() override;
    bool isAvailable() const override;
    void setPresentation(const reducer::TrayPresentation& presentation) override;

    // A balloon hung off the item, which Windows 10 and 11 render as a toast.
    // Only lands while the item is shown; the shell drops it otherwise, which
    // is the right answer for a notice that says "quit from the tray icon".
    void showBalloon(const QString& title, const QString& body);

    // ── The event side, public so the dispatch is pinned without a shell ────
    enum MenuCommand : int { CommandNone = 0, CommandShowWindow = 1, CommandQuit = 2 };

    // `event` is the low word of the callback message's lParam under
    // NOTIFYICON_VERSION_4. A select (click or keyboard) shows the window; the
    // context-menu request opens the menu, which the window procedure does
    // because it needs the HWND and the cursor. Everything else (the mouse
    // moves and button messages the shell still sends beside the NIN_ ones)
    // resolves to nothing, so a click can never be handled twice.
    static MenuCommand commandForEvent(unsigned event);
    static bool eventOpensMenu(unsigned event);
    void runCommand(MenuCommand command);

    // The tooltip: the app name idle, the streaming count otherwise.
    static QString tooltipFor(const reducer::TrayPresentation& presentation);

    bool isShown() const { return shown_; }

  private:
    struct Native;
    friend struct Native;

    bool addIcon();
    void deleteIcon();
    void applyPresentation();
    void openMenu();
    void setAvailable(bool available);

    std::unique_ptr<Native> native_;
    reducer::TrayPresentation presentation_;
    bool shown_ = false;
    bool available_ = false;
};

// The platform tray item. Never null: a session where the shell refuses the
// icon still gets an item that honestly reports isAvailable() == false.
std::unique_ptr<Win32TrayIcon> makeSystemTrayIcon();

} // namespace dish::source
