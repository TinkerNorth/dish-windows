// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// GamepadButtonLayouts — the pure, Qt-free XUSB <-> HID button/hat bit map (the
// controller wire contract). Port of dish-android core/input/GamepadButtonLayouts.kt
// 1:1: xusbToHid / hidToXusb / hidButtonsOf / hidHatOf, all packed into a single
// `int` (NOT a struct/pair) so there is zero per-report allocation on the
// <=250 Hz hot path. No dependencies at all — header-only, free functions.
//
//   * xusbToHid(wButtons): XInput wButtons word -> packed HID (low 16 bits =
//     HID button word, bits 16..19 = HID hat octant 0..8).
//   * hidButtonsOf(packed) / hidHatOf(packed): unpack the two fields.
//   * hidToXusb(hidButtons, hat): the inverse map back to an XInput wButtons word.
//
// dpad bit combos fold to the 8 hat octants (the conflicting diagonals resolve
// in a fixed precedence); unknown XUSB bits (e.g. 0x0800) are dropped; 0 is the
// identity; xusbToHid then hidToXusb reproduces every canonical bit. The exact
// HID button/hat constants match the satellite/android HID report layout
// (BTN_A=0x0001 .. BTN_HOME=0x0400, HAT_NONE=0 / HAT_N=1 .. HAT_NW=8).

#pragma once

