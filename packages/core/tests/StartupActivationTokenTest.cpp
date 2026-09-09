#include "StartupActivationToken.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>

namespace {

using react_native_linux::StartupActivationToken;

TEST(StartupActivationTokenTest, ATokenInTheEnvironmentIsTakenExactlyOnce) {
    StartupActivationToken token = StartupActivationToken::fromEnvironment(std::string("abc123"));

    const std::optional<std::string> first = token.take();

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first.value(), "abc123");

    EXPECT_EQ(token.take(), std::nullopt);
    EXPECT_EQ(token.take(), std::nullopt);
    EXPECT_TRUE(token.consumed());
}

TEST(StartupActivationTokenTest, TheAbsentTokenPathTakesNothingAndIsConsumedFromTheStart) {
    StartupActivationToken token = StartupActivationToken::fromEnvironment(std::nullopt);

    EXPECT_EQ(token.take(), std::nullopt);
    EXPECT_TRUE(token.consumed());
}

TEST(StartupActivationTokenTest, AnEmptyVariableReadsAsUnset) {
    StartupActivationToken token = StartupActivationToken::fromEnvironment(std::string(""));

    EXPECT_EQ(token.take(), std::nullopt);
}

TEST(StartupActivationTokenTest, AnUnconsumedTokenReportsItselfAsNotConsumed) {
    StartupActivationToken token = StartupActivationToken::fromEnvironment(std::string("abc123"));

    EXPECT_FALSE(token.consumed());

    (void)token.take();

    EXPECT_TRUE(token.consumed());
}

TEST(StartupActivationTokenTest, ATakenTokenIsMovedOutRatherThanCopied) {
    StartupActivationToken token = StartupActivationToken::fromEnvironment(std::string("abc123"));

    std::optional<std::string> taken = token.take();

    // The point of take() is that the value is gone from the holder, so a caller that strips the environment
    // variable on the same line cannot leave a second copy of the credential anywhere reachable.
    EXPECT_EQ(taken.value(), "abc123");
    EXPECT_FALSE(token.consumed() == false && token.take().has_value());
}

} // namespace
