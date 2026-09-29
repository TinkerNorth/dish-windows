// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The tray item's one line to the shell: Shell_NotifyIconW, and nothing else.
// Abstract so the item can be driven by a test that stands in for Explorer,
// without adding anything to the real notification area.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

namespace dish::source {

class NotifyIconShell {
  public:
    virtual ~NotifyIconShell() = default;

    // `message` is one of the NIM_ verbs; true when the shell accepted it.
    virtual bool notify(DWORD message, NOTIFYICONDATAW& data) = 0;
};

class Win32NotifyIconShell final : public NotifyIconShell {
  public:
    bool notify(DWORD message, NOTIFYICONDATAW& data) override {
        return Shell_NotifyIconW(message, &data) != FALSE;
    }
};

} // namespace dish::source
