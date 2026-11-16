#pragma once

#include <jsi/jsi.h>

namespace react_native_linux {

/**
 * Installs `globalThis.Intl` (#83): `DateTimeFormat`, `NumberFormat`, `Collator`, `DisplayNames` and `Locale` over
 * `Intl.h` (#772), and `getCanonicalLocales`. Hermes' own Intl stays off on Linux, because its ICU backend is
 * unfinished and upstream deleted it; see the decision on #83.
 *
 * Threading contract: runs on the JavaScript thread before the bundle, and every object it installs is used there.
 */
void installIntl(facebook::jsi::Runtime& runtime);

} // namespace react_native_linux
