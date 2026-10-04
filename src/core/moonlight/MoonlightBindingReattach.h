// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Which standing Moonlight bindings to put back when a pad appears, and under which slot. Pure,
// so the rule is tested without a bridge or a manager. A record that carries the pad's identity
// follows the pad to whatever slot it holds now; a record written before identities existed
// follows its slot id, as it always did.

#pragma once

#include "Network/MoonlightHost.h"

#include <QList>
#include <QSet>
#include <QString>

namespace dish::moonlight {

struct PresentPad {
    QString slotId;
    QString identity;
};

struct Reattach {
    models::MoonlightBinding binding;
    // Where the pad is now, and what it is: the record is rewritten with both when they differ.
    QString slotId;
    QString identity;
};

inline QList<Reattach> bindingsToReattach(const QList<models::MoonlightBinding>& standing,
                                          const QList<PresentPad>& present,
                                          const QSet<QString>& moonlightBoundSlotIds) {
    QList<Reattach> out;
    QSet<QString> claimed;
    for (const auto& binding : standing) {
        const PresentPad* pad = nullptr;
        for (const auto& candidate : present) {
            if (claimed.contains(candidate.slotId)) { continue; }
            const bool byIdentity =
                !binding.padIdentity.isEmpty() && candidate.identity == binding.padIdentity;
            const bool bySlot = binding.padIdentity.isEmpty() && candidate.slotId == binding.slotId;
            if (byIdentity || bySlot) {
                pad = &candidate;
                break;
            }
        }
        if (pad == nullptr) { continue; }
        claimed.insert(pad->slotId);
        if (!moonlightBoundSlotIds.contains(pad->slotId)) {
            out.append({binding, pad->slotId, pad->identity});
        }
    }
    return out;
}

} // namespace dish::moonlight
