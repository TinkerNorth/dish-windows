// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The one call from the crash-reporting backend back to the local crash
// handler. sentry_init installs crashpad's exception filter and SIGABRT
// handler on top of ours and never calls on to a previous filter, so the
// handler has to put itself back on top after every arm, chaining to what the
// SDK installed. The handler lives in the app target and the backend in
// dish_core, so the link is a function pointer the handler registers at boot:
// process-wide, like the rest of the crash handler's state, because the fault
// path takes no context.

#pragma once

namespace dish::crash {

using RearmHook = void (*)();

// Registered once by UI/CrashHandler::install(); nullptr means no handler.
void setRearmHook(RearmHook hook);

// Called by the backend after a successful sentry_init. A no-op with no hook.
void rearmAfterSdkInit();

// The hook now registered, for the tests.
RearmHook rearmHook();

} // namespace dish::crash
