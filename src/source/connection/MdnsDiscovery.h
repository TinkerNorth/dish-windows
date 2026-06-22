// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dish::net {

// Discovers Satellite servers advertised over mDNS / Bonjour as the
// `_satellite._udp.local.` service type — the modern discovery path that
// reaches subnets where the legacy UDP broadcast beacon (LANDiscovery) is
// dropped. Pairs with satellite/src/net/mdns_responder.cpp.
//
// Windows has no first-class mDNS browse API that's free of COM / service
// dependencies, so this is a one-shot raw multicast-DNS client: it sends a
// single PTR query (with the unicast-response bit set) to 224.0.0.251:5353
// and parses the PTR + SRV + TXT + A answer records the responder returns.
//
// This is a Gateway (IO + pure wire parse): the wire-DNS layer lives in
// detail::, and the service→DiscoveredServer MAPPING layer below mirrors
// dish-android's MdnsDiscovery mapping functions so port precedence + TXT
// extraction are unit-testable without a socket.
class MdnsDiscovery {
  public:
    static constexpr int kDefaultTimeoutMs = 4000;

    // Blocking. Call from a background thread. Returns every unique satellite
    // that answered within `timeoutMs`. Each recv has a short timeout so a
    // quiet network doesn't hang the caller past the deadline; once at least
    // one satellite has answered the wait is capped at a short grace window.
    static QList<models::DiscoveredServer> discover(int timeoutMs = kDefaultTimeoutMs);
};

// ── mDNS service→DiscoveredServer mapping layer ─────────────────────────────
// Pure functions over a parsed (serviceName, hostAddress, SRV port, TXT map)
// tuple. Port of dish-android source/connection/MdnsDiscovery.kt's
// mdnsServiceToServer / mdnsTxtInt / mdnsTxtString. Exposed for unit tests.

// Protocol-default ports when neither TXT nor SRV supplies one. The client API
// is HTTPS on a single port (9443) advertised under `pair` and `http`.
inline constexpr int kMdnsDefaultUdp = 9876;
inline constexpr int kMdnsDefaultPair = 9443;
inline constexpr int kMdnsDefaultHttp = 9443;

// Parse TXT[key] as an int, trimming surrounding whitespace. nullopt when the
// key is missing, the value is null/empty, or it isn't fully numeric.
std::optional<int> mdnsTxtInt(const QHash<QString, QByteArray>& txt, const QString& key);

// TXT[key] as a trimmed non-empty string, else nullopt (missing / whitespace).
std::optional<QString> mdnsTxtString(const QHash<QString, QByteArray>& txt, const QString& key);

// Assemble a DiscoveredServer from one resolved mDNS service.
//   * null host (empty `hostAddress`) → nullopt (nothing to connect to)
//   * empty service name → fall back to the observed IP for `name`
//   * udpPort precedence: TXT "udp" > SRV port (only when > 0) > 9876
//   * pairPort / httpPort: TXT "pair"/"http" > 9443 (SRV never feeds these)
//   * machineId from TXT "mid" (else "")
//   * source = Mdns
// `hostAddress` empty string models android's null host.
std::optional<models::DiscoveredServer> mdnsServiceToServer(const QString& serviceName,
                                                            const QString& hostAddress, int srvPort,
                                                            const QHash<QString, QByteArray>& txt);

// Pure mDNS wire helpers. Exposed only so tests/test_mdns_discovery.cpp can
// exercise the compression-pointer + bounds handling directly; production code
// reaches them through MdnsDiscovery::discover().
namespace detail {

// Read a DNS name at `off`, following 0xC0 compression pointers. Returns the
// bytes consumed at `off` (a pointer counts as 2), or 0 on malformed input.
std::size_t skipName(const std::uint8_t* p, std::size_t len, std::size_t off);

// Decode a DNS name into `out` (dot-separated, no trailing dot). Returns false
// on malformed input.
bool readName(const std::uint8_t* p, std::size_t len, std::size_t off, std::string& out);

// Parse one mDNS response packet into a DiscoveredServer (source = Mdns), or
// nullopt when it doesn't carry a usable satellite record set.
std::optional<models::DiscoveredServer> parseResponse(const std::uint8_t* p, std::size_t len);

} // namespace detail

} // namespace dish::net
