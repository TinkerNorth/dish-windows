// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/tray/Win32TrayIcon.h"

#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>
#include <QSize>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

#include <cstring>

namespace dish::source {

namespace {

Q_LOGGING_CATEGORY(lcDishTray, "dish.tray")

// The callback the shell posts for every interaction with the item.
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;
constexpr const wchar_t* kWindowClassName = L"DishTrayMessageWindow";
// The icon the exe's resource script embeds (packaging/dish.rc), addressed by
// its name: the script defines no numeric id, so the resource compiler keeps
// the identifier as a string.
constexpr const wchar_t* kAppIconResource = L"IDI_DISH_ICON";
constexpr const char* kStreamingGlyphResource = ":/brand/dish-receiving.svg";

// The exe's own icon at the shell's small-icon size, or the stock application
// icon when the resource is missing (a test binary carries no dish.rc).
HICON loadAppIcon(int cx, int cy) {
    auto* icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), kAppIconResource,
                                               IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    if (icon != nullptr) { return icon; }
    return LoadIconW(nullptr, IDI_APPLICATION);
}

// The brand's receiving glyph, rendered for the streaming state. Null when the
// resource or the SVG plugin is absent, and the caller keeps the app icon then.
HICON loadStreamingIcon(int cx, int cy) {
    QImageReader reader(QString::fromLatin1(kStreamingGlyphResource));
    reader.setScaledSize(QSize(cx, cy));
    const QImage image = reader.read();
    if (image.isNull()) { return nullptr; }
    return image.toHICON();
}

} // namespace

