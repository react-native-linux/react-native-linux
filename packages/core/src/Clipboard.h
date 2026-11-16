#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * The clipboard Ctrl+C, Ctrl+X and Ctrl+V move text through, and that `RNCClipboard` (#23) reads and writes.
 *
 * This is the process's copy of the text. Under a compositor, `WaylandClipboard` (#60) keeps it in step with the
 * seat's `wl_data_device` selection: `takeClipboardPublish` tells it a copy was made here so it can offer the text
 * to other applications, and `beginClipboardReceive` and `receiveClipboardText` are how another application's
 * selection replaces it. Without a compositor — `hello_react` and the unit suite — nothing takes or receives, and
 * the text never leaves the process.
 *
 * Threading contract: the frame thread reads and writes it for the text field's shortcuts and for the Wayland
 * selection, and the JavaScript thread through `RNCClipboard`, so every function takes one mutex and the text is
 * copied out.
 */
std::string clipboardText();
void setClipboardText(std::string text);

/** True once per `setClipboardText` call, for the system selection to offer that text. */
bool takeClipboardPublish();

/**
 * Another application's selection replaced the clipboard: empties it, so a paste before its text arrives is empty
 * rather than stale, and returns the revision `receiveClipboardText` stores that text under. Nothing while a copy
 * made here still waits for `takeClipboardPublish`, because publishing it supersedes that selection.
 */
std::optional<uint64_t> beginClipboardReceive();

/** Stores another application's text, unless a copy or a newer selection changed the clipboard since `revision`. */
bool receiveClipboardText(std::string text, uint64_t revision);

/** The text types offered on copy, in the order they are preferred on paste. */
inline constexpr std::array<std::string_view, 3> kClipboardTextMimeTypes{"text/plain;charset=utf-8", "text/plain",
                                                                         "UTF8_STRING"};

/** The type a paste asks an offer for, or nothing when the offer carries no text. */
std::optional<std::string_view> preferredClipboardMimeType(const std::vector<std::string>& offeredMimeTypes);

} // namespace react_native_linux
