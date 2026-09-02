#pragma once

#include <react/renderer/core/ReactPrimitives.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * Where a focus change came from, which is the whole of focus-visible: a keyboard traversal draws the focus ring
 * and a click does not, the way every desktop toolkit and every browser behaves.
 */
enum class FocusOrigin : uint8_t { Keyboard, Pointer };

enum class FocusDirection : uint8_t { Forward, Backward };

/**
 * What one focus change asks its caller to do: blur one node, focus another, and re-mark the scene.
 *
 * A tag of zero means there is nothing to do on that side — nothing held focus before, or nothing holds it now —
 * so a caller that emits one event per non-zero tag emits exactly one `blur` and one `focus` per transition, and
 * nothing at all when focus did not move. That is the double-emit prevention, by construction rather than by a
 * guard at the call site.
 *
 * `hasChanged` is broader than the two tags, because visibility can change without focus moving: a Tab press on
 * the node a click already focused moves nothing and still turns the ring on.
 */
struct FocusTransition {
    facebook::react::Tag blurredTag{0};
    facebook::react::Tag focusedTag{0};
    bool hasChanged{false};
};

/**
 * The one focused node of a surface, and the three rules that move it: traversal, click, and unmount.
 *
 * React Native has no focus concept to inherit. `BaseViewEventEmitter` carries `onFocus` and `onBlur` and nothing
 * else, so every desktop platform invents `focusable`, an order, a ring and activation keys, and then spends
 * years keeping the four consistent — thirteen react-native-macos issues' worth. This class is the part of that
 * which can be wrong arithmetically, kept where the coverage gate can see it, exactly as `TextInputV3State` is
 * for composition.
 *
 * Order is the caller's, and it is Fabric's document order: the focusable tags arrive in the pre-order walk of
 * the committed shadow tree, which is mount order and therefore already `zIndex` order. Traversal wraps at both
 * ends. Nothing here knows what made a node focusable, what it looks like, or which events its caller emits;
 * a tag is the whole vocabulary.
 *
 * Threading contract: the frame thread owns this object, like everything else the input dispatcher holds.
 */
class FocusModel final {
public:
    /**
     * Replaces the focusable set after a commit. A focused node that is no longer in it was unmounted, hidden or
     * disabled, and is blurred; focus does not move to a neighbour, because the nearest-focusable-ancestor rule
     * needs a tree and this class deliberately has none.
     */
    FocusTransition setFocusableTags(std::vector<facebook::react::Tag> focusableTags);

    /**
     * Tab and Shift+Tab. With nothing focused, forward starts at the first focusable and backward at the last,
     * which is where a wrap from outside the list lands.
     */
    FocusTransition move(FocusDirection direction);

    /**
     * Click-to-focus, and the blur that a click on anything else produces. A tag that is not focusable — the
     * surface background included, which arrives as zero — clears focus rather than leaving it where it was.
     */
    FocusTransition focusTag(facebook::react::Tag tag, FocusOrigin origin);

    facebook::react::Tag focusedTag() const noexcept;
    bool isFocusVisible() const noexcept;

private:
    bool isFocusable(facebook::react::Tag tag) const;
    FocusTransition transitionTo(facebook::react::Tag tag, bool isFocusVisible);

    std::vector<facebook::react::Tag> focusableTags_;
    facebook::react::Tag focusedTag_{0};
    bool isFocusVisible_{false};
};

/**
 * Which way a key moves focus, or nothing when it is not a traversal key. Shift is what reverses Tab, and
 * xkbcommon reports the Shift+Tab keysym as `ISO_Left_Tab`, which `domKeyName` has already turned back into
 * `Tab` by the time this sees it.
 */
std::optional<FocusDirection> focusDirectionForKey(const std::string& key, bool isShiftDown);

/**
 * Whether a key activates the focused control. Enter and Space are what react-native-macos#1622 asked for and
 * what every desktop toolkit does; the platform turns them into the same synthetic click a press and a release
 * on one target produce, so `Pressability` needs no keyboard path of its own.
 */
bool isActivationKey(const std::string& key);

/**
 * Whether a focused component owns a text cursor, and therefore whether the compositor's text input is enabled
 * while it holds focus. No component registers this name yet; issue #17 is what makes one, and this is the whole
 * of what it has to satisfy. See *Focus and keyboard* in docs/cpp-toolchain.md.
 */
bool isTextInputComponent(const std::string& componentName);

} // namespace react_native_linux
