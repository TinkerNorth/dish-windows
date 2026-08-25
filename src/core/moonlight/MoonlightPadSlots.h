// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Controller-number allocation and the CONTROLLER_MULTI active mask, as pure
// logic so the bind/unbind decisions are testable without a socket or a pad.
//
// Moonlight identifies each virtual pad by a small controller number, and every
// CONTROLLER_MULTI packet carries a bitfield of the controllers currently
// present. Clearing a controller's bit (while still naming it in `ctrl #`) is
// how the protocol signals an unplug, so the final packet after an unbind has to
// be sent with the bit already dropped.

#pragma once

#include "core/moonlight/MoonlightControl.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace dish::moonlight {

// Moonlight hosts accept at most 4 virtual pads (the XInput ceiling every
// reference host implements).
inline constexpr std::uint8_t kMaxPads = 4;

// slotId -> controller number, plus the derived active mask. Copyable, so a
// caller can snapshot it under a lock and act on the copy outside.
class PadSlots {
  public:
    // Assigns the lowest free controller number. Returns nullopt when full or
    // already assigned (the caller reads `numberFor` for the existing one).
    std::optional<std::uint8_t> assign(const std::string& slotId) {
        if (assigned_.count(slotId) != 0) { return std::nullopt; }
        for (std::uint8_t n = 0; n < kMaxPads; ++n) {
            bool taken = false;
            for (const auto& [id, num] : assigned_) {
                if (num == n) {
                    taken = true;
                    break;
                }
            }
            if (!taken) {
                assigned_[slotId] = n;
                return n;
            }
        }
        return std::nullopt;
    }

    // The controller number this slot holds, if any.
    std::optional<std::uint8_t> numberFor(const std::string& slotId) const {
        const auto it = assigned_.find(slotId);
        if (it == assigned_.end()) { return std::nullopt; }
        return it->second;
    }

    // Releases the slot and returns the number it held, so the caller can send
    // the final bit-cleared CONTROLLER_MULTI for it.
    std::optional<std::uint8_t> release(const std::string& slotId) {
        const auto it = assigned_.find(slotId);
        if (it == assigned_.end()) { return std::nullopt; }
        const std::uint8_t number = it->second;
        assigned_.erase(it);
        return number;
    }

    // Bitfield of every controller currently present.
    std::uint16_t activeMask() const {
        std::uint16_t mask = 0;
        for (const auto& [id, num] : assigned_) {
            mask = static_cast<std::uint16_t>(mask | (1U << num));
        }
        return mask;
    }

    bool empty() const { return assigned_.empty(); }
    std::size_t size() const { return assigned_.size(); }

  private:
    std::map<std::string, std::uint8_t> assigned_;
};

// The capability bits to advertise in CONTROLLER_ARRIVAL for a pad with these
// hardware features. Analog triggers are always present on the pads Dish
// forwards; the rest follow the detected hardware.
inline std::uint8_t padCapabilities(bool hasRumble, bool hasMotion, bool hasTouchpad,
                                    bool hasBattery, bool hasLightbar) {
    std::uint8_t caps = kPadCapAnalogTriggers;
    if (hasRumble) { caps = static_cast<std::uint8_t>(caps | kPadCapRumble); }
    if (hasMotion) { caps = static_cast<std::uint8_t>(caps | kPadCapAccel | kPadCapGyro); }
    if (hasTouchpad) { caps = static_cast<std::uint8_t>(caps | kPadCapTouchpad); }
    if (hasBattery) { caps = static_cast<std::uint8_t>(caps | kPadCapBattery); }
    if (hasLightbar) { caps = static_cast<std::uint8_t>(caps | kPadCapRgbLed); }
    return caps;
}

// The user's emulated-device pick (kMoonlightDevice*) as a CONTROLLER_ARRIVAL
// wire type. Auto sends Unknown, which lets the host choose.
inline std::uint8_t arrivalTypeFromDevicePick(int devicePick) {
    switch (devicePick) {
    case 1:
        return kPadTypeXbox;
    case 2:
        return kPadTypePlayStation;
    case 3:
        return kPadTypeNintendo;
    default:
        return kPadTypeUnknown;
    }
}

} // namespace dish::moonlight
