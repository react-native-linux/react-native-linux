#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <linux/input-event-codes.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace {

constexpr uint32_t kSeatVersion = 1;
constexpr uint32_t kOutputVersion = 2;
constexpr uint32_t kManagerVersion = 1;
constexpr uint32_t kPressedState = 1;
constexpr uint32_t kReleasedState = 0;
constexpr uint32_t kNoModifiers = 0;
constexpr uint32_t kFirstLayout = 0;
constexpr xkb_level_index_t kBaseLevel = 0;
// Every keymap in the wild is written against X11 keycodes; evdev keycodes, which both virtual-input protocols
// carry, are the same numbers minus this offset. WaylandSeat adds it back on the way in.
constexpr uint32_t kEvdevKeycodeOffset = 8;
constexpr int kFailureExitStatus = 1;
constexpr int kSuccessExitStatus = 0;
constexpr int kNoSymbols = 0;
constexpr int kRoundtripFailure = -1;
constexpr size_t kScriptArgument = 1;
constexpr std::string_view kWhitespace = " \t";

struct Injector {
    wl_display* display{nullptr};
    wl_seat* seat{nullptr};
    zwlr_virtual_pointer_manager_v1* pointerManager{nullptr};
    zwp_virtual_keyboard_manager_v1* keyboardManager{nullptr};
    zwlr_virtual_pointer_v1* pointer{nullptr};
    zwp_virtual_keyboard_v1* keyboard{nullptr};
    xkb_keymap* keymap{nullptr};
    uint32_t outputWidth{0};
    uint32_t outputHeight{0};
    uint32_t shiftMask{kNoModifiers};
    uint32_t controlMask{kNoModifiers};
    uint32_t altMask{kNoModifiers};
    // Which modifiers a `key <name> press` is currently holding down, so a chord is three lines of a script
    // rather than something the vocabulary cannot say. Cleared key by key as each one is released.
    uint32_t heldModifiers{kNoModifiers};
    std::chrono::steady_clock::time_point startedAt{std::chrono::steady_clock::now()};
};

struct KeyStroke {
    uint32_t code{0};
    bool shifted{false};
};

bool reportError(std::string_view message) {
    std::cerr << "[rnl-inject] " << message << std::endl;

    return false;
}

uint32_t elapsedMilliseconds(const Injector& injector) {
    const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - injector.startedAt;

    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void handleOutputGeometry(void* /*data*/, wl_output* /*output*/, int32_t /*x*/, int32_t /*y*/,
                          int32_t /*physicalWidth*/, int32_t /*physicalHeight*/, int32_t /*subpixel*/,
                          const char* /*make*/, const char* /*model*/, int32_t /*transform*/) {}

void handleOutputMode(void* data, wl_output* /*output*/, uint32_t flags, int32_t width, int32_t height,
                      int32_t /*refresh*/) {
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) {
        return;
    }

    Injector& injector = *static_cast<Injector*>(data);
    injector.outputWidth = static_cast<uint32_t>(width);
    injector.outputHeight = static_cast<uint32_t>(height);
}

void handleOutputDone(void* /*data*/, wl_output* /*output*/) {}

void handleOutputScale(void* /*data*/, wl_output* /*output*/, int32_t /*factor*/) {}

wl_output_listener makeOutputListener() {
    // Value-initialised and then filled member by member, for the reason WaylandSeat::makePointerListener gives:
    // libwayland grew `name` and `description` in 1.20, and the bound version is what decides which events a
    // compositor may send.
    wl_output_listener listener{};

    listener.geometry = handleOutputGeometry;
    listener.mode = handleOutputMode;
    listener.done = handleOutputDone;
    listener.scale = handleOutputScale;

    return listener;
}

const wl_output_listener kOutputListener = makeOutputListener();

void handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                          uint32_t /*version*/) {
    Injector& injector = *static_cast<Injector*>(data);
    const std::string_view advertised(interface);

    if (advertised == wl_seat_interface.name) {
        injector.seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, kSeatVersion));
    } else if (advertised == wl_output_interface.name) {
        wl_output* output =
            static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, kOutputVersion));

        wl_output_add_listener(output, &kOutputListener, data);
    } else if (advertised == zwlr_virtual_pointer_manager_v1_interface.name) {
        injector.pointerManager = static_cast<zwlr_virtual_pointer_manager_v1*>(
            wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, kManagerVersion));
    } else if (advertised == zwp_virtual_keyboard_manager_v1_interface.name) {
        injector.keyboardManager = static_cast<zwp_virtual_keyboard_manager_v1*>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, kManagerVersion));
    }
}

void handleRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

const wl_registry_listener kRegistryListener{
    .global = handleRegistryGlobal,
    .global_remove = handleRegistryGlobalRemove,
};

bool connect(Injector& injector) {
    injector.display = wl_display_connect(nullptr);

    if (injector.display == nullptr) {
        return reportError("wl_display_connect failed; is WAYLAND_DISPLAY set?");
    }

    wl_registry* registry = wl_display_get_registry(injector.display);
    wl_registry_add_listener(registry, &kRegistryListener, &injector);

    // The first roundtrip carries the globals, the second the wl_output events the bind above subscribed to.
    wl_display_roundtrip(injector.display);
    wl_display_roundtrip(injector.display);
    wl_registry_destroy(registry);

    if (injector.seat == nullptr || injector.pointerManager == nullptr || injector.keyboardManager == nullptr) {
        return reportError("the compositor does not advertise wl_seat, zwlr_virtual_pointer_manager_v1 and "
                           "zwp_virtual_keyboard_manager_v1");
    }

    if (injector.outputWidth == 0 || injector.outputHeight == 0) {
        return reportError("no wl_output reported a current mode");
    }

    return true;
}

bool uploadKeymap(Injector& injector) {
    const std::unique_ptr<char, void (*)(void*)> keymapText(
        xkb_keymap_get_as_string(injector.keymap, XKB_KEYMAP_FORMAT_TEXT_V1), std::free);

    if (keymapText == nullptr) {
        return reportError("the keymap could not be serialised");
    }

    const size_t keymapSize = std::strlen(keymapText.get()) + 1;
    const int keymapDescriptor = memfd_create("rnl-inject-keymap", MFD_CLOEXEC);

    if (keymapDescriptor < 0) {
        return reportError("memfd_create failed");
    }

    const ssize_t written = write(keymapDescriptor, keymapText.get(), keymapSize);

    zwp_virtual_keyboard_v1_keymap(injector.keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymapDescriptor,
                                   static_cast<uint32_t>(keymapSize));
    close(keymapDescriptor);

    return written == static_cast<ssize_t>(keymapSize) ? true : reportError("the keymap could not be written");
}

bool createDevices(Injector& injector) {
    injector.pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(injector.pointerManager, injector.seat);
    injector.keyboard =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(injector.keyboardManager, injector.seat);

    // The layout is pinned rather than inherited: the keysym lookup below and the fixtures' expected traces are
    // both written against a plain US keymap.
    const xkb_rule_names names{.rules = "", .model = "", .layout = "us", .variant = "", .options = ""};
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    injector.keymap =
        context == nullptr ? nullptr : xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (injector.keymap == nullptr) {
        return reportError("the us keymap could not be compiled");
    }

    const xkb_mod_index_t shiftIndex = xkb_keymap_mod_get_index(injector.keymap, XKB_MOD_NAME_SHIFT);
    const xkb_mod_index_t controlIndex = xkb_keymap_mod_get_index(injector.keymap, XKB_MOD_NAME_CTRL);
    const xkb_mod_index_t altIndex = xkb_keymap_mod_get_index(injector.keymap, XKB_MOD_NAME_ALT);

    if (shiftIndex == XKB_MOD_INVALID || controlIndex == XKB_MOD_INVALID || altIndex == XKB_MOD_INVALID) {
        return reportError("the us keymap is missing shift, control or alt");
    }

    injector.shiftMask = uint32_t{1} << shiftIndex;
    injector.controlMask = uint32_t{1} << controlIndex;
    injector.altMask = uint32_t{1} << altIndex;

    return uploadKeymap(injector);
}

bool findKeyStroke(xkb_keymap* keymap, xkb_keysym_t keysym, KeyStroke& stroke) {
    for (xkb_keycode_t keycode = xkb_keymap_min_keycode(keymap); keycode <= xkb_keymap_max_keycode(keymap); ++keycode) {
        const xkb_level_index_t levels = xkb_keymap_num_levels_for_key(keymap, keycode, kFirstLayout);

        for (xkb_level_index_t level = kBaseLevel; level < levels; ++level) {
            const xkb_keysym_t* symbols = nullptr;
            const int count = xkb_keymap_key_get_syms_by_level(keymap, keycode, kFirstLayout, level, &symbols);

            if (count > kNoSymbols && *symbols == keysym) {
                stroke = KeyStroke{.code = keycode - kEvdevKeycodeOffset, .shifted = level > kBaseLevel};

                return true;
            }
        }
    }

    return false;
}