struct Win32TrayIcon::Native {
    HWND hwnd = nullptr;
    ATOM windowClass = 0;
    HICON idleIcon = nullptr;
    HICON streamingIcon = nullptr;
    UINT taskbarCreated = 0;
    Win32TrayIcon* owner = nullptr;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Win32TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self != nullptr && self->native_ != nullptr) {
            if (message == kTrayCallbackMessage) {
                const unsigned event = LOWORD(lParam);
                if (eventOpensMenu(event)) {
                    self->openMenu();
                } else {
                    self->runCommand(commandForEvent(event));
                }
                return 0;
            }
            // Explorer came back after a restart or a crash and has forgotten
            // every icon: put ours back if it is meant to be there.
            if (message == self->native_->taskbarCreated && self->shown_) {
                self->setAvailable(self->addIcon());
                return 0;
            }
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    NOTIFYICONDATAW data() const {
        NOTIFYICONDATAW nid;
        std::memset(&nid, 0, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = kTrayIconId;
        return nid;
    }
};

Win32TrayIcon::Win32TrayIcon(QObject* parent) : TrayIcon(parent), native_(new Native) {
    native_->owner = this;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Native::windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    // A second registration (two icons in one process) reuses the first.
    native_->windowClass = RegisterClassExW(&wc);
    if (native_->windowClass == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        qCWarning(lcDishTray) << "tray window class registration failed:" << GetLastError();
        return;
    }
    native_->hwnd = CreateWindowExW(0, kWindowClassName, L"Dish", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                    nullptr, instance, nullptr);
    if (native_->hwnd == nullptr) {
        qCWarning(lcDishTray) << "tray message window creation failed:" << GetLastError();
        return;
    }
    SetWindowLongPtrW(native_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    native_->taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    native_->idleIcon = loadAppIcon(cx, cy);
    native_->streamingIcon = loadStreamingIcon(cx, cy);
}

Win32TrayIcon::~Win32TrayIcon() {
    if (native_ == nullptr) { return; }
    if (shown_) { deleteIcon(); }
    if (native_->hwnd != nullptr) {
        SetWindowLongPtrW(native_->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(native_->hwnd);
    }
    if (native_->streamingIcon != nullptr) { DestroyIcon(native_->streamingIcon); }
    // The idle icon came from LoadImage without LR_SHARED, so it is ours to
    // destroy; the stock fallback is shared and DestroyIcon ignores it.
    if (native_->idleIcon != nullptr) { DestroyIcon(native_->idleIcon); }
}

void Win32TrayIcon::show() {
    if (native_ == nullptr || native_->hwnd == nullptr) { return; }
    shown_ = true;
    setAvailable(addIcon());
}

void Win32TrayIcon::hide() {
    if (!shown_) { return; }
    shown_ = false;
    deleteIcon();
    setAvailable(false);
}

bool Win32TrayIcon::isAvailable() const { return available_; }

void Win32TrayIcon::setPresentation(const reducer::TrayPresentation& presentation) {
    presentation_ = presentation;
    if (shown_ && available_) { applyPresentation(); }
}

void Win32TrayIcon::showBalloon(const QString& title, const QString& body) {
    if (native_ == nullptr || !shown_ || !available_) { return; }
    NOTIFYICONDATAW nid = native_->data();
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    const std::wstring wtitle = title.toStdWString();
    const std::wstring wbody = body.toStdWString();
    wcsncpy_s(nid.szInfoTitle, wtitle.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, wbody.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(NIM_MODIFY, &nid) == FALSE) {
        qCDebug(lcDishTray) << "balloon refused:" << GetLastError();
    }
}

Win32TrayIcon::MenuCommand Win32TrayIcon::commandForEvent(unsigned event) {
    switch (event) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
        return CommandShowWindow;
    default:
        return CommandNone;
    }
}

bool Win32TrayIcon::eventOpensMenu(unsigned event) { return event == WM_CONTEXTMENU; }

void Win32TrayIcon::runCommand(MenuCommand command) {
    switch (command) {
    case CommandShowWindow:
        emit showWindowRequested();
        break;
    case CommandQuit:
        emit quitRequested();
        break;
    case CommandNone:
        break;
    }
}

QString Win32TrayIcon::tooltipFor(const reducer::TrayPresentation& presentation) {
    if (presentation.activity == reducer::TrayActivity::Streaming) {
        //: The tray item's tooltip while controllers stream; %n is the count.
        return tr("Dish · %n controllers streaming", "", presentation.streamingSlots);
    }
    return QStringLiteral("Dish");
}

bool Win32TrayIcon::addIcon() {
    NOTIFYICONDATAW nid = native_->data();
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = presentation_.activity == reducer::TrayActivity::Streaming &&
                        native_->streamingIcon != nullptr
                    ? native_->streamingIcon
                    : native_->idleIcon;
    const std::wstring tip = tooltipFor(presentation_).toStdWString();
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    // NIM_ADD fails when the icon is already there (a re-add on TaskbarCreated
    // that raced the shell's own restore); a modify is the same outcome then.
    if (Shell_NotifyIconW(NIM_ADD, &nid) == FALSE && Shell_NotifyIconW(NIM_MODIFY, &nid) == FALSE) {
        qCWarning(lcDishTray) << "Shell_NotifyIcon add failed:" << GetLastError();
        return false;
    }
    // Version 4: a click arrives as NIN_SELECT and the context menu request
    // as WM_CONTEXTMENU, with the coordinates on wParam rather than the cursor.
    nid.uVersion = NOTIFYICON_VERSION_4;
    if (Shell_NotifyIconW(NIM_SETVERSION, &nid) == FALSE) {
        qCWarning(lcDishTray) << "Shell_NotifyIcon set-version failed:" << GetLastError();
    }
    return true;
}

void Win32TrayIcon::deleteIcon() {
    NOTIFYICONDATAW nid = native_->data();
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void Win32TrayIcon::applyPresentation() {
    NOTIFYICONDATAW nid = native_->data();
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = presentation_.activity == reducer::TrayActivity::Streaming &&
                        native_->streamingIcon != nullptr
                    ? native_->streamingIcon
                    : native_->idleIcon;
    const std::wstring tip = tooltipFor(presentation_).toStdWString();
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(NIM_MODIFY, &nid) == FALSE) {
        qCDebug(lcDishTray) << "Shell_NotifyIcon modify failed:" << GetLastError();
    }
}

void Win32TrayIcon::openMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) { return; }
    AppendMenuW(menu, MF_STRING, CommandShowWindow, tr("Show Dish").toStdWString().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandQuit, tr("Quit").toStdWString().c_str());
    // The documented dance: the menu only closes on an outside click once the
    // owner window is in the foreground, and the WM_NULL afterwards is what
    // lets the shell drop the menu's mouse capture.
    SetForegroundWindow(native_->hwnd);
    POINT cursor;
    GetCursorPos(&cursor);
    const int picked = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                        cursor.x, cursor.y, native_->hwnd, nullptr);
    PostMessageW(native_->hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    runCommand(static_cast<MenuCommand>(picked));
}

void Win32TrayIcon::setAvailable(bool available) {
    if (available_ == available) { return; }
    available_ = available;
    emit availabilityChanged(available);
}

std::unique_ptr<Win32TrayIcon> makeSystemTrayIcon() { return std::make_unique<Win32TrayIcon>(); }

} // namespace dish::source
