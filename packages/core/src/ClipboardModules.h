#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `RNCClipboard` (#23), the TurboModule `@react-native-clipboard/clipboard` resolves, since React Native itself no
 * longer ships a clipboard: `getString`, `setString` and `hasString` over `clipboardText()` and `setClipboardText()`,
 * the clipboard the text field's Ctrl+C and Ctrl+V use, so text copied in a field pastes from JavaScript and the
 * other way round. Its images, URL and number detection, and change events are absent.
 */
std::shared_ptr<facebook::react::TurboModule>
makeClipboardModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

/**
 * `ExpoClipboard` (ADR-0004, #278), which upstream's unmodified `expo-clipboard` requires as it is imported:
 * `setStringAsync`, `getStringAsync` and `hasStringAsync` over the same clipboard. It holds plain text only, so a
 * `preferredFormat` of `html` reads that text, as upstream does when no HTML was copied, and an `inputFormat` of
 * `html` rejects rather than storing markup as text. Images, URLs and `onClipboardChanged` are not answered, so
 * upstream rejects their calls with `UnavailabilityError`, and `ClipboardPasteButton` is iOS-only upstream.
 *
 * Threading contract, for both modules: every method runs on the JavaScript thread.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoClipboardModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
