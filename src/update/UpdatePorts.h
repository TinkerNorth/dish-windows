// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The three seams between the updater coordinator and the outside world:
// the manifest fetch, the payload download, and the staging directory. Abstract
// on purpose (the WakeInhibitor pattern) so the coordinator's schedule,
// gating and backoff are testable against fakes with no sockets and no disk.
//
// Threading contract: a port is called only from the thread that owns it, and
// every callback fires on that same thread. The coordinator owns the manifest
// gateway on the Qt main thread and the download gateway plus the staging store
// on the "dish-update" worker, and marshals results back itself. Fakes handed
// to the test constructor run entirely on the calling thread.

#pragma once

#include "core/reducer/UpdateMachine.h"
#include "core/update/UpdateManifest.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

namespace dish::update {

// Either a validated manifest or the typed reason the check failed. Never both.
struct ManifestFetchResult {
    std::optional<UpdateManifest> manifest;
    // The exact bytes that parsed, snapshotted beside a promoted stage so a
    // support request can show what the client was told at stage time.
    QByteArray body;
    reducer::UpdateError error = reducer::UpdateError::None;

    static ManifestFetchResult ok(UpdateManifest m, QByteArray raw) {
        return ManifestFetchResult{std::move(m), std::move(raw), reducer::UpdateError::None};
    }
    static ManifestFetchResult failed(reducer::UpdateError e) {
        return ManifestFetchResult{std::nullopt, {}, e};
    }
};

class ManifestGateway {
  public:
    using Callback = std::function<void(const ManifestFetchResult&)>;

    virtual ~ManifestGateway() = default;

    // Exactly one callback per accepted fetch. A fetch issued while one is in
    // flight is ignored (the coordinator's phase guard already prevents it).
    virtual void fetch(Callback done) = 0;

    // Best-effort; any in-flight callback is dropped, never delivered late.
    virtual void cancel() = 0;
};

struct DownloadRequest {
    QString url;
    QString sha256;  // expected, for the caller's records; the store re-hashes
    qint64 size = 0; // declared; an overrun past size + 1 MiB aborts
    QString version;
    QString partPath; // absolute; overwritten from zero, never resumed
};

struct DownloadOutcome {
    bool ok = false;
    QString partPath;
    reducer::UpdateError error = reducer::UpdateError::None;
};

class DownloadGateway {
  public:
    using StartedCallback = std::function<void(qint64 total)>;
    using ProgressCallback = std::function<void(qint64 received)>;
    using FinishedCallback = std::function<void(const DownloadOutcome&)>;

    virtual ~DownloadGateway() = default;

    // `started` fires once when the declared length is known, `progress` is
    // already throttled by the implementation, and `finished` fires exactly
    // once per accepted start unless abort() cancelled it first (an abort is
    // the caller's own decision and needs no echo).
    virtual void start(const DownloadRequest& request, StartedCallback started,
                       ProgressCallback progress, FinishedCallback finished) = 0;

    virtual void abort() = 0;
};

// One promoted, marker-complete ready\<version> directory.
struct StagedUpdate {
    QString version;
    QString dir;     // ...\updates\ready\<version>
    QString exePath; // ...\ready\<version>\dish-setup.exe
    QString sha256;  // as recorded in ready.marker
    qint64 size = 0; // as recorded in ready.marker
};

class StagingStore {
  public:
    virtual ~StagingStore() = default;

    // %LOCALAPPDATA%\Dish\updates, forward slashes. "" when LOCALAPPDATA is
    // unset, which disables staging rather than writing next to the exe.
    virtual QString root() const = 0;

    // ...\staging\dish-setup-<version>.exe.part
    virtual QString partPathFor(const QString& version) const = 0;

    // The highest ready\<v> that passes the cheap validation (name parses,
    // marker complete, exe present at the recorded size). nullopt when none.
    virtual std::optional<StagedUpdate> findStaged() = 0;

    // Full re-read hash of the .part, then publish ready\<version> with
    // ready.marker written LAST. Returns the ready directory on success;
    // nullopt means the tree was left sweepable, never half-published.
    virtual std::optional<QString> promote(const QString& version, const QString& sha256,
                                           qint64 size, const QByteArray& manifestBytes) = 0;

    // Delete ready\<version>; tolerated to fail (AV lock), retried next sweep.
    virtual void discard(const QString& version) = 0;

    // The janitor: stale .part files, unparsable or incomplete ready dirs,
    // anything at or below `currentVersion`, and every ready dir but the
    // highest survivor.
    virtual void sweep(const QString& currentVersion) = 0;

    // asset size + 200 MB headroom on the staging volume.
    virtual bool hasRoomFor(qint64 assetSize) const = 0;
};

} // namespace dish::update
