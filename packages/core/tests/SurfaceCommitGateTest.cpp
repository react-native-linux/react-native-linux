#include "SurfaceCommitGate.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>

namespace {

using react_native_linux::applySurfaceCommitFault;
using react_native_linux::describeSurfaceCommitAction;
using react_native_linux::describeSurfaceCommitFault;
using react_native_linux::kAcquireStarvationLimit;
using react_native_linux::kMaxExtraSwapchainImages;
using react_native_linux::SurfaceCommitAction;
using react_native_linux::surfaceCommitActionFor;
using react_native_linux::SurfaceCommitFault;
using react_native_linux::SurfaceCommitState;

struct GateRow {
    std::string_view name;
    SurfaceCommitState state;
    SurfaceCommitAction action;
};

// The whole rule as data, including every case where two causes hold at once, because precedence is the part of
// this rule that a chain of `if`s gets wrong silently: a client that rebuilds its swapchain for a stale extent
// while the initial configure is still unacknowledged has attached nothing and has learnt nothing.
constexpr std::array<GateRow, 14> kGateTable{{
    {"no configure yet, nothing else known",
     {.isConfigureAcknowledged = false,
      .doesBufferExtentMatchConfigure = false,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = 0,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::WaitForConfigure},
    {"no configure yet, everything else ready",
     {.isConfigureAcknowledged = false,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = 0,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::WaitForConfigure},
    {"no configure yet outranks a starved acquire and a discard",
     {.isConfigureAcknowledged = false,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = true,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::WaitForConfigure},
    {"configured, extent disagrees",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = false,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = 0,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent},
    {"a stale extent outranks a starved acquire and a discard",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = false,
      .wasLastContentUpdateDiscarded = true,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent},
    {"one starved acquire below the limit is not yet a shortage",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit - 1,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::AttachBuffer},
    {"starvation at the limit rebuilds with one more image",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RecreateSwapchainWithMoreImages},
    {"starvation past the limit rebuilds too",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit + 1,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RecreateSwapchainWithMoreImages},
    {"a shortage outranks a discard, because a discarded frame cannot be re-presented without an image",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = true,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RecreateSwapchainWithMoreImages},
    {"a discard re-presents",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = true,
      .consecutiveAcquireStarvations = 0,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RepresentDiscardedFrame},
    {"a discard below the starvation limit still re-presents",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = true,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit - 1,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::RepresentDiscardedFrame},
    {"a swapchain already grown to the ceiling stops growing: this is an occluded window, not a shortage",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit,
      .extraSwapchainImages = kMaxExtraSwapchainImages},
     SurfaceCommitAction::AttachBuffer},
    {"one image below the ceiling still grows",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = kAcquireStarvationLimit,
      .extraSwapchainImages = kMaxExtraSwapchainImages - 1},
     SurfaceCommitAction::RecreateSwapchainWithMoreImages},
    {"the ordinary frame",
     {.isConfigureAcknowledged = true,
      .doesBufferExtentMatchConfigure = true,
      .wasLastContentUpdateDiscarded = false,
      .consecutiveAcquireStarvations = 0,
      .extraSwapchainImages = 0},
     SurfaceCommitAction::AttachBuffer},
}};

constexpr SurfaceCommitState kReadyState{.isConfigureAcknowledged = true,
                                         .doesBufferExtentMatchConfigure = true,
                                         .wasLastContentUpdateDiscarded = false,
                                         .consecutiveAcquireStarvations = 0,
                                         .extraSwapchainImages = 0};

struct FaultRow {
    SurfaceCommitFault fault;
    std::string_view name;
    SurfaceCommitAction action;
};

// A fault is only worth having if it reaches an action the ready state does not, so the assertion is the action
// each one provokes rather than the field it happens to set.
constexpr std::array<FaultRow, 5> kFaultTable{{
    {SurfaceCommitFault::None, "none", SurfaceCommitAction::AttachBuffer},
    {SurfaceCommitFault::CommitBeforeConfigure, "commit-before-configure", SurfaceCommitAction::WaitForConfigure},
    {SurfaceCommitFault::BufferExtentMismatch, "buffer-extent-mismatch",
     SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent},
    {SurfaceCommitFault::AcquireStarvation, "acquire-starvation", SurfaceCommitAction::RecreateSwapchainWithMoreImages},
    {SurfaceCommitFault::ContentUpdateDiscarded, "content-update-discarded",
     SurfaceCommitAction::RepresentDiscardedFrame},
}};

constexpr std::array<SurfaceCommitAction, 5> kEveryAction{
    SurfaceCommitAction::WaitForConfigure, SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent,
    SurfaceCommitAction::RecreateSwapchainWithMoreImages, SurfaceCommitAction::RepresentDiscardedFrame,
    SurfaceCommitAction::AttachBuffer};

TEST(SurfaceCommitGateTest, EveryTabledStateMapsToItsAction) {
    for (const GateRow& row : kGateTable) {
        EXPECT_EQ(surfaceCommitActionFor(row.state), row.action) << row.name;
    }
}

// The four bits and the starvation counter, enumerated rather than sampled: this is the whole input domain of the
// rule up to the counter's boundary, so nothing about it is reachable only in production.
TEST(SurfaceCommitGateTest, TheWholeInputDomainAgreesWithThePrecedenceOrder) {
    for (const bool isConfigureAcknowledged : {false, true}) {
        for (const bool doesBufferExtentMatchConfigure : {false, true}) {
            for (const bool wasLastContentUpdateDiscarded : {false, true}) {
                for (uint32_t starvations = 0; starvations <= kAcquireStarvationLimit + 1; ++starvations) {
                    for (uint32_t grown = 0; grown <= kMaxExtraSwapchainImages + 1; ++grown) {
                        const SurfaceCommitState state{.isConfigureAcknowledged = isConfigureAcknowledged,
                                                       .doesBufferExtentMatchConfigure = doesBufferExtentMatchConfigure,
                                                       .wasLastContentUpdateDiscarded = wasLastContentUpdateDiscarded,
                                                       .consecutiveAcquireStarvations = starvations,
                                                       .extraSwapchainImages = grown};
                        const bool isShortage =
                            starvations >= kAcquireStarvationLimit && grown < kMaxExtraSwapchainImages;
                        const SurfaceCommitAction expected =
                            !isConfigureAcknowledged          ? SurfaceCommitAction::WaitForConfigure
                            : !doesBufferExtentMatchConfigure ? SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent
                            : isShortage                      ? SurfaceCommitAction::RecreateSwapchainWithMoreImages
                            : wasLastContentUpdateDiscarded   ? SurfaceCommitAction::RepresentDiscardedFrame
                                                              : SurfaceCommitAction::AttachBuffer;

                        EXPECT_EQ(surfaceCommitActionFor(state), expected) << starvations << " " << grown;
                    }
                }
            }
        }
    }
}

TEST(SurfaceCommitGateTest, EveryFaultForcesTheStateItNames) {
    for (const FaultRow& row : kFaultTable) {
        EXPECT_EQ(surfaceCommitActionFor(applySurfaceCommitFault(kReadyState, row.fault)), row.action) << row.name;
    }
}

// A fault changes exactly the one thing it names, which is what makes the recovery that follows the real one
// rather than a reset to a fabricated state.
TEST(SurfaceCommitGateTest, AFaultLeavesEveryOtherFieldAlone) {
    const SurfaceCommitState discarded =
        applySurfaceCommitFault(kReadyState, SurfaceCommitFault::ContentUpdateDiscarded);

    EXPECT_TRUE(discarded.isConfigureAcknowledged);
    EXPECT_TRUE(discarded.doesBufferExtentMatchConfigure);
    EXPECT_TRUE(discarded.wasLastContentUpdateDiscarded);
    EXPECT_EQ(discarded.consecutiveAcquireStarvations, 0U);

    const SurfaceCommitState starved = applySurfaceCommitFault(kReadyState, SurfaceCommitFault::AcquireStarvation);

    EXPECT_TRUE(starved.isConfigureAcknowledged);
    EXPECT_TRUE(starved.doesBufferExtentMatchConfigure);
    EXPECT_FALSE(starved.wasLastContentUpdateDiscarded);
    EXPECT_EQ(starved.consecutiveAcquireStarvations, kAcquireStarvationLimit);
    EXPECT_EQ(starved.extraSwapchainImages, 0U);

    const SurfaceCommitState unconfigured =
        applySurfaceCommitFault(kReadyState, SurfaceCommitFault::CommitBeforeConfigure);

    EXPECT_FALSE(unconfigured.isConfigureAcknowledged);
    EXPECT_TRUE(unconfigured.doesBufferExtentMatchConfigure);

    const SurfaceCommitState mismatched =
        applySurfaceCommitFault(kReadyState, SurfaceCommitFault::BufferExtentMismatch);

    EXPECT_TRUE(mismatched.isConfigureAcknowledged);
    EXPECT_FALSE(mismatched.doesBufferExtentMatchConfigure);
}

TEST(SurfaceCommitGateTest, EveryActionAndFaultHasItsOwnSpelling) {
    for (const SurfaceCommitAction action : kEveryAction) {
        EXPECT_FALSE(describeSurfaceCommitAction(action).empty());
    }

    for (const FaultRow& row : kFaultTable) {
        EXPECT_EQ(describeSurfaceCommitFault(row.fault), row.name);
    }

    EXPECT_EQ(describeSurfaceCommitAction(SurfaceCommitAction::WaitForConfigure), "wait-for-configure");
    EXPECT_EQ(describeSurfaceCommitAction(SurfaceCommitAction::RepresentDiscardedFrame), "represent-discarded-frame");
    EXPECT_EQ(describeSurfaceCommitAction(SurfaceCommitAction::AttachBuffer), "attach-buffer");
    EXPECT_EQ(describeSurfaceCommitAction(SurfaceCommitAction::RecreateSwapchainAtConfiguredExtent),
              "recreate-swapchain-at-configured-extent");
    EXPECT_EQ(describeSurfaceCommitAction(SurfaceCommitAction::RecreateSwapchainWithMoreImages),
              "recreate-swapchain-with-more-images");
}

} // namespace
