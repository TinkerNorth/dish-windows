// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The 3-step setup guide as an in-window dialog over the shell (design frames
// f-a2): Connect (scan + PIN) · Controller (pads auto-appear) · Summary.
// Stub registered ahead of the onboarding workstream; call `open()` from the
// Settings "Setup guide" row or the welcome hand-off.

import QtQuick
import "../kit" as Kit

Kit.ContentDialog {
    id: guide

    eyebrow: qsTr("Setup guide · step 1 of 3")
    heading: qsTr("Find and pair your Satellite")
    acceptText: qsTr("Next")
    rejectText: ""
    preferredWidth: 470

    onAccepted: close()
}
