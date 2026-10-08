#pragma once

#include <vector>

#include <react/renderer/componentregistry/ComponentDescriptorProvider.h>

namespace react_native_linux {

/**
 * The component descriptors of autolinked libraries (#149), appended by the generated `rnl_autolinking.cpp`'s
 * static initializer, which runs before `main`, and added to every `FabricHost`'s registry after the built-in
 * components. A component name that is already registered, a built-in's or another library's, is refused with a
 * message naming it, rather than one descriptor silently replacing the other.
 *
 * Threading contract: written only before `main`, read only afterwards, so it needs no lock.
 */
std::vector<facebook::react::ComponentDescriptorProvider>& autolinkedComponentDescriptorProviders();

} // namespace react_native_linux
