// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// LicenseManifest — the third-party-attribution DTO + parse + row-mapping rules
// (Workstream 3c). Mirrors dish-android ui/settings/LicenseManifest.kt (the
// LicenseManifest / LicenseEntry / LicenseInfo data shape) plus the display
// rules from LicensesActivity/LicensesAdapter.kt.
//
// SoC: pure parse + pure mapping — no Source, no QWidget dependency. The DTO +
// the mapping free functions live testable without a widget; LicensesView is a
// thin renderer over them. The parse tolerates unknown keys and missing fields
// (forward-compat is part of the contract — android uses ignoreUnknownKeys and
// every field is nullable/defaulted).

#pragma once

#include <QString>

#include <optional>
#include <vector>

namespace dish::ui {

// One license reference inside an entry.
struct LicenseInfo {
    std::optional<QString> name;
    std::optional<QString> url;
};

// One third-party dependency.
struct LicenseEntry {
    std::optional<QString> group;
    std::optional<QString> artifact;
    std::optional<QString> version;
    std::optional<QString> name;
    std::optional<QString> url;
    std::vector<LicenseInfo> licenses;
};

// The whole manifest.
struct LicenseManifest {
    std::optional<QString> generatedBy;
    std::vector<LicenseEntry> libraries;
};

// Parse a manifest from raw JSON bytes. Tolerant: a malformed / empty document,
// or a non-object root, yields an empty manifest (no throw). Unknown top-level
// keys and unknown keys inside an entry are ignored; missing fields default.
LicenseManifest parseLicenseManifest(const QByteArray& json);

// Load + parse the manifest bundled in the binary at :/licenses/licenses.json.
LicenseManifest loadBundledLicenseManifest();

// ── Pure display-mapping rules (mirroring LicensesAdapter.kt) ────────────────

// Display name: entry.name if non-blank, else group:artifact (joining the
// present pieces with ':'). Empty if nothing is available.
QString licenseDisplayName(const LicenseEntry& entry);

// Version label: entry.version or empty.
QString licenseVersionLabel(const LicenseEntry& entry);

// License label: licenses[0].name if non-blank, else nullopt (the row hides the
// label rather than rendering an empty one).
std::optional<QString> licenseLabel(const LicenseEntry& entry);

// Click URL precedence: licenses[0].url, else entry.url; nullopt if neither (the
// row is then non-interactive).
std::optional<QString> licenseClickUrl(const LicenseEntry& entry);

// Convenience: a row is interactive iff it has a click URL.
bool licenseRowInteractive(const LicenseEntry& entry);

} // namespace dish::ui
