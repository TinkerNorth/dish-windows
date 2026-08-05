// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "update/HttpGateways.h"

#include "core/update/UpdateManifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace dish::update {

namespace {

// A transport failure that means "this machine cannot reach the internet right
// now" rather than "GitHub said no". The distinction only changes the copy in
// Settings; both back off identically.
reducer::UpdateError classify(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return reducer::UpdateError::Offline;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        return reducer::UpdateError::Stalled;
    default:
        return reducer::UpdateError::Http;
    }
}

void applyCommonRequestPolicy(QNetworkRequest& request) {
    request.setRawHeader(QByteArrayLiteral("User-Agent"), updateUserAgent().toUtf8());
    // https-only redirects. This is Qt 6's default; stated explicitly because
    // the whole design leans on the 302 to the download CDN being safe.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
}

QNetworkAccessManager* makeManager(QObject* parent) {
    auto* nam = new QNetworkAccessManager(parent);
    // No cookie jar and no cache: an update check is one anonymous GET, and
    // there is nothing about it worth remembering between runs.
    nam->setCookieJar(nullptr);
    nam->setAutoDeleteReplies(false);
    return nam;
}

} // namespace

QString updateUserAgent() {
    return QStringLiteral("Dish/%1 (Windows; x64)").arg(QLatin1String(DISH_VERSION));
}

// ── Manifest ────────────────────────────────────────────────────────────────

HttpManifestGateway::HttpManifestGateway(QObject* parent)
    : QObject(parent), nam_(makeManager(this)), url_(QLatin1String(kLatestManifestUrl)) {}

HttpManifestGateway::~HttpManifestGateway() { cancel(); }

void HttpManifestGateway::fetch(Callback done) {
    if (!reply_.isNull()) { return; }
    done_ = std::move(done);

    QNetworkRequest request{QUrl(url_)};
    applyCommonRequestPolicy(request);
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    // The manifest is ~1 KB: 30 s without a byte is a dead link, not a slow one.
    request.setTransferTimeout(30'000);

    QNetworkReply* reply = nam_->get(request);
    reply_ = reply;

    // A captive portal can answer with an arbitrarily large splash page. Cut it
    // off at the cap rather than buffering it just to reject it later.
    QObject::connect(reply, &QNetworkReply::downloadProgress, this,
                     [this](qint64 receivedBytes, qint64) {
                         if (receivedBytes > kManifestMaxBytes && !reply_.isNull()) {
                             reply_->abort();
                         }
                     });
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply_ != reply) { return; }
        reply_.clear();
        if (reply->error() != QNetworkReply::NoError) {
            finish(ManifestFetchResult::failed(classify(reply->error())));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            // 404 is the ordinary publish-window case: transient, backed off,
            // self-healing the moment the asset appears.
            finish(ManifestFetchResult::failed(reducer::UpdateError::Http));
            return;
        }
        const QByteArray body = reply->readAll();
        const auto parsed = UpdateManifest::parse(body);
        if (const auto* manifest = std::get_if<UpdateManifest>(&parsed)) {
            finish(ManifestFetchResult::ok(*manifest, body));
            return;
        }
        finish(ManifestFetchResult::failed(reducer::UpdateError::ManifestInvalid));
    });
}

void HttpManifestGateway::cancel() {
    done_ = {};
    if (!reply_.isNull()) {
        QNetworkReply* reply = reply_.data();
        reply_.clear();
        reply->abort();
    }
}

void HttpManifestGateway::finish(const ManifestFetchResult& result) {
    Callback callback;
    callback.swap(done_);
    if (callback) { callback(result); }
}

// ── Payload download ────────────────────────────────────────────────────────

HttpDownloadGateway::HttpDownloadGateway(QObject* parent)
    : QObject(parent), nam_(makeManager(this)) {}

HttpDownloadGateway::~HttpDownloadGateway() { abort(); }

void HttpDownloadGateway::start(const DownloadRequest& request, StartedCallback started,
                                ProgressCallback progress, FinishedCallback finished) {
    if (!reply_.isNull()) { return; }

    request_ = request;
    started_ = std::move(started);
    progress_ = std::move(progress);
    finished_ = std::move(finished);
    received_ = 0;
    lastEmitted_ = -1;
    startedEmitted_ = false;
    done_ = false;
    aborting_ = false;
    hash_.reset();

    QDir().mkpath(QFileInfo(request_.partPath).absolutePath());
    file_ = std::make_unique<QFile>(request_.partPath);
    // Truncate, never append: there is no resume, so a survivor from an earlier
    // attempt is bytes of unknown provenance.
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(reducer::UpdateError::Io);
        return;
    }

    QNetworkRequest netRequest{QUrl(request_.url)};
    applyCommonRequestPolicy(netRequest);

    QNetworkReply* reply = nam_->get(netRequest);
    reply_ = reply;
    sinceProgress_.start();
    sinceEmit_.start();

    QObject::connect(reply, &QNetworkReply::readyRead, this, &HttpDownloadGateway::onReadyRead);
    QObject::connect(reply, &QNetworkReply::finished, this, &HttpDownloadGateway::onFinished);
    // The declared length comes from the signal rather than the Content-Length
    // header accessor, which is deprecated in newer Qt and would fail /WX.
    QObject::connect(reply, &QNetworkReply::downloadProgress, this,
                     [this](qint64, qint64 bytesTotal) {
                         if (startedEmitted_ || bytesTotal <= 0) { return; }
                         startedEmitted_ = true;
                         if (started_) { started_(bytesTotal); }
                     });

    // The watchdog, not QNetworkRequest::setTransferTimeout: a 40 MB body over
    // a congested link may legitimately go quiet for a while, and 60 s is the
    // budget this design gives it.
    stallTimer_ = new QTimer(this);
    stallTimer_->setInterval(5'000);
    QObject::connect(stallTimer_, &QTimer::timeout, this, &HttpDownloadGateway::onStallCheck);
    stallTimer_->start();
}

