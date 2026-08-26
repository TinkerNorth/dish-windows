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

// The session belongs to the HOST and is reference counted by the pads bound to
// it: the first pad on a host brings it up and the last one off tears it down,
// so a second binding never starts a second session and no app is ever left
// stranded. Both are asked of the pad set BEFORE the assignment and AFTER the
// release, which is what makes them the same question in both directions.
// `pads`, never `slots`: Qt's moc keywords make `slots` a macro that expands to
// nothing, and the parameter would vanish.
inline bool bindStartsSession(const PadSlots& pads) { return pads.empty(); }
inline bool unbindEndsSession(const PadSlots& pads) { return pads.empty(); }

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
// wire type. Auto has no wire value of its own and resolves before the packet is
// built; see arrivalTypeForBinding.
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

// What a host actually materialises for each emulated type, as capability bits.
// NO MOONLIGHT HOST EXPOSES THIS: there is no API that reports it and the data
// flows the other way, the host picking its emulated device from what the client
// declares. So it is client-side knowledge, taken from what the reference host
// builds per type. Only the PlayStation pad carries trigger rumble, a touchpad,
// motion, a battery and an LED; Xbox and Nintendo carry analog triggers and
// rumble and nothing else. Nintendo has NO MOTION over Moonlight, unlike the
// Satellite type of the same name: the two are different type systems that
// happen to share their nouns.
inline std::uint8_t typeCapabilityCeiling(std::uint8_t arrivalType) {
    switch (arrivalType) {
    case kPadTypeXbox:
    case kPadTypeNintendo:
        return static_cast<std::uint8_t>(kPadCapAnalogTriggers | kPadCapRumble);
    default:
        return 0xFF;
    }
}

// Auto resolves on the CLIENT, before the wire. A source that reports motion
// asks for a PlayStation pad and everything else for an Xbox one: it is the only
// rule that matches the reference host's own promotion of an Unknown pad with
// motion, and it lets the type card tell the truth about what the binding will
// carry. Sending Unknown instead would leave the host to pick with no way of
// telling us what it picked.
inline std::uint8_t resolveAutoArrivalType(bool sourceHasMotion) {
    return sourceHasMotion ? kPadTypePlayStation : kPadTypeXbox;
}

// The CONTROLLER_ARRIVAL type for one binding: the user's pick, or the resolved
// Auto answer for the source this binding drives.
inline std::uint8_t arrivalTypeForBinding(int devicePick, bool sourceHasMotion) {
    const std::uint8_t picked = arrivalTypeFromDevicePick(devicePick);
    if (picked != kPadTypeUnknown) { return picked; }
    return resolveAutoArrivalType(sourceHasMotion);
}

// What the arrival packet declares: the type's ceiling intersected with what the
// input source can actually provide. Declaring a bit the source cannot fill
// makes the host request reports that never arrive.
inline std::uint8_t declaredCapabilities(std::uint8_t arrivalType, bool hasRumble, bool hasMotion,
                                         bool hasTouchpad, bool hasBattery, bool hasLightbar) {
    const std::uint8_t source =
        padCapabilities(hasRumble, hasMotion, hasTouchpad, hasBattery, hasLightbar);
    return static_cast<std::uint8_t>(source & typeCapabilityCeiling(arrivalType));
}

// The whole low sixteen, which every type carries, plus the touchpad click only
// when a touchpad is in the declared set.
inline constexpr std::uint32_t kSupportedButtonsBase = 0x0000FFFFu;
inline constexpr std::uint32_t kSupportedButtonTouchpadClick = 0x00100000u;

inline std::uint32_t declaredButtonFlags(std::uint8_t declaredCaps) {
    std::uint32_t flags = kSupportedButtonsBase;
    if ((declaredCaps & kPadCapTouchpad) != 0) { flags |= kSupportedButtonTouchpadClick; }
    return flags;
}

} // namespace dish::moonlight
