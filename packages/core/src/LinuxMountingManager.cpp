#include "LinuxMountingManager.h"

#include "ModalHostComponent.h"

#include <functional>
#include <glog/logging.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

namespace react_native_linux {

namespace {

constexpr std::string_view kSafeAreaProviderComponentName = "RNCSafeAreaProvider";

/**
 * What iOS's and Android's `RNCSafeAreaProvider` views do when they are laid out (#806): send react-native-safe-area-
 * context's `insetsChange`, again whenever the frame changes. Its `SafeAreaProvider` renders no children until the
 * first one arrives. An `Insert` is the first layout; an `Update` whose frame differs is a later one, a resize too.
 *
 * The insets are always zero: a desktop window has no notch, and the surface React lays out already excludes the
 * title bar this platform draws client-side (`contentExtentOf`).
 *
 * The frame is the provider's layout frame, relative to its parent. iOS and Android report it in root coordinates,
 * and the two agree only for a provider at the root of the surface, which is where apps mount it. A nested provider
 * under an offset reports that offset wrongly, and an ancestor that moves sends nothing. #161 owns both: the scene
 * has no absolute-origin query to answer them with yet.
 */
void reportSafeAreaInsetsIfProvider(const facebook::react::ShadowView* previous,
                                    const facebook::react::ShadowView& next) {
    const facebook::react::Rect& frame = next.layoutMetrics.frame;

    if (std::string_view{next.componentName} != kSafeAreaProviderComponentName ||
        (previous != nullptr && previous->layoutMetrics.frame == frame)) {
        return;
    }

    next.eventEmitter->dispatchEvent(
        "insetsChange",
        folly::dynamic::object("insets", folly::dynamic::object("top", 0)("right", 0)("bottom", 0)("left", 0))(
            "frame", folly::dynamic::object("x", frame.origin.x)("y", frame.origin.y)("width", frame.size.width)(
                         "height", frame.size.height)));
}

/** A mutation's view names no component in some fixtures, and `std::string_view` of a null pointer is undefined. */
bool isModalHostView(const facebook::react::ShadowView& shadowView) {
    return shadowView.componentName != nullptr &&
           std::string_view{shadowView.componentName} == facebook::react::ModalHostViewComponentName;
}

std::string supportedAnimatedPropNames() {
    std::string names;

    for (const AnimatablePropEntry& entry : kAnimatableProps) {
        if (!names.empty()) {
            names += ", ";
        }

        names += entry.name;
    }

    return names;
}

} // namespace

std::string rejectedAnimatedPropMessage(std::string_view propName, AnimatedPropRejection rejection) {
    const std::string prefix = "[mounting] synchronous update carries prop " + std::string{propName};

    if (rejection == AnimatedPropRejection::NonFinite) {
        return prefix + " with a non-finite value, which the Linux native driver cannot paint; supported: " +
               supportedAnimatedPropNames() +
               " — check the interpolation output range or animate it with useNativeDriver: false";
    }

    return prefix + ", which the Linux native driver cannot animate; supported: " + supportedAnimatedPropNames() +
           " — animate it with useNativeDriver: false or file an issue";
}

std::string rejectedNonFinitePropMessage(facebook::react::Tag tag, std::string_view propName) {
    return "[mounting] node " + std::to_string(tag) + " carries a non-finite " + std::string{propName} +
           ", which the Linux scene refuses rather than painting: a NaN frame damages nothing, paints an "
           "unspecified rectangle and answers every hit test — the prop kept its default instead";
}

void LinuxMountingManager::startSurface(facebook::react::SurfaceId surfaceId, facebook::react::Size size) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.createSurfaceRoot(surfaceId, size);
}

void LinuxMountingManager::damageImageSource(const std::string& uri,
                                             const std::shared_ptr<const DecodedImageFrames>& decoded) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.damageImageSource(uri, decoded);
    hasPendingDamage_ = true;
}

void LinuxMountingManager::setDecodedImageProvider(RetainedScene::DecodedImageProvider decodedImages) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.setDecodedImageProvider(std::move(decodedImages));
}

void LinuxMountingManager::setPlaceholderImageDecodeRequester(RetainedScene::ImageDecodeRequester requester) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.setPlaceholderImageDecodeRequester(std::move(requester));
}