/**
 * The modifier bit a keysym *is*, or nothing when it is an ordinary key. A modifier held across other keys is
 * what makes Ctrl+A and Shift+Left expressible, and the compositor only knows one is held because the state
 * below says so on every key that follows.
 */
uint32_t modifierMaskOf(const Injector& injector, xkb_keysym_t keysym) {
    if (keysym == XKB_KEY_Control_L || keysym == XKB_KEY_Control_R) {
        return injector.controlMask;
    }

    if (keysym == XKB_KEY_Shift_L || keysym == XKB_KEY_Shift_R) {
        return injector.shiftMask;
    }

    if (keysym == XKB_KEY_Alt_L || keysym == XKB_KEY_Alt_R) {
        return injector.altMask;
    }

    return kNoModifiers;
}

bool sendKeysym(Injector& injector, xkb_keysym_t keysym, uint32_t state) {
    KeyStroke stroke;

    if (!findKeyStroke(injector.keymap, keysym, stroke)) {
        return reportError("the us keymap has no key for that keysym");
    }

    const uint32_t held = modifierMaskOf(injector, keysym);

    if (held != kNoModifiers) {
        injector.heldModifiers = state == kPressedState ? injector.heldModifiers | held
                                                        : injector.heldModifiers & ~held;
    }

    const uint32_t shifted = stroke.shifted && state == kPressedState ? injector.shiftMask : kNoModifiers;

    zwp_virtual_keyboard_v1_modifiers(injector.keyboard, injector.heldModifiers | shifted, kNoModifiers,
                                      kNoModifiers, kFirstLayout);
    zwp_virtual_keyboard_v1_key(injector.keyboard, elapsedMilliseconds(injector), stroke.code, state);

    return true;
}

std::string_view trimLeading(std::string_view text) {
    const size_t start = text.find_first_not_of(kWhitespace);

    return start == std::string_view::npos ? std::string_view{} : text.substr(start);
}

std::string_view nextToken(std::string_view& rest) {
    rest = trimLeading(rest);

    const size_t end = rest.find_first_of(kWhitespace);
    const std::string_view token = rest.substr(0, end);

    rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);

    return token;
}

bool parseNumber(std::string_view text, uint32_t& value) {
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);

    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool parsePosition(std::string_view& rest, uint32_t& x, uint32_t& y) {
    return parseNumber(nextToken(rest), x) && parseNumber(nextToken(rest), y);
}

bool parseButton(std::string_view name, uint32_t& button) {
    button = name == "left" ? BTN_LEFT : (name == "middle" ? BTN_MIDDLE : BTN_RIGHT);

    return name == "left" || name == "middle" || name == "right";
}

bool parseState(std::string_view name, uint32_t& state) {
    state = name == "press" ? kPressedState : kReleasedState;

    return name == "press" || name == "release";
}

void movePointer(Injector& injector, uint32_t x, uint32_t y) {
    zwlr_virtual_pointer_v1_motion_absolute(injector.pointer, elapsedMilliseconds(injector), x, y,
                                            injector.outputWidth, injector.outputHeight);
    zwlr_virtual_pointer_v1_frame(injector.pointer);
}

void sendButton(Injector& injector, uint32_t button, uint32_t state) {
    zwlr_virtual_pointer_v1_button(injector.pointer, elapsedMilliseconds(injector), button, state);
    zwlr_virtual_pointer_v1_frame(injector.pointer);
}

bool runMove(Injector& injector, std::string_view rest) {
    uint32_t x = 0;
    uint32_t y = 0;

    if (!parsePosition(rest, x, y)) {
        return reportError("move needs an x and a y in output pixels");
    }

    movePointer(injector, x, y);

    return true;
}

bool runClick(Injector& injector, std::string_view rest) {
    if (!runMove(injector, rest)) {
        return false;
    }

    sendButton(injector, BTN_LEFT, kPressedState);
    sendButton(injector, BTN_LEFT, kReleasedState);

    return true;
}

