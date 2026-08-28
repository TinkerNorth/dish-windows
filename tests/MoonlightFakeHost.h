// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A Moonlight host on loopback, speaking the four PLAINTEXT pairing phases for
// real.
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
// of its own, which is how Moonlight says no.

#pragma once

#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightPairing.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <array>
#include <cstdint>
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

    void begin(const std::string& saltHex, const std::string& clientCertHex,
               const std::string& pin) {
        id = *dish::moonlight::generateIdentity();
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

    // Which phase numbers were served, in order, so a refused exchange can be
    // shown to have STOPPED rather than merely to have failed at the end.
    const QList<int>& phasesServed() const { return phasesServed_; }
    bool pairedSomebody() const { return pairing_.paired; }

  private:
    void accept(QTcpSocket* socket) {
        if (socket == nullptr) { return; }
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray line = socket->readLine();
            if (line.isEmpty()) { return; }
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() < 2) { return; }
            const QUrl url(QString::fromUtf8(parts.at(1)));
            const QByteArray body = replyFor(url.path(), QUrlQuery(url.query()));
            if (body.isNull()) { return; } // hold the request open, answer nothing
            const QByteArray head =
                "HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nContent-Length: " +
                QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
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
        if (phase == refusePhase_) { return refused(QStringLiteral("Invalid uniqueid")); }

        switch (phase) {
        case 1:
            pairing_.begin(query.queryItemValue(QStringLiteral("salt")).toStdString(),
                           query.queryItemValue(QStringLiteral("clientcert")).toStdString(),
                           pin_.toStdString());
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
        default:
            pairing_.verifyClient(
                query.queryItemValue(QStringLiteral("clientpairingsecret")).toStdString());
            return QStringLiteral("<root status_code=\"200\"><paired>%1</paired></root>")
                .arg(pairing_.paired ? 1 : 0)
                .toUtf8();
        }
    }

    static int phaseOf(const QUrlQuery& query) {
        if (query.queryItemValue(QStringLiteral("phrase")) == QLatin1String("getservercert")) {
            return 1;
        }
        if (query.hasQueryItem(QStringLiteral("clientchallenge"))) { return 2; }
        if (query.hasQueryItem(QStringLiteral("serverchallengeresp"))) { return 3; }
        if (query.hasQueryItem(QStringLiteral("clientpairingsecret"))) { return 4; }
        return 0;
    }

    QTcpServer server_;
    QString pin_;
    int refusePhase_;
    QString uniqueId_ = QStringLiteral("FAKEHOST-0001");
    fake_detail::PairingServer pairing_;
    QList<int> phasesServed_;
};

} // namespace dish::test