void LinuxMountingManager::setDimensionsSource(std::shared_ptr<const DimensionsSource> dimensions) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    dimensions_ = std::move(dimensions);
}

bool LinuxMountingManager::hasOverlays() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.hasOverlays();
}

std::vector<std::shared_ptr<const facebook::react::ModalHostViewEventEmitter>>
LinuxMountingManager::takeShownOverlays() {
    const std::lock_guard<std::mutex> guard(sceneMutex_);
    std::vector<std::shared_ptr<const facebook::react::ModalHostViewEventEmitter>> shown;

    for (const PendingShow& painted : std::exchange(paintedOverlays_, {})) {
        shown.push_back(painted.emitter);
    }

    return shown;
}

facebook::react::Tag LinuxMountingManager::topmostOverlayTag(facebook::react::SurfaceId surfaceId) const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.topmostOverlayTag(surfaceId);
}

bool LinuxMountingManager::advanceImageAnimations(double frameMilliseconds) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);
    const bool hasAdvanced = scene_.advanceImageAnimations(frameMilliseconds);

    hasPendingDamage_ = hasPendingDamage_ || hasAdvanced;

    return hasAdvanced;
}

bool LinuxMountingManager::advanceControlAnimations(double frameMilliseconds) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);
    const bool hasAdvanced = scene_.advanceControlAnimations(frameMilliseconds);

    hasPendingDamage_ = hasPendingDamage_ || hasAdvanced;

    return hasAdvanced;
}

void LinuxMountingManager::setFocus(facebook::react::Tag tag, bool isFocusVisible) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.setFocus(tag, isFocusVisible);
    hasPendingDamage_ = true;
}

void LinuxMountingManager::setEditorState(facebook::react::Tag tag, const SceneEditorState& editorState) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    const bool hasChanged = scene_.setEditorState(tag, editorState);

    hasPendingDamage_ = hasPendingDamage_ || hasChanged;
}

SceneFrame LinuxMountingManager::takeFrame() {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    hasPendingDamage_ = false;

    paintedOverlays_.insert(paintedOverlays_.end(), unpaintedOverlays_.begin(), unpaintedOverlays_.end());
    unpaintedOverlays_.clear();

    return SceneFrame{.scene = scene_.snapshot(), .damage = scene_.takeDamage()};
}

std::vector<SceneCommand> LinuxMountingManager::takeCommands() {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return std::exchange(commands_, std::vector<SceneCommand>{});
}

std::vector<MaintainedScrollOffset> LinuxMountingManager::takeMaintainedScrollOffsets() {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return std::exchange(maintainedScrollOffsets_, std::vector<MaintainedScrollOffset>{});
}

std::vector<AccessibilityChange> LinuxMountingManager::takeAccessibilityChanges() {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return std::exchange(accessibilityChanges_, std::vector<AccessibilityChange>{});
}

MountDiagnostics LinuxMountingManager::mountDiagnostics() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return diagnostics_;
}

bool LinuxMountingManager::hasPendingDamage() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return hasPendingDamage_;
}

SceneHit LinuxMountingManager::findNodeAtPoint(facebook::react::SurfaceId surfaceId,
                                               facebook::react::Point surfacePoint) const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.findNodeAtPoint(surfaceId, surfacePoint);
}

SceneSnapshot LinuxMountingManager::snapshotScene() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.snapshot();
}

std::string LinuxMountingManager::dumpScene() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.dump();
}

SceneNodes LinuxMountingManager::visualTreeNodes() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.nodes();
}

