#include "RecordingEventDispatcher.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/core/RawEvent.h>

// #806: the `insetsChange` react-native-safe-area-context's `SafeAreaProvider` waits for before it renders its
// children, sent by the mount of an `RNCSafeAreaProvider` as its iOS and Android views send it when laid out. The
// `safe-area-provider` e2e scenario and its golden are the same contract through the real library.

namespace {

constexpr Tag kProviderTag = 2;

struct InsetsChange {
    double top;
    double right;
    double bottom;
    double left;
    Rect frame;

    bool operator==(const InsetsChange&) const = default;
};

class SafeAreaProviderInsetsTest : public ::testing::Test {
protected:
    SafeAreaProviderInsetsTest() { mountingManager_.startSurface(kSurfaceTag, Size{.width = 800, .height = 600}); }

    ShadowView makeProvider(Rect frame, const char* componentName = "RNCSafeAreaProvider") const {
        ShadowView shadowView = makeView(kProviderTag, frame);

        shadowView.componentName = componentName;
        shadowView.eventEmitter = eventEmitter_;

        return shadowView;
    }

    void mount(ShadowViewMutationList&& mutations) {
        mountingManager_.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    }

    std::vector<InsetsChange> insetsChanges_;

private:
    std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher_{
        react_native_linux::makeListeningEventDispatcher([this](const facebook::react::RawEvent& event) {
            const auto read = [&event](const std::string& object, const std::string& field) {
                return event.eventPayload->extractValue({object, field}).value_or(-1);
            };

            EXPECT_EQ(event.type, "topInsetsChange");
            insetsChanges_.push_back(InsetsChange{.top = read("insets", "top"),
                                                  .right = read("insets", "right"),
                                                  .bottom = read("insets", "bottom"),
                                                  .left = read("insets", "left"),
                                                  .frame = makeRect(static_cast<float>(read("frame", "x")),
                                                                    static_cast<float>(read("frame", "y")),
                                                                    static_cast<float>(read("frame", "width")),
                                                                    static_cast<float>(read("frame", "height")))});

            return true;
        })};
    std::shared_ptr<facebook::react::EventEmitter> eventEmitter_{
        std::make_shared<facebook::react::EventEmitter>(nullptr, eventDispatcher_)};
    LinuxMountingManager mountingManager_;
};

TEST_F(SafeAreaProviderInsetsTest, AMountedProviderReportsZeroInsetsAndItsFrame) {
    const ShadowView provider = makeProvider(makeRect(0, 0, 800, 600));

    mount({ShadowViewMutation::CreateMutation(provider), ShadowViewMutation::InsertMutation(kSurfaceTag, provider, 0)});

    EXPECT_EQ(insetsChanges_, (std::vector<InsetsChange>{
                                  {.top = 0, .right = 0, .bottom = 0, .left = 0, .frame = makeRect(0, 0, 800, 600)}}));
}

TEST_F(SafeAreaProviderInsetsTest, AResizedProviderReportsItsNewFrameAndAnUpdateThatKeepsTheFrameReportsNothing) {
    const ShadowView provider = makeProvider(makeRect(0, 0, 800, 600));
    const ShadowView resized = makeProvider(makeRect(0, 0, 1024, 700));

    mount({ShadowViewMutation::CreateMutation(provider), ShadowViewMutation::InsertMutation(kSurfaceTag, provider, 0)});
    mount({ShadowViewMutation::UpdateMutation(provider, resized, kSurfaceTag)});
    mount({ShadowViewMutation::UpdateMutation(resized, resized, kSurfaceTag)});

    ASSERT_EQ(insetsChanges_.size(), 2U);
    EXPECT_EQ(insetsChanges_.back().frame, makeRect(0, 0, 1024, 700));
}

TEST_F(SafeAreaProviderInsetsTest, AnyOtherComponentReportsNothing) {
    const ShadowView view = makeProvider(makeRect(0, 0, 800, 600), "View");
    const ShadowView resized = makeProvider(makeRect(0, 0, 1024, 700), "View");

    mount({ShadowViewMutation::CreateMutation(view), ShadowViewMutation::InsertMutation(kSurfaceTag, view, 0)});
    mount({ShadowViewMutation::UpdateMutation(view, resized, kSurfaceTag)});

    EXPECT_TRUE(insetsChanges_.empty());
}

} // namespace
