// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The two QNetworkAccessManager gateways behind the updater's ports. Both use
// Qt's DEFAULT certificate validation (Schannel over the Windows root store)
// and the default NoLessSafeRedirectPolicy, which follows GitHub's 302 to the
// download CDN without ever downgrading to http.
//
// net::HTTPClient / net::PairingClient MUST NOT carry this traffic: they set
// QSslSocket::VerifyNone deliberately (TOFU pinning for satellites on a LAN),
// which is exactly wrong for a public host. That is why these two own dedicated
// managers rather than borrowing the app's.
//
// A QNetworkAccessManager belongs to the thread that created it, so the
// download gateway is constructed ON the "dish-update" worker and the manifest
// gateway on the main thread.

#pragma once

#include "update/UpdatePorts.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace dish::update {

// The User-Agent both gateways send: product, version, platform. No device id,
// no account data, nothing else (PRIVACY.md 2.4).
QString updateUserAgent();

class HttpManifestGateway : public QObject, public ManifestGateway {
    Q_OBJECT
  public:
    explicit HttpManifestGateway(QObject* parent = nullptr);
    ~HttpManifestGateway() override;

    void fetch(Callback done) override;
    void cancel() override;

    // Test seam: the permalink is the only URL this ever requests in
    // production, but a fixture server needs to be reachable too.
    void setUrl(const QString& url) { url_ = url; }

  private:
    void finish(const ManifestFetchResult& result);

    QNetworkAccessManager* nam_ = nullptr;
    QPointer<QNetworkReply> reply_;
    Callback done_;
    QString url_;
};

class HttpDownloadGateway : public QObject, public DownloadGateway {
    Q_OBJECT
  public:
    explicit HttpDownloadGateway(QObject* parent = nullptr);
    ~HttpDownloadGateway() override;

    void start(const DownloadRequest& request, StartedCallback started, ProgressCallback progress,
               FinishedCallback finished) override;
    void abort() override;

  private:
    void onReadyRead();
    void onFinished();
    void onStallCheck();
    void teardown();
    void fail(reducer::UpdateError error);

    QNetworkAccessManager* nam_ = nullptr;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QFile> file_;
    QTimer* stallTimer_ = nullptr;
    QElapsedTimer sinceProgress_;
    QElapsedTimer sinceEmit_;
    // Early truncation / substitution detection while the bytes stream. The
    // promote still re-reads the finished file: this hash proves what arrived,
    // that one proves what survived to disk.
    QCryptographicHash hash_{QCryptographicHash::Sha256};

    DownloadRequest request_;
    StartedCallback started_;
    ProgressCallback progress_;
    FinishedCallback finished_;

    qint64 received_ = 0;
    qint64 lastEmitted_ = -1;
    bool startedEmitted_ = false;
    bool done_ = false;
    bool aborting_ = false;
};

} // namespace dish::update