void HttpDownloadGateway::onReadyRead() {
    if (reply_.isNull() || !file_ || done_) { return; }
    const QByteArray chunk = reply_->readAll();
    if (chunk.isEmpty()) { return; }

    received_ += chunk.size();
    if (request_.size > 0 && received_ > request_.size + reducer::kOverrunAllowanceBytes) {
        // More bytes than the manifest described: whatever this is, it is not
        // the asset that was signed off on.
        fail(reducer::UpdateError::Corrupt);
        return;
    }
    if (file_->write(chunk) != chunk.size()) {
        fail(reducer::UpdateError::Io);
        return;
    }
    hash_.addData(chunk);
    sinceProgress_.restart();

    if (!startedEmitted_) {
        // No Content-Length (chunked): fall back to the manifest's declared
        // size so the progress bar is determinate either way.
        startedEmitted_ = true;
        if (started_) { started_(request_.size); }
    }
    // Throttled to 100 ms or one percent, whichever comes first: the reducer is
    // cheap but the QML rebind is not.
    const qint64 onePercent = request_.size > 0 ? request_.size / 100 : 0;
    const bool bigEnough = onePercent > 0 && received_ - lastEmitted_ >= onePercent;
    if (sinceEmit_.elapsed() >= 100 || bigEnough || lastEmitted_ < 0) {
        lastEmitted_ = received_;
        sinceEmit_.restart();
        if (progress_) { progress_(received_); }
    }
}

void HttpDownloadGateway::onStallCheck() {
    if (done_ || reply_.isNull()) { return; }
    if (sinceProgress_.elapsed() >= reducer::kStallTimeoutMs) {
        fail(reducer::UpdateError::Stalled);
    }
}

void HttpDownloadGateway::onFinished() {
    if (done_ || reply_.isNull()) { return; }
    QNetworkReply* reply = reply_.data();
    // Drain whatever readyRead has not been dispatched yet.
    onReadyRead();
    if (done_) { return; }

    if (reply->error() != QNetworkReply::NoError) {
        fail(classify(reply->error()));
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
        fail(reducer::UpdateError::Http);
        return;
    }
    if (request_.size > 0 && received_ != request_.size) {
        // A short body that still reported success is a truncation.
        fail(reducer::UpdateError::Corrupt);
        return;
    }
    const QString streamed = QString::fromLatin1(hash_.result().toHex());
    if (!request_.sha256.isEmpty() && streamed.compare(request_.sha256, Qt::CaseInsensitive) != 0) {
        fail(reducer::UpdateError::Corrupt);
        return;
    }

    const QString partPath = request_.partPath;
    done_ = true;
    if (file_) { file_->close(); }
    teardown();
    FinishedCallback callback;
    callback.swap(finished_);
    if (callback) { callback(DownloadOutcome{true, partPath, reducer::UpdateError::None}); }
}

void HttpDownloadGateway::fail(reducer::UpdateError error) {
    if (done_) { return; }
    done_ = true;
    if (file_) {
        file_->close();
        // Nothing partial is ever kept: the next attempt starts from zero.
        (void)QFile::remove(request_.partPath);
    }
    teardown();
    FinishedCallback callback;
    callback.swap(finished_);
    if (callback) { callback(DownloadOutcome{false, {}, error}); }
}

void HttpDownloadGateway::abort() {
    if (done_) {
        teardown();
        return;
    }
    done_ = true;
    aborting_ = true;
    finished_ = {};
    if (file_) {
        file_->close();
        (void)QFile::remove(request_.partPath);
    }
    teardown();
}

void HttpDownloadGateway::teardown() {
    if (stallTimer_ != nullptr) {
        stallTimer_->stop();
        stallTimer_->deleteLater();
        stallTimer_ = nullptr;
    }
    if (!reply_.isNull()) {
        QNetworkReply* reply = reply_.data();
        reply_.clear();
        QObject::disconnect(reply, nullptr, this, nullptr);
        if (aborting_) { reply->abort(); }
        reply->deleteLater();
    }
    file_.reset();
    started_ = {};
    progress_ = {};
}

} // namespace dish::update
