#include "ToplevelState.h"

#include <cstdint>
#include <gtest/gtest.h>

namespace {

using react_native_linux::decodeToplevelStates;
using react_native_linux::ToplevelState;

constexpr uint32_t kXdgToplevelStateMaximized = 1;
constexpr uint32_t kXdgToplevelStateFullscreen = 2;
constexpr uint32_t kXdgToplevelStateResizing = 3;
constexpr uint32_t kXdgToplevelStateActivated = 4;
constexpr uint32_t kXdgToplevelStateTiledLeft = 5;

TEST(ToplevelStateTest, DefaultIsEveryFlagFalse) {
    EXPECT_EQ(ToplevelState{}, (ToplevelState{false, false, false, false}));
}

TEST(ToplevelStateTest, AnEmptyArrayDecodesToTheDefault) {
    EXPECT_EQ(decodeToplevelStates(nullptr, 0), ToplevelState{});
}

TEST(ToplevelStateTest, ActivatedIsDecoded) {
    const uint32_t states[] = {kXdgToplevelStateActivated};

    const ToplevelState decoded = decodeToplevelStates(states, 1);

    EXPECT_TRUE(decoded.activated);
    EXPECT_FALSE(decoded.maximized);
    EXPECT_FALSE(decoded.fullscreen);
    EXPECT_FALSE(decoded.resizing);
}

TEST(ToplevelStateTest, MaximizedIsDecoded) {
    const uint32_t states[] = {kXdgToplevelStateMaximized};

    EXPECT_TRUE(decodeToplevelStates(states, 1).maximized);
}

TEST(ToplevelStateTest, FullscreenIsDecoded) {
    const uint32_t states[] = {kXdgToplevelStateFullscreen};

    EXPECT_TRUE(decodeToplevelStates(states, 1).fullscreen);
}

TEST(ToplevelStateTest, ResizingIsDecoded) {
    const uint32_t states[] = {kXdgToplevelStateResizing};

    EXPECT_TRUE(decodeToplevelStates(states, 1).resizing);
}

TEST(ToplevelStateTest, AnUnrecognisedStateIsIgnored) {
    const uint32_t states[] = {kXdgToplevelStateTiledLeft};

    EXPECT_EQ(decodeToplevelStates(states, 1), ToplevelState{});
}

TEST(ToplevelStateTest, MaximizedAndActivatedTogetherAreBothSet) {
    const uint32_t states[] = {kXdgToplevelStateMaximized, kXdgToplevelStateActivated};

    const ToplevelState decoded = decodeToplevelStates(states, 2);

    EXPECT_TRUE(decoded.maximized);
    EXPECT_TRUE(decoded.activated);
    EXPECT_FALSE(decoded.fullscreen);
    EXPECT_FALSE(decoded.resizing);
}

TEST(ToplevelStateTest, AnUnrecognisedStateAmongRecognisedOnesDoesNotSuppressThem) {
    const uint32_t states[] = {kXdgToplevelStateTiledLeft, kXdgToplevelStateFullscreen, kXdgToplevelStateActivated};

    const ToplevelState decoded = decodeToplevelStates(states, 3);

    EXPECT_TRUE(decoded.fullscreen);
    EXPECT_TRUE(decoded.activated);
    EXPECT_FALSE(decoded.maximized);
    EXPECT_FALSE(decoded.resizing);
}

TEST(ToplevelStateTest, EqualityDistinguishesEveryField) {
    EXPECT_NE(ToplevelState{}, (ToplevelState{true, false, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, true, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, true, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, false, true}));
}

} // namespace
