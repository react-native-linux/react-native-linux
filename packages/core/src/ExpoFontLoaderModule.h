#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoFontLoader` (ADR-0004), which upstream's unmodified `expo-font` requires as it is imported: its
 * `typeof window === 'undefined'` guard is false under React Native. `loadAsync(fontFamily, localUri)` and
 * `loadFontFamilyAsync(fontFamily, faces)` register the font files the app's `Font.loadAsync` and `useFonts`
 * downloaded through `expo-asset` into the text pipeline's application fonts (`registerApplicationFont`), and
 * `getLoadedFonts` answers every name those fonts resolve by, which upstream's `isLoaded` reads. A face's `weight` and
 * `style` are not applied: Skia matches each face by the weight and slant its file declares.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here; the registration takes the text
 * pipeline's own lock, which the layout and render threads shape under.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoFontLoaderModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
