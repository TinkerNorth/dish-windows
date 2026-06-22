// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// IpLiterals — private-IP-literal classification, pure and Qt-free.
//
// The connect path refuses to open a session to a public address that a
// satellite beacon/REST response claims to live at (a hostile beacon must not
// be able to point a dish at an arbitrary internet host), and discovery vets
// the addresses it surfaces the same way. `isPrivateHostLiteral` answers "is
// this an RFC-1918 / link-local / loopback / unique-local literal?" purely from
// the string, with no DNS. A hostname (anything that isn't a bare IP literal)
// is NOT private. Mirrors dish-android core/net/IpLiterals.kt — landed once in
// core/net so both the discovery slice (2a) and the connect guard (2b) consume
// the same rule.

#pragma once

#include <string>

namespace dish::net {

// True iff `host` is a private/loopback/link-local/unique-local IPv4 or IPv6
// literal. Surrounding "[...]" brackets (IPv6 URL-authority form) are stripped
// first. Returns false for hostnames, malformed literals, and public addresses.
bool isPrivateHostLiteral(const std::string& host);

} // namespace dish::net
