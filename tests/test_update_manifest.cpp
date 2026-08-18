// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// latest.json (schema 1). The golden document is the one from spec 16.1,
// verbatim; every other case is one rule of the validation list, because this
// parser is the ONLY thing standing between a captive portal's HTML (or a
// lookalike host) and a 40 MB download the client will later execute.

#include "core/update/UpdateManifest.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <variant>

using dish::update::kAssetMaxBytes;
using dish::update::kAssetUrlPrefix;
using dish::update::kLatestManifestUrl;
using dish::update::kManifestMaxBytes;
using dish::update::kSetupAssetName;
using dish::update::ManifestError;
using dish::update::UpdateManifest;

namespace {

const QByteArray kSha = QByteArray(64, 'a');
const QByteArray kZipSha = QByteArray(64, 'b');

// Spec 16.1's example, byte for byte in structure.
QByteArray golden() {
    return "{\n"
           "  \"schema\": 1,\n"
           "  \"product\": \"dish-windows\",\n"
           "  \"version\": \"0.2.0\",\n"
           "  \"channel\": \"stable\",\n"
           "  \"publishedAt\": \"2026-08-03T14:21:07Z\",\n"
           "  \"minimumSupportedVersion\": \"0.1.0\",\n"
           "  \"releaseNotesUrl\": \"https://github.com/TinkerNorth/dish-windows/releases/tag/"
           "0.2.0\",\n"
           "  \"assets\": {\n"
           "    \"dish-setup.exe\": { \"url\": \"https://github.com/TinkerNorth/dish-windows/"
           "releases/download/0.2.0/dish-setup.exe\", \"sha256\": \"" +
           kSha +
           "\", \"size\": 41943040 },\n"
           "    \"dish-windows.zip\": { \"url\": \"https://github.com/TinkerNorth/dish-windows/"
           "releases/download/0.2.0/dish-windows.zip\", \"sha256\": \"" +
           kZipSha +
           "\", \"size\": 52428800 }\n"
           "  }\n"
           "}\n";
}

// The golden document with one field replaced, so each case differs from a
// KNOWN-GOOD manifest by exactly the rule it is testing.
QByteArray withField(const QByteArray& before, const QByteArray& after) {
    QByteArray json = golden();
    const qsizetype at = json.indexOf(before);
    if (at < 0) { return "MISSING FIELD: " + before; }
    json.replace(before, after);
    return json;
}

ManifestError errorOf(const QByteArray& body) {
    const auto result = UpdateManifest::parse(body);
    REQUIRE(std::holds_alternative<ManifestError>(result));
    return std::get<ManifestError>(result);
}

UpdateManifest parsedOk(const QByteArray& body) {
    const auto result = UpdateManifest::parse(body);
    REQUIRE(std::holds_alternative<UpdateManifest>(result));
    return std::get<UpdateManifest>(result);
}

} // namespace

TEST_CASE("update manifest: the fetch target and the caps are the contract", "[update][manifest]") {
    CHECK(QString::fromLatin1(kLatestManifestUrl) ==
          QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/latest/download/"
                         "latest.json"));
    CHECK(QString::fromLatin1(kAssetUrlPrefix) ==
          QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/download/"));
    CHECK(QString::fromLatin1(kSetupAssetName) == QStringLiteral("dish-setup.exe"));
    CHECK(kManifestMaxBytes == 64 * 1024);
    CHECK(kAssetMaxBytes == qint64(500) * 1024 * 1024);
}

TEST_CASE("update manifest: the golden document parses field by field", "[update][manifest]") {
    const UpdateManifest m = parsedOk(golden());
    CHECK(m.schema == 1);
    CHECK(m.product == QStringLiteral("dish-windows"));
    CHECK(m.version == QStringLiteral("0.2.0"));
    CHECK(m.channel == QStringLiteral("stable"));
    // Display only, carried verbatim: no ordering anywhere reads it.
    CHECK(m.publishedAt == QStringLiteral("2026-08-03T14:21:07Z"));
    CHECK(m.minimumSupportedVersion == QStringLiteral("0.1.0"));
    CHECK(m.releaseNotesUrl ==
          QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/tag/0.2.0"));
    CHECK(m.setupAsset.url ==
          QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/download/0.2.0/"
                         "dish-setup.exe"));
    CHECK(m.setupAsset.sha256 == QString::fromLatin1(kSha));
    CHECK(m.setupAsset.size == 41943040);
    // The zip asset is deliberately ignored: the updater never consumes it.
}