void LinuxMountingManager::recordAccessibilityChangeIfAny(
    const facebook::react::ShadowView& next,
    std::unordered_map<facebook::react::Tag, std::size_t>& changedThisTransaction) {
    const auto previous = scene_.nodes().find(next.tag);

    if (previous == scene_.nodes().end()) {
        return;
    }

    const std::shared_ptr<const facebook::react::ViewProps> nextProps =
        std::dynamic_pointer_cast<const facebook::react::ViewProps>(next.props);

    if (nextProps == nullptr) {
        return;
    }

    const SceneAccessibility& previousAccessibility = previous->second.accessibility;
    const bool stateChanged = previousAccessibility.state != nextProps->accessibilityState;
    const bool valueChanged = !(previousAccessibility.value == nextProps->accessibilityValue);

    if (!stateChanged && !valueChanged) {
        return;
    }

    const auto existing = changedThisTransaction.find(next.tag);

    if (existing != changedThisTransaction.end()) {
        AccessibilityChange& change = accessibilityChanges_[existing->second];

        // Bitwise, not `||`: both operands are pure bools, and `||`'s short-circuit branch is one llvm-cov cannot
        // be driven to both outcomes without a test that exists only to please the coverage gate.
        change.stateChanged = change.stateChanged | stateChanged;
        change.valueChanged = change.valueChanged | valueChanged;
        change.testId = nextProps->testId;
        return;
    }

    changedThisTransaction.emplace(next.tag, accessibilityChanges_.size());
    accessibilityChanges_.push_back(AccessibilityChange{
        .tag = next.tag, .stateChanged = stateChanged, .valueChanged = valueChanged, .testId = nextProps->testId});
}

void LinuxMountingManager::executeMount(facebook::react::SurfaceId surfaceId,
                                        facebook::react::MountingTransaction&& mountingTransaction) {
    applyTransaction(std::move(mountingTransaction));

    if (onAfterMount_) {
        onAfterMount_(surfaceId);
    }
}

void LinuxMountingManager::setAfterMountCallback(std::function<void(facebook::react::SurfaceId)>&& onAfterMount) {
    onAfterMount_ = std::move(onAfterMount);
}

void LinuxMountingManager::applyTransaction(facebook::react::MountingTransaction&& mountingTransaction) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    lastTransactionNumber_ = mountingTransaction.getNumber();

    if (!mountingTransaction.getMutations().empty()) {
        hasPendingDamage_ = true;
    }

    std::unordered_map<facebook::react::Tag, std::size_t> accessibilityChangesThisTransaction;

    for (const facebook::react::ShadowViewMutation& mutation : mountingTransaction.getMutations()) {
        switch (mutation.type) { // COV_EXCL: every ShadowViewMutation::Type value has a case, so the implicit no-match
                                 // branch cannot execute
        case facebook::react::ShadowViewMutation::Create:
            verifyPropsDeriveFromViewProps(mutation.newChildShadowView);
            scene_.createNode(mutation.newChildShadowView);

            // On the create, not the insert: a modal reordered or reparented is removed and inserted again, and a
            // second `onShow` would run the app's show handler, and its focus, twice.
            if (isModalHostView(mutation.newChildShadowView)) {
                unpaintedOverlays_.push_back(
                    PendingShow{.tag = mutation.newChildShadowView.tag,
                                .emitter = std::static_pointer_cast<const facebook::react::ModalHostViewEventEmitter>(
                                    mutation.newChildShadowView.eventEmitter)});
            }
            break;
        case facebook::react::ShadowViewMutation::Delete:
            verifyTagIsKnown("Delete", mutation.oldChildShadowView.tag);
            scene_.deleteNode(mutation.oldChildShadowView.tag);
            std::erase_if(unpaintedOverlays_, [&mutation](const PendingShow& pending) {
                return pending.tag == mutation.oldChildShadowView.tag;
            });
            std::erase_if(paintedOverlays_, [&mutation](const PendingShow& pending) {
                return pending.tag == mutation.oldChildShadowView.tag;
            });
            break;
        case facebook::react::ShadowViewMutation::Insert:
            scene_.insertChild(mutation.parentTag, mutation.newChildShadowView, mutation.index);
            reportSafeAreaInsetsIfProvider(nullptr, mutation.newChildShadowView);

            if (dimensions_ != nullptr && isModalHostView(mutation.newChildShadowView)) {
                requestModalSize(mutation.newChildShadowView.state, surfaceSizeOf(*dimensions_),
                                 facebook::react::EventQueue::UpdateMode::Asynchronous);
            }
            break;
        case facebook::react::ShadowViewMutation::Remove:
            verifyTagIsKnown("Remove", mutation.oldChildShadowView.tag);
            scene_.removeChild(mutation.parentTag, mutation.oldChildShadowView);
            break;
        case facebook::react::ShadowViewMutation::Update:
            verifyTagIsKnown("Update", mutation.newChildShadowView.tag);
            recordAccessibilityChangeIfAny(mutation.newChildShadowView, accessibilityChangesThisTransaction);
            scene_.updateNode(mutation.newChildShadowView);
            reportSafeAreaInsetsIfProvider(&mutation.oldChildShadowView, mutation.newChildShadowView);
            break;
        }
    }

    // After the last mutation and before the mutex is dropped, so no frame can be taken between the children this
    // transaction prepended and the offset that holds the visible one still. That is what makes the adjustment
    // part of the mount rather than of the frame after it.
    for (const MaintainedScrollOffset& maintained : scene_.maintainScrollPositions()) {
        maintainedScrollOffsets_.push_back(maintained);
    }

    for (const RejectedNonFiniteProp& rejectedProp : scene_.takeRejectedNonFiniteProps()) {
        reportRejectedNonFiniteProp(rejectedProp);
    }
}

