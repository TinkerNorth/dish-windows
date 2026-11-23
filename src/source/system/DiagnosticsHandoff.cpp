// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/DiagnosticsHandoff.h"

#include "core/diagnostics/MailHandoff.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>

#include <windows.h>

#include <MAPI.h>
#include <objbase.h>

#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace dish::source {

namespace {

// MAPISendMailW's shape (mapi.h), named here so the load below does not depend on which typedef
// name an SDK version spells it with.
using SendMailW = ULONG(WINAPI*)(ULONG_PTR session, ULONG_PTR uiParam, MapiMessageW* message,
                                 ULONG flags, ULONG reserved);

// mapi32.dll is the stub every Windows ships, which forwards to the default mail client's own
// MAPI provider; with no client configured it still loads and answers MAPI_E_* instead.
std::optional<unsigned long> sendThroughMapi(const DiagnosticsMail& mail) {
    HMODULE mapi = ::LoadLibraryW(L"mapi32.dll");
    if (mapi == nullptr) { return std::nullopt; }
    const FARPROC proc = ::GetProcAddress(mapi, "MAPISendMailW");
    if (proc == nullptr) {
        ::FreeLibrary(mapi);
        return std::nullopt;
    }
    // Copied, not cast: a function-pointer cast trips -Wcast-function-type-mismatch and laundering
    // through void* trips bugprone-casting-through-void (FramelessWindowChrome does the same).
    static_assert(sizeof(SendMailW) == sizeof(FARPROC),
                  "MAPISendMailW and FARPROC must be the same width to copy between them");
    SendMailW send = nullptr;
    std::memcpy(static_cast<void*>(&send), static_cast<const void*>(&proc), sizeof(send));

    std::wstring subject = mail.subject.toStdWString();
    std::wstring body = mail.body.toStdWString();
    std::wstring address = L"SMTP:" + mail.to.toStdWString();
    std::wstring name = mail.to.toStdWString();
    std::wstring path = QDir::toNativeSeparators(mail.attachmentPath).toStdWString();
    std::wstring fileName = QFileInfo(mail.attachmentPath).fileName().toStdWString();

    MapiRecipDescW recipient{};
    recipient.ulRecipClass = MAPI_TO;
    recipient.lpszName = name.data();
    recipient.lpszAddress = address.data();

    MapiFileDescW attachment{};
    attachment.nPosition = static_cast<ULONG>(-1);
    attachment.lpszPathName = path.data();
    attachment.lpszFileName = fileName.data();

    MapiMessageW message{};
    message.lpszSubject = subject.data();
    message.lpszNoteText = body.data();
    message.nRecipCount = 1;
    message.lpRecips = &recipient;
    message.nFileCount = 1;
    message.lpFiles = &attachment;

    // MAPI_DIALOG: the person's own compose window, which only they can send from.
    const ULONG answer = send(0, 0, &message, MAPI_LOGON_UI | MAPI_DIALOG, 0);
    ::FreeLibrary(mapi);
    return answer;
}

void openMailtoUrl(const QByteArray& url) {
    QDesktopServices::openUrl(QUrl::fromEncoded(url, QUrl::StrictMode));
}

} // namespace

QString diagnosticsFolder() {
    const QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) { return {}; }
    return QDir::fromNativeSeparators(base) + QStringLiteral("/Dish/diagnostics");
}

QString saveDiagnosticsFile(const QString& folder, const QString& fileName,
                            const QByteArray& bundle) {
    if (folder.isEmpty() || !QDir().mkpath(folder)) { return {}; }
    QString path = folder + QLatin1Char('/') + fileName;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { return {}; }
    const bool wrote = file.write(bundle) == bundle.size();
    if (!wrote || !file.commit()) { return {}; }
    return path;
}

void revealInExplorer(const QString& filePath) {
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(filePath)});
}

MailShell windowsMailShell() {
    return MailShell{&sendThroughMapi, &openMailtoUrl, &revealInExplorer};
}

DiagnosticsHandoff::DiagnosticsHandoff(QObject* parent)
    : DiagnosticsHandoff(windowsMailShell(), parent) {}

DiagnosticsHandoff::DiagnosticsHandoff(MailShell shell, QObject* parent)
    : QObject(parent), shell_(std::move(shell)) {}

DiagnosticsHandoff::~DiagnosticsHandoff() {
    if (worker_.joinable()) { worker_.join(); }
}

bool DiagnosticsHandoff::send(const DiagnosticsMail& mail) {
    if (busy_.exchange(true)) { return false; }
    if (worker_.joinable()) { worker_.join(); }
    worker_ = std::thread(&DiagnosticsHandoff::runMapi, this, mail);
    return true;
}

// The worker's body: Simple MAPI wants an apartment of its own on the thread that calls it.
void DiagnosticsHandoff::runMapi(const DiagnosticsMail& mail) {
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::optional<unsigned long> answer = shell_.sendThroughMapi(mail);
    if (SUCCEEDED(com)) { ::CoUninitialize(); }
    QMetaObject::invokeMethod(
        this, [this, mail, answer] { onMapiAnswered(mail, answer); }, Qt::QueuedConnection);
}

void DiagnosticsHandoff::onMapiAnswered(const DiagnosticsMail& mail,
                                        std::optional<unsigned long> answer) {
    busy_.store(false);
    const QString fileName = QFileInfo(mail.attachmentPath).fileName();
    switch (diagnostics::nextStepAfterMapi(answer)) {
    case diagnostics::MailStep::RevealFolder:
        shell_.reveal(mail.attachmentPath);
        emit handedOff(true, fileName);
        return;
    case diagnostics::MailStep::OpenMailtoAndRevealFolder:
        openMailtoAndReveal(mail);
        emit handedOff(false, fileName);
        return;
    }
}

void DiagnosticsHandoff::openMailtoAndReveal(const DiagnosticsMail& mail) {
    const std::string url = diagnostics::mailtoUrl(
        mail.to.toStdString(), mail.subject.toStdString(), mail.body.toStdString());
    shell_.openMailto(QByteArray::fromStdString(url));
    shell_.reveal(mail.attachmentPath);
}

} // namespace dish::source