TEST_CASE("update manifest: unknown fields are ignored (additive-only schema 1)",
          "[update][manifest]") {
    QByteArray json = golden();
    json.replace("\"schema\": 1,", "\"schema\": 1, \"futureField\": {\"a\": [1,2,3]},");
    const UpdateManifest m = parsedOk(json);
    CHECK(m.version == QStringLiteral("0.2.0"));
}

TEST_CASE("update manifest: a body over 64 KiB is refused before the JSON parser sees it",
          "[update][manifest]") {
    // A captive portal splash can be arbitrarily large, and the parser must not
    // be the thing that decides how much of it to read.
    QByteArray oversize = golden();
    oversize.append(QByteArray(kManifestMaxBytes, ' '));
    CHECK(errorOf(oversize) == ManifestError::Oversize);

    QByteArray justUnder = golden();
    justUnder.append(QByteArray(kManifestMaxBytes - justUnder.size(), ' '));
    CHECK(justUnder.size() == kManifestMaxBytes);
    CHECK(std::holds_alternative<UpdateManifest>(UpdateManifest::parse(justUnder)));
}

TEST_CASE("update manifest: portal HTML and non-objects are BadJson", "[update][manifest]") {
    CHECK(errorOf("<!DOCTYPE html><html><body>Sign in to WiFi</body></html>") ==
          ManifestError::BadJson);
    CHECK(errorOf("[]") == ManifestError::BadJson);
    CHECK(errorOf("null") == ManifestError::BadJson);
    CHECK(errorOf("{\"schema\": 1,") == ManifestError::BadJson);
    CHECK(errorOf(QByteArray()) == ManifestError::BadJson);
}

TEST_CASE("update manifest: a newer schema means a newer client is required",
          "[update][manifest]") {
    CHECK(errorOf(withField("\"schema\": 1,", "\"schema\": 2,")) ==
          ManifestError::UnsupportedSchema);
    CHECK(errorOf(withField("\"schema\": 1,", "\"schema\": 0,")) ==
          ManifestError::UnsupportedSchema);
    CHECK(errorOf(withField("\"schema\": 1,", "\"schema\": \"1\",")) ==
          ManifestError::UnsupportedSchema);
    CHECK(errorOf(withField("\"schema\": 1,", "\"notSchema\": 1,")) ==
          ManifestError::UnsupportedSchema);
}

TEST_CASE("update manifest: the product and channel are pinned", "[update][manifest]") {
    CHECK(errorOf(withField("\"product\": \"dish-windows\"", "\"product\": \"dish-android\"")) ==
          ManifestError::WrongProduct);
    CHECK(errorOf(withField("\"product\": \"dish-windows\"", "\"product\": \"\"")) ==
          ManifestError::WrongProduct);
    CHECK(errorOf(withField("\"channel\": \"stable\"", "\"channel\": \"beta\"")) ==
          ManifestError::WrongChannel);
    CHECK(errorOf(withField("\"channel\": \"stable\"", "\"channel\": \"Stable\"")) ==
          ManifestError::WrongChannel);
}

TEST_CASE("update manifest: the version must be a bare triple", "[update][manifest]") {
    CHECK(errorOf(withField("\"version\": \"0.2.0\"", "\"version\": \"v0.2.0\"")) ==
          ManifestError::BadVersion);
    CHECK(errorOf(withField("\"version\": \"0.2.0\"", "\"version\": \"0.2.0-rc1\"")) ==
          ManifestError::BadVersion);
    CHECK(errorOf(withField("\"version\": \"0.2.0\"", "\"version\": \"0.2\"")) ==
          ManifestError::BadVersion);
    CHECK(errorOf(withField("\"version\": \"0.2.0\"", "\"version\": 2")) ==
          ManifestError::BadVersion);
}

