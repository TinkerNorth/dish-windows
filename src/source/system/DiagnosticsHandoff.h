// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Windows end of "Save diagnostics" and "Email diagnostics"
// (docs/diagnostics-bundle.md, "The mail"): the bundle is written under
// %LOCALAPPDATA%\Dish\diagnostics\, then handed to the person's own mail
// client through Simple MAPI (MAPISendMailW, with the compose window shown so
// nothing is sent until they press Send). When no MAPI client answers, a
// mailto: link opens with the subject and body and Explorer shows the file so
// it can be attached by hand. Which of the two runs is
// core/diagnostics/MailHandoff.h's decision.
//
// MAPISendMailW blocks until the compose window closes, so it runs on a thread
// of its own and its answer is marshalled back to the main thread. One mail at
// a time; the destructor waits for a compose window still open.

#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <optional>
#include <thread>

namespace dish::source {

// %LOCALAPPDATA%/Dish/diagnostics, "" when LOCALAPPDATA is unset.
QString diagnosticsFolder();

// Writes the bundle into the folder; the file's full path, or "" when it could not be written.
QString saveDiagnosticsFile(const QString& folder, const QString& fileName,
                            const QByteArray& bundle);

// Explorer, with the file selected.
void revealInExplorer(const QString& filePath);

struct DiagnosticsMail {
    QString to;
    QString subject;
    QString body;
    QString attachmentPath;
};

class DiagnosticsHandoff : public QObject {
    Q_OBJECT
  public:
    explicit DiagnosticsHandoff(QObject* parent = nullptr);
    ~DiagnosticsHandoff() override;

    DiagnosticsHandoff(const DiagnosticsHandoff&) = delete;
    DiagnosticsHandoff& operator=(const DiagnosticsHandoff&) = delete;
    DiagnosticsHandoff(DiagnosticsHandoff&&) = delete;
    DiagnosticsHandoff& operator=(DiagnosticsHandoff&&) = delete;

    // False while a compose window from the last mail is still open.
    bool send(const DiagnosticsMail& mail);

  signals:
    // `viaMailClient` is true when MAPI took it, false when the mailto: fallback ran.
    void handedOff(bool viaMailClient);

  private:
    void runMapi(const DiagnosticsMail& mail);
    void onMapiAnswered(const DiagnosticsMail& mail, std::optional<unsigned long> answer);
    void openMailtoAndReveal(const DiagnosticsMail& mail);

    std::thread worker_;
    std::atomic<bool> busy_{false};
};

} // namespace dish::source