void LinuxMountingManager::dispatchCommand(const facebook::react::ShadowView& shadowView,
                                           const std::string& commandName, const folly::dynamic& args) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    verifyTagIsKnown("Command", shadowView.tag);
    commands_.push_back(SceneCommand{.tag = shadowView.tag, .name = commandName, .args = args});
}

void LinuxMountingManager::synchronouslyUpdateViewOnUIThread(facebook::react::Tag tag, const folly::dynamic& props) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    if (!verifyTagIsKnown("SyncUpdate", tag)) {
        return;
    }

    for (const RejectedAnimatedProp& rejectedProp : scene_.applyAnimatedProps(tag, props)) {
        reportRejectedAnimatedProp(rejectedProp);
    }

    hasPendingDamage_ = true;
}

bool LinuxMountingManager::verifyTagIsKnown(std::string_view operation, facebook::react::Tag tag) {
    if (scene_.hasNode(tag)) {
        return true;
    }

    if (diagnostics_.unknownTagOperations == 0) {
        diagnostics_.firstUnknownOperation = operation;
        diagnostics_.firstUnknownTag = tag;
        diagnostics_.firstUnknownTransactionNumber = lastTransactionNumber_;

        LOG(ERROR) << "[mounting] " << operation << " names tag " << tag
                   << ", which the scene does not hold, in transaction " << lastTransactionNumber_;
    }

    diagnostics_.unknownTagOperations++;

    return false;
}

void LinuxMountingManager::reportRejectedAnimatedProp(const RejectedAnimatedProp& rejectedProp) {
    if (diagnostics_.rejectedAnimatedProps == 0) {
        diagnostics_.firstRejectedAnimatedProp = rejectedProp.name;

        LOG(ERROR) << rejectedAnimatedPropMessage(rejectedProp.name, rejectedProp.rejection);
    }

    diagnostics_.rejectedAnimatedProps++;
}

void LinuxMountingManager::reportRejectedNonFiniteProp(const RejectedNonFiniteProp& rejectedProp) {
    if (diagnostics_.rejectedNonFiniteProps == 0) {
        diagnostics_.firstRejectedNonFiniteProp = std::string{rejectedProp.propName};
        diagnostics_.firstRejectedNonFiniteTag = rejectedProp.tag;

        LOG(ERROR) << rejectedNonFinitePropMessage(rejectedProp.tag, rejectedProp.propName);
    }

    diagnostics_.rejectedNonFiniteProps++;
}

void LinuxMountingManager::verifyPropsDeriveFromViewProps(const facebook::react::ShadowView& shadowView) {
    if (shadowView.props == nullptr ||
        dynamic_cast<const facebook::react::ViewProps*>(shadowView.props.get()) != nullptr) {
        return;
    }

    if (diagnostics_.componentsWithoutViewProps == 0) {
        diagnostics_.firstComponentWithoutViewProps = shadowView.componentName;

        LOG(ERROR) << "[mounting] '" << shadowView.componentName
                   << "' mounts with props that do not derive from ViewProps, so react-native-linux paints nothing "
                      "for it; a component that only lays out mounts as a View when its props extend ViewProps. See "
                      "docs/ecosystem/linux-support-contract.md";
    }

    diagnostics_.componentsWithoutViewProps++;
}

} // namespace react_native_linux