TEST_CASE("update manifest: the supported minimum is validated and bounded by the version",
          "[update][manifest]") {
    CHECK(errorOf(withField("\"minimumSupportedVersion\": \"0.1.0\"",
                            "\"minimumSupportedVersion\": \"latest\"")) ==
          ManifestError::BadMinimum);
    // A minimum ABOVE the offered version would mark every client unsupported
    // with nothing to upgrade to.
    CHECK(errorOf(withField("\"minimumSupportedVersion\": \"0.1.0\"",
                            "\"minimumSupportedVersion\": \"0.3.0\"")) ==
          ManifestError::BadMinimum);
    // Equal to the version is legitimate: everything older is unsupported.
    CHECK(parsedOk(withField("\"minimumSupportedVersion\": \"0.1.0\"",
                             "\"minimumSupportedVersion\": \"0.2.0\""))
              .minimumSupportedVersion == QStringLiteral("0.2.0"));
}

TEST_CASE("update manifest: the release-notes link is advisory and dropped when unsafe",
          "[update][manifest]") {
    const auto notes = [](const QByteArray& url) {
        return parsedOk(
                   withField("\"releaseNotesUrl\": \"https://github.com/TinkerNorth/dish-windows/"
                             "releases/tag/0.2.0\"",
                             "\"releaseNotesUrl\": \"" + url + "\""))
            .releaseNotesUrl;
    };
    CHECK(notes("https://github.com/TinkerNorth/dish-windows/releases") ==
          QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases"));
    // A bad link must never take the whole check down; it is dropped instead.
    CHECK(notes("http://github.com/TinkerNorth/dish-windows/releases").isEmpty());
    CHECK(notes("https://evil.example/notes").isEmpty());
    CHECK(notes("https://github.com.evil.example/notes").isEmpty());
    CHECK(notes("javascript:alert(1)").isEmpty());
    CHECK(notes("not a url").isEmpty());
}

TEST_CASE("update manifest: the setup asset is required", "[update][manifest]") {
    QByteArray noAssets = golden();
    const qsizetype at = noAssets.indexOf("\"assets\"");
    REQUIRE(at > 0);
    noAssets = noAssets.left(at) + "\"assets\": {}\n}\n";
    CHECK(errorOf(noAssets) == ManifestError::MissingSetupAsset);

    CHECK(errorOf(withField("\"dish-setup.exe\":", "\"dish-setup-x86.exe\":")) ==
          ManifestError::MissingSetupAsset);
}

TEST_CASE("update manifest: the asset URL must be on the release download prefix",
          "[update][manifest]") {
    const QByteArray goodUrl = "https://github.com/TinkerNorth/dish-windows/releases/download/"
                               "0.2.0/dish-setup.exe";
    const auto withUrl = [&goodUrl](const QByteArray& url) {
        return errorOf(withField("\"url\": \"" + goodUrl + "\"", "\"url\": \"" + url + "\""));
    };
    CHECK(withUrl("http://github.com/TinkerNorth/dish-windows/releases/download/0.2.0/"
                  "dish-setup.exe") == ManifestError::BadAssetUrl);
    // A lookalike host: plain prefix comparison is what catches it, before any
    // URL normalization can make it look benign.
    CHECK(withUrl("https://github.com.evil.example/TinkerNorth/dish-windows/releases/download/"
                  "0.2.0/dish-setup.exe") == ManifestError::BadAssetUrl);
    CHECK(withUrl("https://raw.githubusercontent.com/TinkerNorth/dish-windows/0.2.0/"
                  "dish-setup.exe") == ManifestError::BadAssetUrl);
    CHECK(withUrl("https://github.com/SomeoneElse/dish-windows/releases/download/0.2.0/"
                  "dish-setup.exe") == ManifestError::BadAssetUrl);
    CHECK(withUrl("") == ManifestError::BadAssetUrl);
}

TEST_CASE("update manifest: the asset hash must be 64 lowercase hex", "[update][manifest]") {
    const auto withSha = [](const QByteArray& sha) {
        QByteArray json = golden();
        json.replace("\"sha256\": \"" + kSha + "\"", "\"sha256\": \"" + sha + "\"");
        return errorOf(json);
    };
    CHECK(withSha(QByteArray(63, 'a')) == ManifestError::BadSha);
    CHECK(withSha(QByteArray(65, 'a')) == ManifestError::BadSha);
    CHECK(withSha(QByteArray(64, 'A')) == ManifestError::BadSha);
    CHECK(withSha(QByteArray(64, 'z')) == ManifestError::BadSha);
    CHECK(withSha(QByteArray()) == ManifestError::BadSha);
}

TEST_CASE("update manifest: the asset size must be inside (0, 500 MB)", "[update][manifest]") {
    const auto withSize = [](const QByteArray& size) {
        return errorOf(withField("\"size\": 41943040", "\"size\": " + size));
    };
    CHECK(withSize("0") == ManifestError::BadSize);
    CHECK(withSize("-1") == ManifestError::BadSize);
    CHECK(withSize("524288000") == ManifestError::BadSize); // exactly 500 MB is out
    CHECK(withSize("999999999999") == ManifestError::BadSize);
    CHECK(withSize("\"41943040\"") == ManifestError::BadSize);
    CHECK(parsedOk(withField("\"size\": 41943040", "\"size\": 1")).setupAsset.size == 1);
}

TEST_CASE("update manifest: a size JSON can express but qint64 cannot is refused",
          "[update][manifest]") {
    // JSON numbers are doubles, so a manifest can name a size no qint64 holds.
    // `static_cast<qint64>` of an out-of-range double is not defined by the
    // standard; on MSVC x64 it saturates to INT64_MIN, which the `<= 0` bound
    // then rejects. The REQUIREMENT is the rejection, so pin that rather than
    // the mechanism: a toolchain whose saturation differs must still not hand a
    // nonsense size to the downloader.
    const auto withSize = [](const QByteArray& size) {
        return errorOf(withField("\"size\": 41943040", "\"size\": " + size));
    };
    CHECK(withSize("1e300") == ManifestError::BadSize);
    CHECK(withSize("1e18") == ManifestError::BadSize); // > 500 MB, still finite
    CHECK(withSize("-1e300") == ManifestError::BadSize);
    CHECK(withSize("1e-300") == ManifestError::BadSize); // truncates to 0
    CHECK(withSize("9223372036854775808") == ManifestError::BadSize);
}

TEST_CASE("update manifest: whatever follows the pinned prefix, the host stays github.com",
          "[update][manifest]") {
    // The prefix compare is a plain string test, so the tail can be anything —
    // including control characters a naive HTTP client might turn into extra
    // request lines. What makes that safe is that the entry host is already
    // fixed by the prefix and QUrl percent-encodes the rest, so no header can be
    // smuggled and no other scheme or authority can be reached.
    const QByteArray goodUrl = "https://github.com/TinkerNorth/dish-windows/releases/download/"
                               "0.2.0/dish-setup.exe";
    const auto urlOf = [&goodUrl](const QByteArray& url) {
        return parsedOk(withField("\"url\": \"" + goodUrl + "\"", "\"url\": \"" + url + "\""))
            .setupAsset.url;
    };
    const QUrl crlf(urlOf("https://github.com/TinkerNorth/dish-windows/releases/download/"
                          "v1\\r\\nX-Evil: 1/dish-setup.exe"));
    CHECK(crlf.isValid());
    CHECK(crlf.scheme() == QStringLiteral("https"));
    CHECK(crlf.host() == QStringLiteral("github.com"));
    CHECK(QString::fromLatin1(crlf.toEncoded()).contains(QStringLiteral("%0D%0A")));
    CHECK_FALSE(QString::fromLatin1(crlf.toEncoded()).contains(QLatin1Char('\r')));

    // The shapes that would take the download off HTTPS entirely never get past
    // the prefix in the first place.
    const auto refused = [&goodUrl](const QByteArray& url) {
        return errorOf(withField("\"url\": \"" + goodUrl + "\"", "\"url\": \"" + url + "\"")) ==
               ManifestError::BadAssetUrl;
    };
    CHECK(refused("file:///C:/Windows/System32/calc.exe"));
    CHECK(refused("\\\\\\\\evil.example\\\\share\\\\dish-setup.exe")); // UNC
    CHECK(refused("//evil.example/dish-setup.exe"));                   // scheme-relative
    CHECK(refused("HTTPS://GITHUB.COM/TinkerNorth/dish-windows/releases/download/1/x.exe"));
}

TEST_CASE("update manifest: asset equality is field-wise", "[update][manifest]") {
    const UpdateManifest a = parsedOk(golden());
    UpdateManifest b = a;
    CHECK(a == b);
    b.setupAsset.size += 1;
    CHECK(a != b);
    b = a;
    b.publishedAt = QStringLiteral("2026-08-04T00:00:00Z");
    CHECK(a != b);
}
