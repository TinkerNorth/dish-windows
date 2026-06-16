// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/ConnectionListModel.h"

#include "core/reducer/ConnectionRows.h"

namespace dish::qml {

namespace {

namespace rd = dish::reducer;

QString linkStateToken(rd::UiLinkState s) {
    switch (s) {
    case rd::UiLinkState::Found:
        return QStringLiteral("found");
    case rd::UiLinkState::Stale:
        return QStringLiteral("stale");
    case rd::UiLinkState::Saved:
        return QStringLiteral("saved");
    case rd::UiLinkState::Ready:
        return QStringLiteral("ready");
    case rd::UiLinkState::Connecting:
        return QStringLiteral("connecting");
    case rd::UiLinkState::Connected:
        return QStringLiteral("connected");
    case rd::UiLinkState::Unstable:
        return QStringLiteral("unstable");
    }
    return {};
}

QString chipToken(rd::StatusChipKey c) {
    switch (c) {
    case rd::StatusChipKey::Found:
        return QStringLiteral("found");
    case rd::StatusChipKey::NeedsPairing:
        return QStringLiteral("needsPairing");
    case rd::StatusChipKey::Offline:
        return QStringLiteral("offline");
    case rd::StatusChipKey::Ready:
        return QStringLiteral("ready");
    case rd::StatusChipKey::Connecting:
        return QStringLiteral("connecting");
    case rd::StatusChipKey::Online:
        return QStringLiteral("online");
    case rd::StatusChipKey::Unstable:
        return QStringLiteral("unstable");
    }
    return {};
}

QString dotToken(rd::DotColor d) {
    switch (d) {
    case rd::DotColor::Success:
        return QStringLiteral("success");
    case rd::DotColor::Primary:
        return QStringLiteral("primary");
    case rd::DotColor::Warning:
        return QStringLiteral("warning");
    case rd::DotColor::Muted:
        return QStringLiteral("muted");
    }
    return {};
}

QString glyphToken(rd::ConnectionGlyph g) {
    switch (g) {
    case rd::ConnectionGlyph::SatelliteBase:
        return QStringLiteral("satelliteBase");
    case rd::ConnectionGlyph::SatelliteConnected:
        return QStringLiteral("satelliteConnected");
    case rd::ConnectionGlyph::SatelliteOff:
        return QStringLiteral("satelliteOff");
    }
    return {};
}

} // namespace

ConnectionListModel::ConnectionListModel(QObject* parent) : QAbstractListModel(parent) {}

int ConnectionListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) { return 0; }
    return static_cast<int>(rows_.size());
}

QVariant ConnectionListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const auto& r = rows_.at(static_cast<std::size_t>(index.row()));

    switch (role) {
    case IdRole:
        return QString::fromStdString(r.id);
    case LabelRole:
        return QString::fromStdString(r.label);
    case IpRole:
        return QString::fromStdString(r.ip);
    case UdpPortRole:
        return r.udpPort;
    case LinkStateRole:
        return linkStateToken(r.live);
    case ChipRole:
        return chipToken(r.chip);
    case DotColorRole:
        return dotToken(r.dotColor);
    case GlyphRole:
        return glyphToken(r.glyph);
    case BoundSlotIdRole:
        return QString::fromStdString(r.boundSlotId);
    case LiveLinkRole:
        return rd::isLiveLink(r.live);
    default:
        return {};
    }
}

QHash<int, QByteArray> ConnectionListModel::roleNames() const {
    return {
        {IdRole, "connectionId"},
        {LabelRole, "label"},
        {IpRole, "ip"},
        {UdpPortRole, "udpPort"},
        {LinkStateRole, "linkState"},
        {ChipRole, "chip"},
        {DotColorRole, "dotColor"},
        {GlyphRole, "glyph"},
        {BoundSlotIdRole, "boundSlotId"},
        {LiveLinkRole, "liveLink"},
    };
}

void ConnectionListModel::setRows(const std::vector<composer::ConnectionRow>& rows) {
    const int oldCount = static_cast<int>(rows_.size());
    const int newCount = static_cast<int>(rows.size());

    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        rows_ = rows;
        endInsertRows();
    } else if (newCount < oldCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        rows_ = rows;
        endRemoveRows();
    } else {
        rows_ = rows;
    }

    const int common = newCount < oldCount ? newCount : oldCount;
    if (common > 0) {
        emit dataChanged(index(0), index(common - 1));
    }

    if (oldCount != newCount) {
        emit countChanged();
    }
}

} // namespace dish::qml
