#include "LinuxMountingManager.h"

#include <glog/logging.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {

void LinuxMountingManager::startSurface(facebook::react::SurfaceId surfaceId, facebook::react::Size size) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.createSurfaceRoot(surfaceId, size);
}

void LinuxMountingManager::damageImageSource(const std::string& uri) {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    scene_.damageImageSource(uri);
    hasPendingDamage_ = true;
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

MountDiagnostics LinuxMountingManager::mountDiagnostics() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return diagnostics_;
}

bool LinuxMountingManager::hasPendingDamage() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return hasPendingDamage_;
}

SceneSnapshot LinuxMountingManager::snapshotScene() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.snapshot();
}

std::string LinuxMountingManager::dumpScene() const {
    const std::lock_guard<std::mutex> guard(sceneMutex_);

    return scene_.dump();
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
                scene_.updateNode(mutation.newChildShadowView);
                break;
        }
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

    for (const std::string& propName : scene_.applyAnimatedProps(tag, props)) {
        reportRejectedAnimatedProp(propName);
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

void LinuxMountingManager::reportRejectedAnimatedProp(const std::string& propName) {
    if (diagnostics_.rejectedAnimatedProps == 0) {
        diagnostics_.firstRejectedAnimatedProp = propName;

        LOG(ERROR) << "[mounting] synchronous update carries prop " << propName
                   << ", which the non-layout fast path does not apply";
    }

    diagnostics_.rejectedAnimatedProps++;
}

} // namespace react_native_linux
