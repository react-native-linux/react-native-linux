#include "WaylandSerialLedger.h"

#include <cstddef>
#include <iostream>

namespace react_native_linux {

namespace {

constexpr size_t kindIndex(WaylandSerialKind kind) noexcept { return static_cast<size_t>(kind); }

} // namespace

std::string_view nameOfSerialKind(WaylandSerialKind kind) noexcept {
    switch (kind) { // COV_EXCL: every WaylandSerialKind value has a case below, so the implicit no-match branch cannot
                    // execute
    case WaylandSerialKind::PointerEnter:
        return "pointer enter";
    case WaylandSerialKind::PointerButtonPress:
        return "pointer button press";
    case WaylandSerialKind::KeyboardEnter:
        return "keyboard enter";
    case WaylandSerialKind::KeyboardKeyPress:
        return "keyboard key press";
    case WaylandSerialKind::TouchDown:
        return "touch down";
    case WaylandSerialKind::Configure:
        return "configure";
    case WaylandSerialKind::InteractiveMove:
        return "interactive move";
    case WaylandSerialKind::Selection:
        return "selection";
    }

    return "unknown"; // COV_EXCL: every WaylandSerialKind value has a case above, so this fallback cannot execute
}

void WaylandSerialLedger::recordPointerEnter(uint32_t serial) noexcept {
    const size_t index = kindIndex(WaylandSerialKind::PointerEnter);

    serials_[index] = serial;
    recorded_[index] = true;
}

void WaylandSerialLedger::recordPointerButton(uint32_t serial, bool pressed) noexcept {
    if (!pressed) {
        return;
    }

    for (const WaylandSerialKind kind :
         {WaylandSerialKind::PointerButtonPress, WaylandSerialKind::InteractiveMove, WaylandSerialKind::Selection}) {
        const size_t index = kindIndex(kind);

        serials_[index] = serial;
        recorded_[index] = true;
    }
}

void WaylandSerialLedger::recordKeyboardEnter(uint32_t serial) noexcept {
    const size_t index = kindIndex(WaylandSerialKind::KeyboardEnter);

    serials_[index] = serial;
    recorded_[index] = true;
}

void WaylandSerialLedger::recordKeyboardKey(uint32_t serial, bool pressed) noexcept {
    if (!pressed) {
        return;
    }

    for (const WaylandSerialKind kind : {WaylandSerialKind::KeyboardKeyPress, WaylandSerialKind::Selection}) {
        const size_t index = kindIndex(kind);

        serials_[index] = serial;
        recorded_[index] = true;
    }
}

void WaylandSerialLedger::recordTouchDown(uint32_t serial) noexcept {
    for (const WaylandSerialKind kind :
         {WaylandSerialKind::TouchDown, WaylandSerialKind::InteractiveMove, WaylandSerialKind::Selection}) {
        const size_t index = kindIndex(kind);

        serials_[index] = serial;
        recorded_[index] = true;
    }
}

void WaylandSerialLedger::recordConfigure(uint32_t serial) noexcept {
    const size_t index = kindIndex(WaylandSerialKind::Configure);

    serials_[index] = serial;
    recorded_[index] = true;
}

uint32_t WaylandSerialLedger::serial(WaylandSerialKind kind) const noexcept { return serials_[kindIndex(kind)]; }

std::optional<uint32_t> WaylandSerialLedger::requestSerial(WaylandSerialKind kind) const {
    const size_t index = kindIndex(kind);

    if (!recorded_[index]) {
        std::cerr << "[wayland-serial-ledger] refusing a request for the " << nameOfSerialKind(kind)
                  << " serial: none has arrived yet" << std::endl;

        return std::nullopt;
    }

    return serials_[index];
}

} // namespace react_native_linux
