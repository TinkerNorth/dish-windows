// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CaptureMode — the explicit, pure lifecycle FSM for input-capture ("press a
// button to assign it") on the Configure-controls page. Qt-free / SDL-free and
// exhaustively tested, in the house style of core/reducer/UsbPathMachine.h.
//
// ── The gap this closes ──────────────────────────────────────────────────────
// Today "capture mode" has NO modeled state. It is THREE disconnected pieces
// with no single owner:
//   * SDLGamepadBridge::captureEnabled_ — an atomic<bool> on the SDL thread
//     that gates whether rawJoystickInput is emitted at all;
//   * the one-shot rawJoystickInput signal that fans out on every raw input;
//   * AppViewModel::capturingSlotId_ — a QString on the GUI thread, and the
//     guard `deviceId != capturingSlotId_` in onRawJoystickInput is the ONLY
//     thing stopping a SECOND pad's input from being assigned to the capturing
//     slot.
// Because the truth is split across a bridge bool and a view-model string, a
// missed clear leaves capture armed, and the "which device may assign" rule
// lives in an inline `!=` rather than in one checkable place. This reducer makes
// the capture lifecycle a single value: a phase + the slot/target being
// assigned, with a TOTAL transition function over every (phase x event). The
// coordinator (AppViewModel / AppModel) drives events in and acts on the result;
// it stops owning the truth.
//
// ── What this does NOT do ────────────────────────────────────────────────────
// It models the lifecycle AROUND the per-capture fold, not the fold itself. The
// existing pure reducer JoystickMapping::withAssignment (one capture result ->
// a new JoystickRemap) stays exactly where it is. On an ACCEPTED capture this
// FSM returns to Idle (a one-shot assign) and reports accepted == true; the
// coordinator then calls withAssignment with the captured (kind, index) and
// re-arms (a fresh Start) if the user is assigning more outputs.
//
// The accept rule reuses the SAME per-kind capture-threshold predicates the
// bridge already gates its emit behind (JoystickMapping.h: captureAxisPasses /
// captureButtonPasses / captureHatPasses), so the deliberate-press gate has ONE
// definition shared by the bridge, the existing remap tests, and this FSM —
// never a second, drifting copy.

#pragma once

#include "Input/JoystickMapping.h" // CaptureKind + capture{Axis,Button,Hat}Passes

#include <string>
#include <variant>

namespace dish::input {

// The two capture phases. Idle: not capturing; raw input is ignored. Capturing:
// armed for exactly one logical output (slotId + target), waiting for the
// matching device to send a deliberate press.
enum class CapturePhase { Idle, Capturing };

// The whole capture lifecycle as ONE value (the truth the split bridge-bool +
// view-model-QString did not have). `slotId` is the capturing device/slot id
// (for an SDL raw joystick the slot id IS the "sdl:<iid>" device id — the same
// identity AppViewModel filters on); `target` is the logical output being
// assigned (e.g. "buttonA" / "leftStickX"). Both are empty while Idle.
struct CaptureState {
    CapturePhase phase = CapturePhase::Idle;
    std::string slotId;
    std::string target;

    bool operator==(const CaptureState& o) const {
        return phase == o.phase && slotId == o.slotId && target == o.target;
    }
    bool operator!=(const CaptureState& o) const { return !(*this == o); }
};

// ── Events ────────────────────────────────────────────────────────────────

namespace capture_event {

// The user picked an output to (re)assign and pressed "listen". Arms capture for
// (slotId, target). Issued for the slot the Configure-controls page is editing.
struct Start {
    std::string slotId;
    std::string target;
    bool operator==(const Start& o) const { return slotId == o.slotId && target == o.target; }
    bool operator!=(const Start& o) const { return !(*this == o); }
};

// A raw input observed from the bridge (the rawJoystickInput payload, 1:1):
// `deviceId` the "sdl:<iid>" source id; `kind` 0=axis / 1=button / 2=hat (the
// CaptureKind values); `index` the raw source index; `value` the axis int16 /
// 1 for a button press / the SDL_HAT_* bitmask for a hat.
struct RawInput {
    std::string deviceId;
    int kind = 0;
    int index = 0;
    int value = 0;
    bool operator==(const RawInput& o) const {
        return deviceId == o.deviceId && kind == o.kind && index == o.index && value == o.value;
    }
    bool operator!=(const RawInput& o) const { return !(*this == o); }
};

// The user cancelled / left the page. Disarms capture from any phase.
struct Stop {
    bool operator==(const Stop&) const { return true; }
    bool operator!=(const Stop&) const { return false; }
};

} // namespace capture_event

using CaptureEvent =
    std::variant<capture_event::Start, capture_event::RawInput, capture_event::Stop>;

// The pure result of reduceCapture: the next state plus whether THIS event was an
// accepted capture. `accepted` is true ONLY for a RawInput that, while Capturing,
// came from the matching device (deviceId == slotId) AND passed the per-kind
// deliberate-press threshold. On an accepted capture `next.phase` is Idle (the
// one-shot assign — see file header); the coordinator reads the (kind, index) off
// the event it just fed in, calls withAssignment, and re-arms with a fresh Start
// if more outputs remain. For every other event `accepted` is false.
struct CaptureReduction {
    CaptureState next;
    bool accepted = false;

