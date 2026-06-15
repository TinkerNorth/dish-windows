// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// QSettingsFixture — a fresh, isolated, file-backed QSettings for repository
// tests. Mirrors dish-android's mapBackedPrefs: one shared key/value store that
// the cert-pin, shared-key, and remembered-list repos co-tenant, distinguished
// only by key prefix — which is what makes the namespace-isolation and selective
// -clear assertions meaningful. Each call returns a brand-new empty store backed
// by a unique temp INI file (never the real HKCU registry), so tests don't leak
// into each other or the developer's machine.

#pragma once

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QUuid>

#include <memory>

namespace dish::test {

// A shared, empty, isolated QSettings on a unique temp INI path. The file is
// removed when the last shared_ptr drops.
inline std::shared_ptr<QSettings> makeSharedSettings() {
    const QString path = QDir::tempPath() + QStringLiteral("/dish-test-") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) +
                         QStringLiteral(".ini");
    // Custom deleter: sync, then unlink the backing file so the next test starts
    // clean and nothing lingers in the temp dir.
    return std::shared_ptr<QSettings>(new QSettings(path, QSettings::IniFormat),
                                      [path](QSettings* s) {
                                          s->sync();
                                          delete s;
                                          QFile::remove(path);
                                      });
}

} // namespace dish::test
