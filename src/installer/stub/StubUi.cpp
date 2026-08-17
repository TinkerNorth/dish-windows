// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "StubUi.h"

#include "StubStrings.h"

// windows.h (via StubStrings.h) must precede commctrl.h.
#include <commctrl.h>

namespace dish::installer::stub {

namespace {

void initCommonControlsOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc); // best effort; TaskDialogIndirect has its own gate
}

struct MarqueeContext {
    const std::atomic<bool>* done = nullptr;
    std::atomic<bool>* cancel = nullptr;
    bool closingBecauseDone = false;
};

HRESULT CALLBACK marqueeCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                 LONG_PTR refData) {
    (void)lParam;
    MarqueeContext* ctx = reinterpret_cast<MarqueeContext*>(refData);
    switch (msg) {
    case TDN_CREATED:
        SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
        break;
    case TDN_TIMER:
        if (!ctx->closingBecauseDone && ctx->done->load(std::memory_order_relaxed)) {
            // Work finished: close ourselves. The synthetic IDCANCEL below is
            // distinguished from a real cancel by closingBecauseDone.
            ctx->closingBecauseDone = true;
            SendMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
        }
        break;
    case TDN_BUTTON_CLICKED:
        if (static_cast<int>(wParam) == IDCANCEL && !ctx->closingBecauseDone)
            ctx->cancel->store(true, std::memory_order_relaxed);
        return S_OK; // allow the dialog to close
    default:
        break;
    }
    return S_OK;
}

} // namespace

void runMarqueeUntil(const std::atomic<bool>& done, std::atomic<bool>& cancel) {
    initCommonControlsOnce();
    const StubStrings& strings = stringsForUserLocale();

    MarqueeContext ctx;
    ctx.done = &done;
    ctx.cancel = &cancel;

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_CALLBACK_TIMER |
                     TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON; // label localized by Windows
    config.pszWindowTitle = kDialogTitle;
    config.pszMainInstruction = strings.preparing;
    config.pfCallback = &marqueeCallback;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&ctx);

    // Failure (no comctl32 v6 context) degrades to no UI at all; the caller
    // keeps waiting on the worker thread either way.
    TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

void showDamagedDialog() {
    initCommonControlsOnce();
    const StubStrings& strings = stringsForUserLocale();

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.pszWindowTitle = kDialogTitle;
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszMainInstruction = strings.damaged;
    if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr)))
        MessageBoxW(nullptr, strings.damaged, kDialogTitle, MB_OK | MB_ICONERROR);
}

void showOsTooOldDialog() {
    const StubStrings& strings = stringsForUserLocale();
    MessageBoxW(nullptr, strings.osTooOld, kDialogTitle, MB_OK | MB_ICONERROR);
}

} // namespace dish::installer::stub
