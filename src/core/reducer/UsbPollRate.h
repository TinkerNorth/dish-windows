// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbPollRate — pure USB interrupt-endpoint poll-rate math. Qt-free port of
// dish-android source/usb/UsbPollRate.kt 1:1 (the UsbPollRateTest pins both
// functions, 20 cases).
//
//   * computeUsbPollRateHz(epInterval, epMaxPacketSize): the THEORETICAL rate
//     from the endpoint descriptor's bInterval. Full-speed (maxPacket <= 64):
//     period = bInterval * 1ms, so rate = 1000 / bInterval. High-speed
//     (maxPacket >= 65): period = 2^(bInterval-1) * 125us, so rate =
//     8000 / 2^(bInterval-1). The exponent is clamped to [0,15] so an absurd
//     bInterval cannot overflow the shift (and a clamped-to-15 period floors
//     the rate to 0). epInterval <= 0 -> 0.
//   * measuredPollRateHz(deltaCount, deltaMs): the OBSERVED rate from a
//     completion-count delta over a sampling window. floor(deltaCount/deltaMs
//     * 1000), guarding a zero/negative window (-> 0) and a counter reset
//     (negative delta -> 0, never a negative rate).
//
// On Windows the descriptor fields come from WinUSB_QueryPipe /
// HidD_GetAttributes rather than android UsbEndpoint, but the arithmetic is
// identical — this is platform-independent USB spec math.

#pragma once

#include <cstdint>

namespace dish::reducer {

// Largest full-speed interrupt-endpoint max packet size. A descriptor reporting
// more than this is high-speed and encodes its interval as a power-of-two
// exponent of 125us microframes rather than a millisecond count.
inline constexpr int kMaxFsInterruptPacket = 64;

inline int computeUsbPollRateHz(int epInterval, int epMaxPacketSize) {
    if (epInterval <= 0) { return 0; }
    const bool isHighSpeed = epMaxPacketSize > kMaxFsInterruptPacket;
    std::int64_t periodMicros = 0;
    if (isHighSpeed) {
        int exp = epInterval - 1;
        if (exp < 0) { exp = 0; }
        if (exp > 15) { exp = 15; }
        periodMicros = (std::int64_t{1} << exp) * 125;
    } else {
        periodMicros = static_cast<std::int64_t>(epInterval) * 1000;
    }
    if (periodMicros <= 0) { return 0; }
    return static_cast<int>(1'000'000 / periodMicros);
}

inline int measuredPollRateHz(std::int64_t deltaCount, std::int64_t deltaMs) {
    if (deltaMs <= 0) { return 0; }
    if (deltaCount <= 0) { return 0; }
    return static_cast<int>((deltaCount * 1000) / deltaMs);
}

} // namespace dish::reducer