bool runButton(Injector& injector, std::string_view rest) {
    uint32_t button = 0;
    uint32_t state = 0;

    if (!parseButton(nextToken(rest), button) || !parseState(nextToken(rest), state)) {
        return reportError("button needs left, middle or right, then press or release");
    }

    sendButton(injector, button, state);

    return true;
}

bool runKey(Injector& injector, std::string_view rest) {
    const std::string keysymName(nextToken(rest));
    const xkb_keysym_t keysym = xkb_keysym_from_name(keysymName.c_str(), XKB_KEYSYM_NO_FLAGS);
    uint32_t state = 0;

    if (keysym == XKB_KEY_NoSymbol || !parseState(nextToken(rest), state)) {
        return reportError("key needs an xkb keysym name, then press or release");
    }

    return sendKeysym(injector, keysym, state);
}

bool runType(Injector& injector, std::string_view rest) {
    for (const char character : trimLeading(rest)) {
        const xkb_keysym_t keysym = xkb_utf32_to_keysym(static_cast<uint32_t>(static_cast<unsigned char>(character)));

        if (!sendKeysym(injector, keysym, kPressedState) || !sendKeysym(injector, keysym, kReleasedState)) {
            return false;
        }
    }

    return true;
}

bool runSleep(Injector& injector, std::string_view rest) {
    uint32_t milliseconds = 0;

    if (!parseNumber(nextToken(rest), milliseconds)) {
        return reportError("sleep needs a duration in milliseconds");
    }

    wl_display_flush(injector.display);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));

    return true;
}

// One detent in the touchpad-coordinate units `wl_pointer.axis` measures. The value travels beside the notch
// count so a compositor that forwards only the smooth half of the pair still moves the same distance.
constexpr double kPointsPerWheelNotch = 10.0;
constexpr int32_t kUpwardWheelSign = -1;
constexpr int32_t kDownwardWheelSign = 1;

bool runWheel(Injector& injector, std::string_view rest) {
    const std::string_view direction = nextToken(rest);
    uint32_t notches = 0;

    if ((direction != "up" && direction != "down") || !parseNumber(nextToken(rest), notches)) {
        return reportError("wheel needs up or down, then a notch count");
    }

    const int32_t steps =
        static_cast<int32_t>(notches) * (direction == "up" ? kUpwardWheelSign : kDownwardWheelSign);

    zwlr_virtual_pointer_v1_axis_discrete(injector.pointer, elapsedMilliseconds(injector),
                                          WL_POINTER_AXIS_VERTICAL_SCROLL,
                                          wl_fixed_from_double(kPointsPerWheelNotch * steps), steps);
    zwlr_virtual_pointer_v1_frame(injector.pointer);

    return true;
}

struct Command {
    std::string_view name;
    bool (*run)(Injector&, std::string_view);
};

constexpr std::array<Command, 7> kCommands{{
    {.name = "move", .run = runMove},
    {.name = "click", .run = runClick},
    {.name = "button", .run = runButton},
    {.name = "wheel", .run = runWheel},
    {.name = "key", .run = runKey},
    {.name = "type", .run = runType},
    {.name = "sleep", .run = runSleep},
}};

bool executeLine(Injector& injector, std::string_view line) {
    std::string_view rest = line;
    const std::string_view name = nextToken(rest);

    if (name.empty() || name.starts_with('#')) {
        return true;
    }

    for (const Command& command : kCommands) {
        if (command.name == name) {
            return command.run(injector, rest);
        }
    }

    return reportError("unknown command " + std::string(name));
}

bool runScript(Injector& injector, std::istream& script) {
    std::string line;

    while (std::getline(script, line)) {
        if (!executeLine(injector, line)) {
            return false;
        }

        wl_display_flush(injector.display);
    }

    return wl_display_roundtrip(injector.display) != kRoundtripFailure;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    Injector injector;

    if (!connect(injector) || !createDevices(injector)) {
        return kFailureExitStatus;
    }

    // The compositor only advertises the pointer and keyboard capabilities to the application under test once it
    // has handled the two create requests above, and the application needs them before the first event arrives.
    wl_display_roundtrip(injector.display);

    if (arguments.size() <= kScriptArgument) {
        return runScript(injector, std::cin) ? kSuccessExitStatus : kFailureExitStatus;
    }

    std::ifstream file(arguments[kScriptArgument]);

    if (!file.is_open()) {
        reportError("the script file could not be opened");

        return kFailureExitStatus;
    }

    return runScript(injector, file) ? kSuccessExitStatus : kFailureExitStatus;
}
