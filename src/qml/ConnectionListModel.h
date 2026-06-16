// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ConnectionListModel — a thin QAbstractListModel adapter over the derived
// connection-row list (composer::ConnectionRow), so the QML Connections page
// can render the same rows the Widgets ConnectionsDialog shows. Holds NO
// business logic: every role mirrors a field of ConnectionRow (the pre-mapped
// render keys are exposed as stable string tokens the delegate resolves). The
// owner pushes the fresh row vector in via setRows() whenever the coordinator's
// connections() Observable changes.
//
// In dish_core (not the Quick target) so the unit tests link it without Qml.

#pragma once

#include "composer/ConnectionsComposer.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>

#include <vector>

namespace dish::qml {

class ConnectionListModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        LabelRole,         // QString: server name (or ip when name empty)
        IpRole,            // QString
        UdpPortRole,       // int
        LinkStateRole,     // QString token: "found"/"stale"/"saved"/"ready"/
                           //               "connecting"/"connected"/"unstable"
        ChipRole,          // QString token: status-chip key (see contract)
        DotColorRole,      // QString token: "success"/"primary"/"warning"/"muted"
        GlyphRole,         // QString token: "satelliteBase"/"satelliteConnected"/
                           //               "satelliteOff"
        BoundSlotIdRole,   // QString: bound slot id ("" if unbound)
        LiveLinkRole,      // bool: link is actively streaming (Connected/Unstable)
    };
    Q_ENUM(Roles)

    explicit ConnectionListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace the rows with the coordinator's latest derived list. Diffs the
    // tail count (rowsInserted/rowsRemoved) and patches surviving rows via one
    // dataChanged so a no-op re-emit doesn't reset the view.
    void setRows(const std::vector<composer::ConnectionRow>& rows);

  private:
    std::vector<composer::ConnectionRow> rows_;
};

} // namespace dish::qml
