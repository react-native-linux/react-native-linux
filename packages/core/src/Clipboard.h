#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * The clipboard Ctrl+C, Ctrl+X and Ctrl+V move text through, and that `RNCClipboard` (#23) reads and writes.
 *
 * This is the process's copy of the text. Under a compositor, `WaylandClipboard` (#60) keeps it in step with the
 * seat's `wl_data_device` selection: `takeClipboardPublish` hands it what was copied here so it can offer it to
 * other applications, and `receiveClipboardText` is how it stores what another application copied. Without a
 * compositor — `hello_react` and the unit suite — nothing takes or receives, and the text never leaves the process.
 *
 * Threading contract: the frame thread reads and writes it for the text field's shortcuts and for the Wayland
 * selection, and the JavaScript thread through `RNCClipboard`, so every function takes one mutex and the text is
 * copied out.
 */
std::string clipboardText();
void setClipboardText(std::string text);

/** The text `setClipboardText` stored, once per call to it, for the system selection to offer; otherwise empty. */
std::optional<std::string> takeClipboardPublish();

/** Another application's selection became the clipboard: stored without anything to publish. */
void receiveClipboardText(std::string text);

/** The text types offered on copy, in the order they are preferred on paste. */
inline constexpr std::array<std::string_view, 3> kClipboardTextMimeTypes{"text/plain;charset=utf-8", "text/plain",
                                                                         "UTF8_STRING"};

/** The type a paste asks an offer for, or nothing when the offer carries no text. */
std::optional<std::string_view> preferredClipboardMimeType(const std::vector<std::string>& offeredMimeTypes);

} // namespace react_native_linux
