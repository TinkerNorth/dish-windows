// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The production ShortcutOps: IShellLinkW + IPersistFile on the calling
// thread (COM is initialized apartment-threaded per call, so the worker
// thread needs no global CoInitialize), SHChangeNotify after every mutation
// so the Start Menu / Desktop refresh promptly (spec section 10).

#pragma once

#include "installer/ops/ShortcutOps.h"

namespace dish::installer {

class Win32ShortcutOps : public ShortcutOps {
  public:
    Win32ShortcutOps() = default;
    ~Win32ShortcutOps() override = default;

    // Resolves the deterministic link path (KnownFolders::shortcutLinkPath),
    // ensures the parent folder, writes the .lnk, notifies the shell. The
    // resolved absolute path comes back in OpResult::path so the installed
    // manifest can record it.
    OpResult create(const ShortcutSpec& spec) override;
    OpResult remove(const QString& linkAbs) override;
    bool exists(const QString& linkAbs) override;
};

} // namespace dish::installer
