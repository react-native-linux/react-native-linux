#include "SurfaceCommitGate.h"

namespace react_native_linux {

SurfaceCommitAction surfaceCommitActionFor(const SurfaceCommitState& state) noexcept {
    if (!state.isConfigureAcknowledged) {
        return SurfaceCommitAction::WaitForConfigure;
    }

    if (!state.doesBufferExtentMatchConfigure) {
        return SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent;
    }

    if (state.consecutiveAcquireStarvations >= kAcquireStarvationLimit &&
        state.extraSwapchainImages < kMaxExtraSwapchainImages) {
        return SurfaceCommitAction::RecreateSwapchainWithMoreImages;
    }

    if (state.wasLastContentUpdateDiscarded) {
        return SurfaceCommitAction::RepresentDiscardedFrame;
    }

    return SurfaceCommitAction::AttachBuffer;
}

SurfaceCommitState applySurfaceCommitFault(const SurfaceCommitState& state, SurfaceCommitFault fault) noexcept {
    SurfaceCommitState faultedState = state;

    switch (fault) { // COV_EXCL: every SurfaceCommitFault value has a case, so no-match cannot execute
    case SurfaceCommitFault::None:
        break;
    case SurfaceCommitFault::CommitBeforeConfigure:
        faultedState.isConfigureAcknowledged = false;
        break;
    case SurfaceCommitFault::BufferExtentMismatch:
        faultedState.doesBufferExtentMatchConfigure = false;
        break;
    case SurfaceCommitFault::AcquireStarvation:
        faultedState.consecutiveAcquireStarvations = kAcquireStarvationLimit;
        break;
    case SurfaceCommitFault::ContentUpdateDiscarded:
        faultedState.wasLastContentUpdateDiscarded = true;
        break;
    }

    return faultedState;
}

std::string_view describeSurfaceCommitAction(SurfaceCommitAction action) noexcept {
    switch (action) { // COV_EXCL: every SurfaceCommitAction value has a case, so no-match cannot execute
    case SurfaceCommitAction::WaitForConfigure:
        return "wait-for-configure";
    case SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent:
        return "recreate-swapchain-at-configured-extent";
    case SurfaceCommitAction::RecreateSwapchainWithMoreImages:
        return "recreate-swapchain-with-more-images";
    case SurfaceCommitAction::RepresentDiscardedFrame:
        return "represent-discarded-frame";
    case SurfaceCommitAction::AttachBuffer:
        return "attach-buffer";
    }

    return "attach-buffer"; // COV_EXCL: every SurfaceCommitAction value has a case above, so this cannot execute
}

std::string_view describeSurfaceCommitFault(SurfaceCommitFault fault) noexcept {
    switch (fault) { // COV_EXCL: every SurfaceCommitFault value has a case, so no-match cannot execute
    case SurfaceCommitFault::None:
        return "none";
    case SurfaceCommitFault::CommitBeforeConfigure:
        return "commit-before-configure";
    case SurfaceCommitFault::BufferExtentMismatch:
        return "buffer-extent-mismatch";
    case SurfaceCommitFault::AcquireStarvation:
        return "acquire-starvation";
    case SurfaceCommitFault::ContentUpdateDiscarded:
        return "content-update-discarded";
    }

    return "none"; // COV_EXCL: every SurfaceCommitFault value has a case above, so this cannot execute
}

} // namespace react_native_linux
