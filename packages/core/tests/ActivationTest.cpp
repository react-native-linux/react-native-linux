#include "Activation.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>

namespace {

using react_native_linux::ActivationModel;

TEST(ActivationModelTest, StartsWithNoCurrentUrl) {
    const ActivationModel model;

    EXPECT_EQ(model.currentUrl(), std::nullopt);
}

TEST(ActivationModelTest, OnActivationUrlReceivedUpdatesTheCurrentUrl) {
    ActivationModel model;

    model.onActivationUrlReceived("myapp://open");

    EXPECT_EQ(model.currentUrl(), "myapp://open");
}

TEST(ActivationModelTest, ALaterActivationReplacesTheEarlierUrl) {
    ActivationModel model;

    model.onActivationUrlReceived("myapp://first");
    model.onActivationUrlReceived("myapp://second");

    EXPECT_EQ(model.currentUrl(), "myapp://second");
}

TEST(ActivationModelTest, InvokesTheChangeListenerWithTheReceivedUrl) {
    ActivationModel model;
    int invocationCount = 0;
    std::string lastUrl;

    model.setChangeListener([&invocationCount, &lastUrl](const std::string& url) {
        ++invocationCount;
        lastUrl = url;
    });

    model.onActivationUrlReceived("myapp://open");

    EXPECT_EQ(invocationCount, 1);
    EXPECT_EQ(lastUrl, "myapp://open");
}

TEST(ActivationModelTest, WithNoChangeListenerAnActivationStillUpdatesTheState) {
    ActivationModel model;

    model.onActivationUrlReceived("myapp://open");

    EXPECT_EQ(model.currentUrl(), "myapp://open");
}

TEST(ActivationModelTest, ReplacingTheChangeListenerStopsTheOldOneFromBeingCalled) {
    ActivationModel model;
    int firstListenerCount = 0;
    int secondListenerCount = 0;

    model.setChangeListener([&firstListenerCount](const std::string&) { ++firstListenerCount; });
    model.setChangeListener([&secondListenerCount](const std::string&) { ++secondListenerCount; });

    model.onActivationUrlReceived("myapp://open");

    EXPECT_EQ(firstListenerCount, 0);
    EXPECT_EQ(secondListenerCount, 1);
}

} // namespace
