#include "ToplevelState.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using react_native_linux::decodeToplevelStates;
using react_native_linux::isEffectivelyTiled;
using react_native_linux::ToplevelState;

constexpr uint32_t kXdgToplevelStateMaximized = 1;
constexpr uint32_t kXdgToplevelStateFullscreen = 2;
constexpr uint32_t kXdgToplevelStateResizing = 3;
constexpr uint32_t kXdgToplevelStateActivated = 4;
constexpr uint32_t kXdgToplevelStateTiledLeft = 5;
constexpr uint32_t kXdgToplevelStateTiledRight = 6;
constexpr uint32_t kXdgToplevelStateTiledTop = 7;
constexpr uint32_t kXdgToplevelStateTiledBottom = 8;
constexpr uint32_t kXdgToplevelStateSuspended = 9;

TEST(ToplevelStateTest, DefaultIsEveryFlagFalse) {
    EXPECT_EQ(ToplevelState{}, (ToplevelState{false, false, false, false, false, false, false, false}));
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
    const uint32_t states[] = {kXdgToplevelStateSuspended};

    EXPECT_EQ(decodeToplevelStates(states, 1), ToplevelState{});
}

struct TiledEdgeCase {
    std::string name;
    uint32_t wireState;
    bool ToplevelState::*field;
};

TEST(ToplevelStateTest, EachTiledEdgeIsDecodedIndependently) {
    const std::vector<TiledEdgeCase> cases{
        {"left", kXdgToplevelStateTiledLeft, &ToplevelState::tiledLeft},
        {"right", kXdgToplevelStateTiledRight, &ToplevelState::tiledRight},
        {"top", kXdgToplevelStateTiledTop, &ToplevelState::tiledTop},
        {"bottom", kXdgToplevelStateTiledBottom, &ToplevelState::tiledBottom},
    };

    for (const TiledEdgeCase& tiledCase : cases) {
        const uint32_t states[] = {tiledCase.wireState};
        const ToplevelState decoded = decodeToplevelStates(states, 1);

        EXPECT_TRUE(decoded.*tiledCase.field) << tiledCase.name;
        EXPECT_FALSE(decoded.maximized) << tiledCase.name;
    }
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
    const uint32_t states[] = {kXdgToplevelStateSuspended, kXdgToplevelStateFullscreen, kXdgToplevelStateActivated};

    const ToplevelState decoded = decodeToplevelStates(states, 3);

    EXPECT_TRUE(decoded.fullscreen);
    EXPECT_TRUE(decoded.activated);
    EXPECT_FALSE(decoded.maximized);
    EXPECT_FALSE(decoded.resizing);
}

TEST(ToplevelStateTest, EqualityDistinguishesEveryField) {
    EXPECT_NE(ToplevelState{}, (ToplevelState{true, false, false, false, false, false, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, true, false, false, false, false, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, true, false, false, false, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, false, true, false, false, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, false, false, true, false, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, false, false, false, true, false, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, false, false, false, false, true, false}));
    EXPECT_NE(ToplevelState{}, (ToplevelState{false, false, false, false, false, false, false, true}));
}

struct TiledPredicateCase {
    std::string name;
    ToplevelState state;
    bool expected;
};

TEST(ToplevelStateTest, TheTiledPredicateIsAnyEdgeTiledAndNotMaximized) {
    const std::vector<TiledPredicateCase> cases{
        {"floating, nothing tiled", ToplevelState{}, false},
        {"one edge tiled", ToplevelState{.tiledLeft = true}, true},
        // GNOME on Ubuntu reports every edge tiled even when the window occupies only one quarter of the
        // screen, so the predicate has to answer true from that report exactly as it would from an accurate one:
        // trusting an individual edge is the mistake, not the report itself.
        {"gnome reports all four edges for a half-tile", ToplevelState{.tiledLeft = true, .tiledTop = true}, true},
        {"all four edges tiled",
         ToplevelState{.tiledLeft = true, .tiledRight = true, .tiledTop = true, .tiledBottom = true}, true},
        // Maximized also reports every edge tiled, and it has its own unambiguous chrome, so it wins over the
        // tiled reading rather than combining with it.
        {"maximized with every edge also reported tiled",
         ToplevelState{.maximized = true, .tiledLeft = true, .tiledRight = true, .tiledTop = true, .tiledBottom = true},
         false},
        {"maximized alone", ToplevelState{.maximized = true}, false},
        {"fullscreen alone is not tiled", ToplevelState{.fullscreen = true}, false},
    };

    for (const TiledPredicateCase& predicateCase : cases) {
        EXPECT_EQ(isEffectivelyTiled(predicateCase.state), predicateCase.expected) << predicateCase.name;
    }
}

} // namespace