    bool operator==(const CaptureReduction& o) const {
        return next == o.next && accepted == o.accepted;
    }
    bool operator!=(const CaptureReduction& o) const { return !(*this == o); }
};

// True iff a raw capture of `kind` carrying `value` clears the deliberate-press
// gate for that kind. Reuses the bridge's own per-kind predicates so the gate is
// defined ONCE (no second copy of the axis threshold here). An axis must exceed
// the stick-flat-scaled threshold (captureAxisPasses); a button press always
// passes (captureButtonPasses); a hat passes on any non-centered direction
// (captureHatPasses). Any unrecognised kind fails closed (returns false) so the
// reducer stays total and a bogus kind can never self-assign.
inline bool capturePasses(int kind, int value) {
    switch (static_cast<CaptureKind>(kind)) {
    case CaptureKind::Axis:
        return captureAxisPasses(value);
    case CaptureKind::Button:
        return captureButtonPasses();
    case CaptureKind::Hat:
        return captureHatPasses(value);
    }
    return false;
}

// The total reducer: (state, event) -> next state + accepted flag. Defined for
// EVERY (phase x event) pair; never throws. The transition table:
//
//   Start            -> Capturing(slotId, target) from ANY phase. A Start while
//                       already Capturing RE-TARGETS (the new slot/target wins),
//                       so re-pointing the capture is a first-class transition,
//                       not a hidden mutation.
//   RawInput (Idle)  -> ignored (accepted=false, state unchanged). Nothing is
//                       armed, so a stray raw input can never assign.
//   RawInput (Capturing):
//       deviceId == slotId AND capturePasses(kind, value)
//                    -> accepted=true, next=Idle  (one-shot assign).
//       deviceId != slotId  (a SECOND pad)
//                    -> ignored, still Capturing. THIS is the property the inline
//                       `deviceId != capturingSlotId_` guard enforces today, now
//                       a pure, tested transition with a single owner.
//       sub-threshold (right device, but the press did not clear the gate)
//                    -> ignored, still Capturing (resting-axis jitter / a hat
//                       recenter never self-assigns; the user keeps trying).
//   Stop             -> Idle from ANY phase (slot/target cleared). Idempotent
//                       from Idle, so a double-clear is harmless (the missed-clear
//                       hazard of the old split state disappears: Stop is total
//                       and always lands on Idle).
inline CaptureReduction reduceCapture(const CaptureState& state, const CaptureEvent& event) {
    if (const auto* start = std::get_if<capture_event::Start>(&event)) {
        return CaptureReduction{CaptureState{CapturePhase::Capturing, start->slotId, start->target},
                                /*accepted=*/false};
    }
    if (std::get_if<capture_event::Stop>(&event) != nullptr) {
        return CaptureReduction{CaptureState{CapturePhase::Idle, /*slotId=*/{}, /*target=*/{}},
                                /*accepted=*/false};
    }
    if (const auto* raw = std::get_if<capture_event::RawInput>(&event)) {
        // A raw input only matters while armed AND only from the device we are
        // capturing — the other-device filter that lived in the view model.
        if (state.phase == CapturePhase::Capturing && raw->deviceId == state.slotId &&
            capturePasses(raw->kind, raw->value)) {
            return CaptureReduction{CaptureState{CapturePhase::Idle, /*slotId=*/{}, /*target=*/{}},
                                    /*accepted=*/true};
        }
        // Idle, a different device, or a sub-threshold press: ignore, unchanged.
        return CaptureReduction{state, /*accepted=*/false};
    }
    // Unreachable: the variant is exhausted above. Stay put, total and safe.
    return CaptureReduction{state, /*accepted=*/false};
}

} // namespace dish::input
