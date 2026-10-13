// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/CrashRearmHook.h"

namespace dish::crash {

namespace {
RearmHook g_rearmHook = nullptr;
} // namespace

void setRearmHook(RearmHook hook) { g_rearmHook = hook; }

void rearmAfterSdkInit() {
    if (g_rearmHook != nullptr) { g_rearmHook(); }
}

RearmHook rearmHook() { return g_rearmHook; }

} // namespace dish::crash
