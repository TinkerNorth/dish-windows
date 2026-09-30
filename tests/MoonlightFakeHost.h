// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A Moonlight host on loopback, speaking the four PLAINTEXT pairing phases for
// real, and with serveTls() the fifth over TLS.
//
// Pairing is the one flow where nothing can be concluded from a single call: the
// PIN derives an AES key, each phase verifies the last, and a client that gets
// phase 2 wrong stops at phase 3 rather than reporting anything wrong with
// phase 2. So the only honest way to assert that the exchange holds together is
// to answer it, which is what this does, using the same server-side algorithm
// test_moonlight_pairing.cpp already plays against PairingClient. Nothing about
// MoonlightSession is stubbed: it makes its real HTTP calls, to a real socket,
// and drives its real state machine on what comes back.
//
// It binds LOOPBACK ON AN EPHEMERAL PORT and is handed to the session as the
// host's httpPort. The httpsPort a caller passes must never be the Moonlight
// default: this machine may itself be running Sunshine, and a unit test that
// reaches a live host is a unit test that can change somebody's session.
//
// A refusal is answered the way a host refuses: HTTP 200 carrying a status_code
// of its own, which is how Moonlight says no, or under an HTTP error with the
// same body (refuseWith), which is how Wolf says it. answerWith() answers one
// path with a status line and body of the test's choosing.

#pragma once

#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightPairing.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dish::test {

namespace fake_detail {

namespace mlc = dish::moonlight::crypto;

// The server half of the exchange, mirroring the Wolf reference algorithm. The
// same shape as the FakeServer in test_moonlight_pairing.cpp, driven from the
// query parameters the client actually sent instead of from a fixed vector.
struct PairingServer {
    dish::moonlight::Identity id;
    std::array<std::uint8_t, 16> aesKey{};
    std::vector<std::uint8_t> serverSecret = std::vector<std::uint8_t>(16, 0xCC);
    std::vector<std::uint8_t> serverChallenge = std::vector<std::uint8_t>(16, 0xDD);
    std::vector<std::uint8_t> clientHash;
    std::string clientCertPem;
    bool paired = false;

    void begin(const std::string& saltHex, const std::string& clientCertHex, const std::string& pin,
               const dish::moonlight::Identity& hostIdentity) {
        id = hostIdentity;
        const auto salt = *mlc::hexDecode(saltHex);
        aesKey = mlc::genAesKey(salt.data(), salt.size(), pin);
        const auto pem = *mlc::hexDecode(clientCertHex);
        clientCertPem.assign(pem.begin(), pem.end());
    }

    std::string plaincertHex() const {
        return mlc::hexEncode(mlc::Bytes(id.certPem.begin(), id.certPem.end()));
    }

    std::string challengeResponse(const std::string& clientChallengeHex) {
        const auto blob = *mlc::hexDecode(clientChallengeHex);
        const auto clientChallenge = *mlc::aesEcbDecrypt(aesKey, blob);
        const auto serverSig = *dish::moonlight::certSignature(id.certPem);
        mlc::Bytes hashInput = clientChallenge;
        hashInput.insert(hashInput.end(), serverSig.begin(), serverSig.end());
        hashInput.insert(hashInput.end(), serverSecret.begin(), serverSecret.end());
        const auto hash = mlc::sha256(hashInput);
        mlc::Bytes pt(hash.begin(), hash.end());
        pt.insert(pt.end(), serverChallenge.begin(), serverChallenge.end());
        return mlc::hexEncode(*mlc::aesEcbEncrypt(aesKey, pt));
    }

    std::string pairingSecret(const std::string& serverChallengeRespHex) {
        const auto blob = *mlc::hexDecode(serverChallengeRespHex);
        clientHash = *mlc::aesEcbDecrypt(aesKey, blob);
        const auto sig = *mlc::rsaSign(id.privateKeyPem, serverSecret.data(), serverSecret.size());
        mlc::Bytes out = serverSecret;
        out.insert(out.end(), sig.begin(), sig.end());
        return mlc::hexEncode(out);
    }

    void verifyClient(const std::string& clientPairingSecretHex) {
        const auto blob = *mlc::hexDecode(clientPairingSecretHex);
        if (blob.size() < 16) {
            paired = false;
            return;
        }
        const mlc::Bytes clientSecret(blob.begin(), blob.begin() + 16);
        const mlc::Bytes clientSig(blob.begin() + 16, blob.end());
        const auto certSig = *dish::moonlight::certSignature(clientCertPem);

        mlc::Bytes hashInput = serverChallenge;
        hashInput.insert(hashInput.end(), certSig.begin(), certSig.end());
        hashInput.insert(hashInput.end(), clientSecret.begin(), clientSecret.end());
        const auto hash = mlc::sha256(hashInput);
        if (mlc::Bytes(hash.begin(), hash.end()) != clientHash) {
            paired = false;
            return;
        }
        const auto pub = *dish::moonlight::certPublicKeyPem(clientCertPem);
        paired = mlc::rsaVerify(pub, clientSecret.data(), clientSecret.size(), clientSig.data(),
                                clientSig.size());
    }
};

} // namespace fake_detail

