// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/ops/Win32ShortcutOps.h"

#include "installer/ops/KnownFolders.h"
#include "installer/ops/Win32FileOps.h"

#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>

namespace dish::installer {

namespace {

// Apartment-threaded per call: cheap, and it keeps this class usable from the
// worker thread without a thread-wide COM policy.
class ComApartment {
  public:
    ComApartment() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() {
        // S_FALSE means "already initialized on this thread": still ours to
        // balance. Only a hard failure leaves nothing to uninitialize.
        if (SUCCEEDED(hr_)) { CoUninitialize(); }
    }
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

  private:
    HRESULT hr_;
};

void notifyShell(const QString& linkAbs) {
    const QString parent = QDir::cleanPath(linkAbs + QStringLiteral("/.."));
    const std::wstring wide = QDir::toNativeSeparators(parent).toStdWString();
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, wide.c_str(), nullptr);
}

} // namespace

OpResult Win32ShortcutOps::create(const ShortcutSpec& spec) {
    const QString link = shortcutLinkPath(spec.location, spec.scope);
    if (link.isEmpty()) {
        return OpResult::failure(SetupError::ShortcutFailed, shortcutLocationToken(spec.location));
    }

    ComApartment com;
    if (!com.ok()) { return OpResult::failure(SetupError::ShortcutFailed, link); }

    // The shell folder normally exists; PublicDesktop on a hardened image may
    // not.
    Win32FileOps files;
    const QString parent = QDir::cleanPath(link + QStringLiteral("/.."));
    if (const OpResult dir = files.ensureDir(parent); !dir.ok) {
        return OpResult::failure(SetupError::ShortcutFailed, link, dir.win32);
    }

    IShellLinkW* shellLink = nullptr;
    HRESULT hr =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
    if (FAILED(hr)) {
        return OpResult::failure(SetupError::ShortcutFailed, link, static_cast<quint32>(hr));
    }

    const std::wstring target = QDir::toNativeSeparators(spec.targetAbs).toStdWString();
    const std::wstring workingDir = QDir::toNativeSeparators(spec.workingDir).toStdWString();
    const std::wstring icon = QDir::toNativeSeparators(spec.iconAbs).toStdWString();
    const std::wstring description = spec.description.toStdWString();
    shellLink->SetPath(target.c_str());
    shellLink->SetWorkingDirectory(workingDir.c_str());
    shellLink->SetIconLocation(icon.c_str(), spec.iconIndex);
    shellLink->SetDescription(description.c_str());

    IPersistFile* persist = nullptr;
    hr = shellLink->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr)) {
        const std::wstring wideLink = QDir::toNativeSeparators(link).toStdWString();
        hr = persist->Save(wideLink.c_str(), TRUE);
        persist->Release();
    }
    shellLink->Release();

    if (FAILED(hr)) {
        return OpResult::failure(SetupError::ShortcutFailed, link, static_cast<quint32>(hr));
    }
    notifyShell(link);
    return OpResult::success(link);
}

OpResult Win32ShortcutOps::remove(const QString& linkAbs) {
    Win32FileOps files;
    const OpResult r = files.remove(linkAbs);
    if (!r.ok) { return OpResult::failure(SetupError::ShortcutFailed, linkAbs, r.win32); }
    notifyShell(linkAbs);
    return OpResult::success(linkAbs);
}

bool Win32ShortcutOps::exists(const QString& linkAbs) {
    Win32FileOps files;
    return files.exists(linkAbs);
}

} // namespace dish::installer
