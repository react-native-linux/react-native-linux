#pragma once

#include "DimensionsSource.h"

#include <react/renderer/components/modal/ModalHostViewShadowNode.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/EventQueue.h>
#include <react/renderer/core/Props.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/core/State.h>
#include <react/renderer/graphics/Size.h>

namespace react_native_linux {

/**
 * The `ContextContainer` key under which `FabricHost` puts its surface's `std::shared_ptr<const DimensionsSource>`,
 * the one `ReactHost::dimensions()` owns and `Dimensions.get` answers from.
 */
extern const char kDimensionsSourceContextKey[];

/**
 * `<Modal>`'s descriptor on this platform (ADR-0005): upstream's shadow node, props, state and event emitter, so
 * `Modal.js` runs unmodified, with Android's answer to the first frame's size.
 *
 * Upstream's `ModalHostViewComponentDescriptor` is `final`, and outside Android its initial state comes from
 * `ModalHostViewScreenSize()`, which the `cxx` platform answers with 0x0. The first commit would then lay the
 * modal's children out at zero size, react-native#50442. `createInitialState` reads the surface's logical size
 * from the `DimensionsSource` in the context container instead, so the first commit is already full-size.
 * `DimensionsSource`'s mutex is what makes that read safe on whichever thread commits.
 *
 * `adopt` is upstream's: the size comes from the state, and the position is absolute.
 */
class ModalHostComponentDescriptor final
    : public facebook::react::ConcreteComponentDescriptor<facebook::react::ModalHostViewShadowNode> {
public:
    using ConcreteComponentDescriptor::ConcreteComponentDescriptor;

    void adopt(facebook::react::ShadowNode& shadowNode) const override;

    facebook::react::State::Shared
    createInitialState(const facebook::react::Props::Shared& props,
                       const facebook::react::ShadowNodeFamily::Shared& family) const override;
};

/** The logical surface size `dimensions` reports, as a modal's state holds it. */
facebook::react::Size surfaceSizeOf(const DimensionsSource& dimensions);

/** Asks for `size` through `modalState`'s family when the state holds another size; otherwise does nothing. */
void requestModalSize(const facebook::react::State::Shared& modalState, facebook::react::Size size,
                      facebook::react::EventQueue::UpdateMode updateMode);

/**
 * Sets every `ModalHostView` under `root` whose state is not `surfaceSize` to it, as iOS's component view does when
 * its bounds change. The update is immediate, so the commit it causes lands in the frame of the resize that asked
 * for it rather than one beat later.
 *
 * The resize contract has two halves, and this is the first. A resize configures the `DimensionsSource` first and
 * then calls `FabricHost::setSurfaceSize`, which lays the surface out and then walks the committed tree with this.
 * A modal is therefore sized correctly in one of three ways:
 * - created after the configure: `createInitialState` reads the new size;
 * - committed before the walk: the walk updates it;
 * - created before the configure but committed after the walk: neither of those reaches it. Its mount does.
 *   `LinuxMountingManager` compares a `ModalHostView`'s state with the `DimensionsSource` when the node is
 *   inserted, and asks for the difference through `requestModalSize` at the next event beat.
 *
 * Threading contract: called on the frame thread that resizes the surface, after `constraintLayout`, and outside
 * the shadow tree registry's `visit`, whose lock the immediate commit takes again.
 */
void resizePresentedModals(const facebook::react::ShadowNode& root, facebook::react::Size surfaceSize);

} // namespace react_native_linux
