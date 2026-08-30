// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Discovers Moonlight hosts advertised over mDNS as `_nvstream._tcp.local.`.
// Sibling of MdnsDiscovery on the Satellite path: a one-shot raw multicast-DNS
// client (one PTR query, then a parse of the PTR/SRV/A/TXT answers), because
// Windows exposes no COM-free mDNS browse API. Manual host entry (an IP typed by
// the user) is the fallback and lives in the coordinator, not here.

#pragma once

#include "Network/MoonlightHost.h"

#include <QHash>
#include <QList>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace dish::net {

class NvstreamDiscovery {
  public:
    static constexpr int kDefaultTimeoutMs = 4000;

    // Blocking; call from a background thread.
    static QList<models::MoonlightHost> discover(int timeoutMs = kDefaultTimeoutMs);
};

// ── Pure mapping + wire helpers, exposed for tests ──────────────────────────
namespace nvstream_detail {

// Bytes consumed at `off`, following 0xC0 compression pointers; 0 on malformed.
std::size_t skipName(const std::uint8_t* p, std::size_t len, std::size_t off);
// Dot-separated, no trailing dot. False on malformed input.
bool readName(const std::uint8_t* p, std::size_t len, std::size_t off, std::string& out);
// nullopt when the packet carries no usable _nvstream record set.
std::optional<models::MoonlightHost> parseResponse(const std::uint8_t* p, std::size_t len);

} // namespace nvstream_detail

// Assemble a host from one resolved service. An empty address yields nullopt.
std::optional<models::MoonlightHost> nvstreamServiceToHost(const QString& instanceName,
                                                           const QString& hostAddress);

} // namespace dish::net
