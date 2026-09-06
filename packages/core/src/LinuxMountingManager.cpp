#include "LinuxMountingManager.h"

#include <glog/logging.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

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

    scene_.setEditorState(tag, editorState);
    hasPendingDamage_ = true;
}

SceneFrame LinuxMountingManager::takeFrame() {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    hasPendingDamage_ = false;

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

void LinuxMountingManager::recordAccessibilityChangeIfAny(const facebook::react::ShadowView& next) {
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

    accessibilityChanges_.push_back(
        AccessibilityChange{.tag = next.tag, .stateChanged = stateChanged, .valueChanged = valueChanged});
}

void LinuxMountingManager::executeMount(facebook::react::SurfaceId /*surfaceId*/,
                                        facebook::react::MountingTransaction&& mountingTransaction) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    lastTransactionNumber_ = mountingTransaction.getNumber();

    if (!mountingTransaction.getMutations().empty()) {
        hasPendingDamage_ = true;
    }

    for (const facebook::react::ShadowViewMutation& mutation : mountingTransaction.getMutations()) {
        switch (mutation.type) { // COV_EXCL: every ShadowViewMutation::Type value has a case, so the implicit no-match branch cannot execute
            case facebook::react::ShadowViewMutation::Create:
                scene_.createNode(mutation.newChildShadowView);
                break;
            case facebook::react::ShadowViewMutation::Delete:
                verifyTagIsKnown("Delete", mutation.oldChildShadowView.tag);
                scene_.deleteNode(mutation.oldChildShadowView.tag);
                break;
            case facebook::react::ShadowViewMutation::Insert:
                scene_.insertChild(mutation.parentTag, mutation.newChildShadowView, mutation.index);
                break;
            case facebook::react::ShadowViewMutation::Remove:
                verifyTagIsKnown("Remove", mutation.oldChildShadowView.tag);
                scene_.removeChild(mutation.parentTag, mutation.oldChildShadowView);
                break;
            case facebook::react::ShadowViewMutation::Update:
                verifyTagIsKnown("Update", mutation.newChildShadowView.tag);
                recordAccessibilityChangeIfAny(mutation.newChildShadowView);
                scene_.updateNode(mutation.newChildShadowView);
                break;
        }
    }

    // After the last mutation and before the mutex is dropped, so no frame can be taken between the children this
    // transaction prepended and the offset that holds the visible one still. That is what makes the adjustment
    // part of the mount rather than of the frame after it.
    for (const MaintainedScrollOffset& maintained : scene_.maintainScrollPositions()) {
        maintainedScrollOffsets_.push_back(maintained);
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

} // namespace react_native_linux
