// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Posts Explorer's TaskbarCreated to the one window named on the command line
// (its handle, in decimal) and exits with the error PostMessageW reported, 0
// when the message was posted. The tray test starts it at Low integrity, so
// what it posts crosses the filter Explorer's broadcast crosses on its way to
// an elevated Dish.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cwchar>

namespace {

constexpr int kArgumentCount = 2;
constexpr int kDecimal = 10;

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != kArgumentCount) { return static_cast<int>(ERROR_BAD_ARGUMENTS); }
    const unsigned long long handleValue = std::wcstoull(argv[1], nullptr, kDecimal);
    const auto window = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(handleValue));
    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    const bool posted = PostMessageW(window, taskbarCreated, 0, 0) != FALSE;
    return posted ? 0 : static_cast<int>(GetLastError());
}
