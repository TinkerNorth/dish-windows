// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight words, in one place. C++ vends render TOKENS and never sentences
// (see src/qml/RenderTokens.h); this is the other half of that contract, and it
// is one file for the same reason RenderTokens.h is one switch per enum: four
// surfaces show a trust chip and two show the type cards, and four copies of the
// same mapping are four chances for them to disagree about what "remembered"
// says or what a PlayStation card is called.
//
// Purely declarative and purely presentational. Nothing here reads state, calls
// an invokable or writes anything; every function is a total mapping from a
// token the C++ side already decided on to the words and the tone that render it.
//
// REACTIVITY: these are JS calls and therefore not binding dependencies. A
// binding that calls one must also read whatever revision counter its page bumps,
// exactly as shared/BindingDraft.qml documents.

import QtQml
import "../kit" as Kit

QtObject {
    id: vocab

    // ── Trust ───────────────────────────────────────────────────────────────
    // Three words and nothing else, never a liveness light: Moonlight has no
    // bidirectional liveness, so the host cannot tell us it has forgotten us and
    // a pulsing dot would be a promise nothing can keep.
    function trustText(token) {
        switch (token) {
        case "paired":
            return qsTr("Paired");
        case "remembered":
            return qsTr("Remembered");
        }
        return qsTr("Not paired");
    }

    // Amber is the problem colour, never the working one. "Remembered" is a host
    // that is switched off, which is not a fault to report.
    function trustTone(token) {
        if (token === "paired")
            return Kit.CapabilityChip.Ok;
        if (token === "remembered")
            return Kit.CapabilityChip.Neutral;
        return Kit.CapabilityChip.Absent;
    }

    // ── Emulated types ──────────────────────────────────────────────────────
    // The three brand names are not translated; Auto is a word rather than a
    // brand and is.
    function typeName(token) {
        switch (token) {
        case "xbox":
            return "Xbox";
        case "playstation":
            return "PlayStation";
        case "nintendo":
            return "Nintendo";
        }
        return qsTr("Auto");
    }

    // The four cards a page renders, in the order C++ vends them, each carrying
    // the word it shows. `options` is App.moonlightTypeOptions(): {type, token}.
    function typesFrom(options) {
        const out = [];
        for (let i = 0; i < options.length; ++i) {
            out.push({
                "type": options[i].type,
                "token": options[i].token,
                "name": vocab.typeName(options[i].token)
            });
        }
        return out;
    }

    // The token for a wire type, read out of the same list rather than from a
    // second mapping that could disagree with it. Auto for anything unknown: it
    // is the only value that is safe to fall back to, because it resolves on this
    // machine before the packet is built.
    function tokenForType(type, options) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].type === type)
                return options[i].token;
        }
        return "auto";
    }
}
