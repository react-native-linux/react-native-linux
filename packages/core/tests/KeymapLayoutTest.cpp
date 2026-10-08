#include "InputPipeline.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <xkbcommon/xkbcommon.h>

namespace {

using react_native_linux::domKeyCode;
using react_native_linux::domKeyName;

/** evdev's KEY_Q: the key US labels Q and French AZERTY labels A. */
constexpr uint32_t kEvdevKeyQ = 16;
constexpr uint32_t kEvdevToXkbKeycodeOffset = 8;

struct DomKey {
    std::string key;
    std::string code;
};

/**
 * One press of `evdevKeycode` under a keymap compiled for `layout`, named the way `WaylandSeat::makeKeyEvent`
 * names it: the keysym's name and the text the key produces go to `domKeyName`, the evdev code to `domKeyCode`.
 */
DomKey pressUnder(const char* layout, uint32_t evdevKeycode) {
    const std::unique_ptr<xkb_context, decltype(&xkb_context_unref)> context(xkb_context_new(XKB_CONTEXT_NO_FLAGS),
                                                                             &xkb_context_unref);
    const xkb_rule_names names{
        .rules = nullptr, .model = nullptr, .layout = layout, .variant = nullptr, .options = nullptr};
    const std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> keymap(
        xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS), &xkb_keymap_unref);
    const std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> state(xkb_state_new(keymap.get()), &xkb_state_unref);
    const xkb_keycode_t keycode = evdevKeycode + kEvdevToXkbKeycodeOffset;
    std::array<char, 64> symbolName{};
    std::array<char, 16> text{};

    xkb_keysym_get_name(xkb_state_key_get_one_sym(state.get(), keycode), symbolName.data(), symbolName.size());
    xkb_state_key_get_utf8(state.get(), keycode, text.data(), text.size());

    return DomKey{.key = domKeyName(symbolName.data(), text.data()), .code = domKeyCode(evdevKeycode)};
}

/**
 * #487, the half of #65 a pure function cannot prove: the same physical key under two layouts. `key` is the
 * character the layout puts there and `code` is the physical position, so a French AZERTY keyboard's top-left
 * letter is `key: "a"` and still `code: "KeyQ"`, exactly as a browser reports it.
 */
TEST(KeymapLayoutTest, TheSamePhysicalKeyHasTheLayoutsKeyAndOneCode) {
    const DomKey us = pressUnder("us", kEvdevKeyQ);
    const DomKey french = pressUnder("fr", kEvdevKeyQ);

    EXPECT_EQ(us.key, "q");
    EXPECT_EQ(french.key, "a");
    EXPECT_EQ(us.code, "KeyQ");
    EXPECT_EQ(french.code, "KeyQ");
}

} // namespace
