#include "LinuxMountingManager.h"

#include <react/renderer/mounting/ShadowViewMutation.h>

#include <mutex>
#include <string>

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

    if (!mountingTransaction.getMutations().empty()) {
        hasPendingDamage_ = true;
    }

    for (const facebook::react::ShadowViewMutation& mutation : mountingTransaction.getMutations()) {
        switch (mutation.type) { // COV_EXCL: every ShadowViewMutation::Type value has a case, so the implicit no-match branch cannot execute
            case facebook::react::ShadowViewMutation::Create:
                scene_.createNode(mutation.newChildShadowView);
                break;
            case facebook::react::ShadowViewMutation::Delete:
                scene_.deleteNode(mutation.oldChildShadowView.tag);
                break;
            case facebook::react::ShadowViewMutation::Insert:
                scene_.insertChild(mutation.parentTag, mutation.newChildShadowView, mutation.index);
                break;
            case facebook::react::ShadowViewMutation::Remove:
                scene_.removeChild(mutation.parentTag, mutation.oldChildShadowView);
                break;
            case facebook::react::ShadowViewMutation::Update:
                scene_.updateNode(mutation.newChildShadowView);
                break;
        }
    }
}

void LinuxMountingManager::dispatchCommand(const facebook::react::ShadowView& /*shadowView*/,
                                           const std::string& /*commandName*/,
                                           const folly::dynamic& /*args*/) {}

} // namespace react_native_linux