namespace dish::input::layout {

// XInput (XUSB) wButtons bits — identical to the android XUSB_* constants and to
// GamepadInputProcessor::Buttons.
inline constexpr int kXusbDpadUp = 0x0001;
inline constexpr int kXusbDpadDown = 0x0002;
inline constexpr int kXusbDpadLeft = 0x0004;
inline constexpr int kXusbDpadRight = 0x0008;
inline constexpr int kXusbStart = 0x0010;
inline constexpr int kXusbBack = 0x0020;
inline constexpr int kXusbLeftThumb = 0x0040;
inline constexpr int kXusbRightThumb = 0x0080;
inline constexpr int kXusbLeftShoulder = 0x0100;
inline constexpr int kXusbRightShoulder = 0x0200;
inline constexpr int kXusbGuide = 0x0400;
inline constexpr int kXusbA = 0x1000;
inline constexpr int kXusbB = 0x2000;
inline constexpr int kXusbX = 0x4000;
inline constexpr int kXusbY = 0x8000;

inline constexpr int kXusbDpadMask = 0x000F;

// HID report button bits (the controller-wire layout the BT/USB report packs).
inline constexpr int kHidA = 0x0001;
inline constexpr int kHidB = 0x0002;
inline constexpr int kHidX = 0x0004;
inline constexpr int kHidY = 0x0008;
inline constexpr int kHidLb = 0x0010;
inline constexpr int kHidRb = 0x0020;
inline constexpr int kHidSelect = 0x0040;
inline constexpr int kHidStart = 0x0080;
inline constexpr int kHidLs = 0x0100;
inline constexpr int kHidRs = 0x0200;
inline constexpr int kHidHome = 0x0400;

// HID hat octants. Neutral is 0; the 8 directions run clockwise from N=1.
inline constexpr int kHatNeutral = 0;
inline constexpr int kHatN = 1;
inline constexpr int kHatNe = 2;
inline constexpr int kHatE = 3;
inline constexpr int kHatSe = 4;
inline constexpr int kHatS = 5;
inline constexpr int kHatSw = 6;
inline constexpr int kHatW = 7;
inline constexpr int kHatNw = 8;

// Fold a 4-bit dpad mask (up/down/left/right) to a single hat octant. Diagonals
// take precedence over the cardinals (so up+right reads NE, not N); the
// physically-impossible opposing pair (up+down or left+right) resolves to the
// remaining axis or, if fully opposed, neutral — matching the android order.
inline int dpadBitsToHat(int dpadBits) {
    const bool up = (dpadBits & kXusbDpadUp) != 0;
    const bool down = (dpadBits & kXusbDpadDown) != 0;
    const bool left = (dpadBits & kXusbDpadLeft) != 0;
    const bool right = (dpadBits & kXusbDpadRight) != 0;
    if (up && right) { return kHatNe; }
    if (right && down) { return kHatSe; }
    if (down && left) { return kHatSw; }
    if (left && up) { return kHatNw; }
    if (up) { return kHatN; }
    if (right) { return kHatE; }
    if (down) { return kHatS; }
    if (left) { return kHatW; }
    return kHatNeutral;
}

// Inverse of dpadBitsToHat: a hat octant back to its (1..2) dpad bits.
inline int hatToDpadBits(int hat) {
    switch (hat) {
    case kHatN:
        return kXusbDpadUp;
    case kHatNe:
        return kXusbDpadUp | kXusbDpadRight;
    case kHatE:
        return kXusbDpadRight;
    case kHatSe:
        return kXusbDpadRight | kXusbDpadDown;
    case kHatS:
        return kXusbDpadDown;
    case kHatSw:
        return kXusbDpadDown | kXusbDpadLeft;
    case kHatW:
        return kXusbDpadLeft;
    case kHatNw:
        return kXusbDpadLeft | kXusbDpadUp;
    default:
        return 0;
    }
}

// Pack an XInput wButtons word into the HID layout. Low 16 bits hold the HID
// button word; bits 16..19 hold the hat octant. No allocation (a plain int)
// because this runs once per forwarded report on the hot path.
inline int xusbToHid(int wButtons) {
    const int hat = dpadBitsToHat(wButtons & kXusbDpadMask);
    int hid = 0;
    if ((wButtons & kXusbStart) != 0) { hid |= kHidStart; }
    if ((wButtons & kXusbBack) != 0) { hid |= kHidSelect; }
    if ((wButtons & kXusbLeftThumb) != 0) { hid |= kHidLs; }
    if ((wButtons & kXusbRightThumb) != 0) { hid |= kHidRs; }
    if ((wButtons & kXusbLeftShoulder) != 0) { hid |= kHidLb; }
    if ((wButtons & kXusbRightShoulder) != 0) { hid |= kHidRb; }
    if ((wButtons & kXusbGuide) != 0) { hid |= kHidHome; }
    if ((wButtons & kXusbA) != 0) { hid |= kHidA; }
    if ((wButtons & kXusbB) != 0) { hid |= kHidB; }
    if ((wButtons & kXusbX) != 0) { hid |= kHidX; }
    if ((wButtons & kXusbY) != 0) { hid |= kHidY; }
    return (hat << 16) | (hid & 0xFFFF);
}

// Extract the HID button word from a packed value.
inline int hidButtonsOf(int packed) { return packed & 0xFFFF; }

// Extract the HID hat octant (0..8) from a packed value.
inline int hidHatOf(int packed) { return (packed >> 16) & 0xF; }

// Inverse map: a HID button word + hat octant back to an XInput wButtons word.
inline int hidToXusb(int hidButtons, int hat) {
    int w = hatToDpadBits(hat);
    if ((hidButtons & kHidStart) != 0) { w |= kXusbStart; }
    if ((hidButtons & kHidSelect) != 0) { w |= kXusbBack; }
    if ((hidButtons & kHidLs) != 0) { w |= kXusbLeftThumb; }
    if ((hidButtons & kHidRs) != 0) { w |= kXusbRightThumb; }
    if ((hidButtons & kHidLb) != 0) { w |= kXusbLeftShoulder; }
    if ((hidButtons & kHidRb) != 0) { w |= kXusbRightShoulder; }
    if ((hidButtons & kHidHome) != 0) { w |= kXusbGuide; }
    if ((hidButtons & kHidA) != 0) { w |= kXusbA; }
    if ((hidButtons & kHidB) != 0) { w |= kXusbB; }
    if ((hidButtons & kHidX) != 0) { w |= kXusbX; }
    if ((hidButtons & kHidY) != 0) { w |= kXusbY; }
    return w;
}

} // namespace dish::input::layout