class MoonlightFakeHost : public QObject {
  public:
    // 0 answers every phase; 1..4 refuse that phase; -1 never answers at all,
    // which is how the exchange is held open the way a host holds it open
    // waiting for a human to type the code.
    static constexpr int kRefuseNothing = 0;
    static constexpr int kAnswerNothing = -1;

    explicit MoonlightFakeHost(QString pin, int refusePhase = kRefuseNothing)
        : pin_(std::move(pin)), refusePhase_(refusePhase) {
        server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, this,
                         [this] { accept(server_.nextPendingConnection()); });
    }

    bool listening() const { return server_.isListening(); }
    int port() const { return static_cast<int>(server_.serverPort()); }

    // Answer every request for `path` with this status line and body: Wolf answers a /launch for
    // an app it does not know with HTTP 400 and <root status_code="400"/>.
    void answerWith(const QString& path, int httpStatus, QByteArray body) {
        answeredPath_ = path;
        answeredStatus_ = httpStatus;
        answeredBody_ = std::move(body);
    }

    // Listen for TLS as well, presenting the certificate phase 1 hands out, the way a host does,
    // so phase 5 can complete. False when the TLS backend cannot serve.
    bool serveTls() { return serveTlsWith(hostIdentity()); }

    // Listen for TLS presenting a certificate other than the one phase 1 hands out: what a machine
    // between the client and its host, or another one answering on the host's port, looks like.
    bool serveTlsAsAnotherMachine() { return serveTlsWith(anotherIdentity()); }

    // The certificate phase 1 hands out and serveTls() presents, as DER.
    QByteArray certDer() {
        return QSslCertificate::fromData(QByteArray::fromStdString(hostIdentity().certPem))
            .value(0)
            .toDer();
    }

    int tlsPort() const { return static_cast<int>(tls_.serverPort()); }

    // Which phase numbers were served, in order, so a refused exchange can be
    // shown to have STOPPED rather than merely to have failed at the end.
    const QList<int>& phasesServed() const { return phasesServed_; }
    bool pairedSomebody() const { return pairing_.paired; }

    // The refused phase says `message`, under `httpStatus`: Wolf turns a
    // pairing phase down with HTTP 400 and its reason in the body (fail_pair
    // in its src/moonlight-server/rest/endpoints.hpp).
    void refuseWith(int httpStatus, QString message) {
        refusalStatus_ = httpStatus;
        refusalMessage_ = std::move(message);
    }

  private:
    void accept(QTcpSocket* socket) {
        if (socket == nullptr) { return; }
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray line = socket->readLine();
            if (line.isEmpty()) { return; }
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() < 2) { return; }
            const QUrl url(QString::fromUtf8(parts.at(1)));
            const QUrlQuery query(url.query());
            const bool answeredAsSet = url.path() == answeredPath_;
            const QByteArray body = answeredAsSet ? answeredBody_ : replyFor(url.path(), query);
            if (body.isNull()) { return; } // hold the request open, answer nothing
            const int status = answeredAsSet ? answeredStatus_ : statusFor(url.path(), query);
            const QByteArray head = "HTTP/1.1 " + QByteArray::number(status) +
                                    " X\r\nContent-Type: application/xml\r\nContent-Length: " +
                                    QByteArray::number(body.size()) +
                                    "\r\nConnection: close\r\n\r\n";
            socket->write(head + body);
            socket->flush();
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    static QByteArray ok(const QString& tag, const std::string& hex) {
        return QStringLiteral("<root status_code=\"200\"><%1>%2</%1></root>")
            .arg(tag, QString::fromStdString(hex))
            .toUtf8();
    }

    // How a Moonlight host says no: HTTP 200, with the refusal in the body.
    static QByteArray refused(const QString& why) {
        return QStringLiteral("<root status_code=\"400\" status_message=\"%1\"></root>")
            .arg(why)
            .toUtf8();
    }

    QByteArray replyFor(const QString& path, const QUrlQuery& query) {
        if (refusePhase_ == kAnswerNothing) { return {}; }
        if (path == QLatin1String("/serverinfo")) {
            // PairStatus 0, as a live Sunshine host answers every plaintext
            // caller, including one it is holding a pairing for.
            return QStringLiteral("<root status_code=\"200\"><hostname>Fake</hostname>"
                                  "<uniqueid>%1</uniqueid><PairStatus>0</PairStatus></root>")
                .arg(uniqueId_)
                .toUtf8();
        }
        const int phase = phaseOf(query);
        if (phase == 0) { return refused(QStringLiteral("Invalid request")); }
        phasesServed_.append(phase);
        if (phase == refusePhase_) { return refused(refusalMessage_); }

        switch (phase) {
        case 1:
            pairing_.begin(query.queryItemValue(QStringLiteral("salt")).toStdString(),
                           query.queryItemValue(QStringLiteral("clientcert")).toStdString(),
                           pin_.toStdString(), hostIdentity());
            return ok(QStringLiteral("plaincert"), pairing_.plaincertHex());
        case 2:
            return ok(QStringLiteral("challengeresponse"),
                      pairing_.challengeResponse(
                          query.queryItemValue(QStringLiteral("clientchallenge")).toStdString()));
        case 3:
            return ok(
                QStringLiteral("pairingsecret"),
                pairing_.pairingSecret(
                    query.queryItemValue(QStringLiteral("serverchallengeresp")).toStdString()));
        case 4:
            pairing_.verifyClient(
                query.queryItemValue(QStringLiteral("clientpairingsecret")).toStdString());
            return pairedBody();
        default:
            // Phase 5 arrives only over TLS: reaching it at all is what it proves.
            return pairedBody();
        }
    }

    // The status line a reply goes out under: the refusal's for the refused
    // phase, and 200 for everything else.
    int statusFor(const QString& path, const QUrlQuery& query) const {
        const bool refusing = path == QLatin1String("/pair") && refusePhase_ > kRefuseNothing &&
                              phaseOf(query) == refusePhase_;
        return refusing ? refusalStatus_ : kHttpOk;
    }

    QByteArray pairedBody() const {
        return QStringLiteral("<root status_code=\"200\"><paired>%1</paired></root>")
            .arg(pairing_.paired ? 1 : 0)
            .toUtf8();
    }

    static int phaseOf(const QUrlQuery& query) {
        const QString phrase = query.queryItemValue(QStringLiteral("phrase"));
        if (phrase == QLatin1String("getservercert")) { return 1; }
        if (query.hasQueryItem(QStringLiteral("clientchallenge"))) { return 2; }
        if (query.hasQueryItem(QStringLiteral("serverchallengeresp"))) { return 3; }
        if (query.hasQueryItem(QStringLiteral("clientpairingsecret"))) { return 4; }
        if (phrase == QLatin1String("pairchallenge")) { return 5; }
        return 0;
    }

    static constexpr int kHttpOk = 200;

    // One identity for the host, made the first time something needs it: the certificate phase 1
    // hands out is the one its TLS port presents.
    const dish::moonlight::Identity& hostIdentity() {
        if (!hostIdentity_.has_value()) { hostIdentity_ = dish::moonlight::generateIdentity(); }
        return *hostIdentity_;
    }

    const dish::moonlight::Identity& anotherIdentity() {
        if (!anotherIdentity_.has_value()) {
            anotherIdentity_ = dish::moonlight::generateIdentity();
        }
        return *anotherIdentity_;
    }

    bool serveTlsWith(const dish::moonlight::Identity& identity) {
        const auto certs = QSslCertificate::fromData(QByteArray::fromStdString(identity.certPem));
        const QSslKey key(QByteArray::fromStdString(identity.privateKeyPem), QSsl::Rsa, QSsl::Pem);
        if (certs.isEmpty() || key.isNull()) { return false; }
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setLocalCertificate(certs.first());
        ssl.setPrivateKey(key);
        // The client presents its own certificate; asking for it back would only add a way for
        // the fixture to refuse a valid call.
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        tls_.setSslConfiguration(ssl);
        QObject::connect(&tls_, &QTcpServer::pendingConnectionAvailable, this, [this] {
            while (auto* socket = tls_.nextPendingConnection()) { accept(socket); }
        });
        return tls_.listen(QHostAddress::LocalHost, 0);
    }

    QTcpServer server_;
    QSslServer tls_;
    std::optional<dish::moonlight::Identity> hostIdentity_;
    std::optional<dish::moonlight::Identity> anotherIdentity_;
    QString pin_;
    int refusePhase_;
    // Unless refuseWith says otherwise, the refusal rides an HTTP 200.
    int refusalStatus_ = kHttpOk;
    QString refusalMessage_ = QStringLiteral("Invalid uniqueid");
    QString answeredPath_;
    int answeredStatus_ = 0;
    QByteArray answeredBody_;
    QString uniqueId_ = QStringLiteral("FAKEHOST-0001");
    fake_detail::PairingServer pairing_;
    QList<int> phasesServed_;
};

} // namespace dish::test
