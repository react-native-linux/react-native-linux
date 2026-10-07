#include "FabricHost.h"
#include "ReactHost.h"

#include <chrono>
#include <cstddef>
#include <cxxreact/JSBigString.h>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 400, .height = 200};
constexpr double kFrameMilliseconds = 16.0;

/** Both hosts, built in `WindowSession`'s order on the thread that constructs this one. */
struct Hosts {
    std::unique_ptr<ReactHost> reactHost{std::make_unique<ReactHost>()};
    std::unique_ptr<FabricHost> fabricHost{std::make_unique<FabricHost>(reactHost->reactInstance(), kSurfaceSize)};
};

struct ForeignThreadCall {
    std::string member;
    std::function<void(Hosts&)> call;
};

void PrintTo(const ForeignThreadCall& foreignThreadCall, std::ostream* stream) { *stream << foreignThreadCall.member; }

const std::vector<ForeignThreadCall> kForeignThreadCalls{
    {"ReactHost::reactInstance", [](Hosts& hosts) { hosts.reactHost->reactInstance(); }},
    {"ReactHost::dimensions", [](Hosts& hosts) { hosts.reactHost->dimensions(); }},
    {"ReactHost::appearance", [](Hosts& hosts) { hosts.reactHost->appearance(); }},
    {"ReactHost::activation", [](Hosts& hosts) { hosts.reactHost->activation(); }},
    {"ReactHost::keyValueStore", [](Hosts& hosts) { hosts.reactHost->keyValueStore(); }},
    {"ReactHost::i18n", [](Hosts& hosts) { hosts.reactHost->i18n(); }},
    {"ReactHost::publishPendingDimensions", [](Hosts& hosts) { hosts.reactHost->publishPendingDimensions(); }},
    {"ReactHost::loadScript",
     [](Hosts& hosts) {
         hosts.reactHost->loadScript(std::make_unique<facebook::react::JSBigStdString>("0;"), "affinity.js");
     }},
    {"ReactHost::loadBundle", [](Hosts& hosts) { hosts.reactHost->loadBundle("affinity.js"); }},
    {"ReactHost::startHotModuleReplacement",
     [](Hosts& hosts) { hosts.reactHost->startHotModuleReplacement("affinity.js"); }},
    {"ReactHost::drainJavaScriptThread", [](Hosts& hosts) { hosts.reactHost->drainJavaScriptThread(); }},
    {"ReactHost::blockJavaScriptThread",
     [](Hosts& hosts) { hosts.reactHost->blockJavaScriptThread(std::chrono::milliseconds{0}); }},
    {"ReactHost::hasMarkedTestPassed", [](Hosts& hosts) { hosts.reactHost->hasMarkedTestPassed(); }},
    {"ReactHost::runUntilQuiescent",
     [](Hosts& hosts) { hosts.reactHost->runUntilQuiescent(std::chrono::milliseconds{0}); }},
    {"ReactHost::hasReportedFatalError", [](Hosts& hosts) { hosts.reactHost->hasReportedFatalError(); }},
    {"ReactHost::dispatchAnimationFrames",
     [](Hosts& hosts) { hosts.reactHost->dispatchAnimationFrames(std::chrono::steady_clock::now()); }},
    {"ReactHost::hasPendingTimers", [](Hosts& hosts) { hosts.reactHost->hasPendingTimers(); }},
    {"ReactHost::~ReactHost", [](Hosts& hosts) { hosts.reactHost.reset(); }},
    {"FabricHost::setSurfaceSize", [](Hosts& hosts) { hosts.fabricHost->setSurfaceSize(kSurfaceSize, 1.0F); }},
    {"FabricHost::setLayoutDirection", [](Hosts& hosts) { hosts.fabricHost->setLayoutDirection({}); }},
    {"FabricHost::stopSurface", [](Hosts& hosts) { hosts.fabricHost->stopSurface(); }},
    {"FabricHost::startAdditionalSurface",
     [](Hosts& hosts) { hosts.fabricHost->startAdditionalSurface(11, kSurfaceSize, 1.0F); }},
    {"FabricHost::stopAdditionalSurface", [](Hosts& hosts) { hosts.fabricHost->stopAdditionalSurface(11); }},
    {"FabricHost::setTextInputFocusSink", [](Hosts& hosts) { hosts.fabricHost->setTextInputFocusSink(nullptr); }},
    {"FabricHost::dispatchInput", [](Hosts& hosts) { hosts.fabricHost->dispatchInput({}); }},
    {"FabricHost::injectFocusCommand", [](Hosts& hosts) { hosts.fabricHost->injectFocusCommand(1); }},
    {"FabricHost::advanceScroll", [](Hosts& hosts) { hosts.fabricHost->advanceScroll(kFrameMilliseconds); }},
    {"FabricHost::advanceCaretBlink", [](Hosts& hosts) { hosts.fabricHost->advanceCaretBlink(kFrameMilliseconds); }},
    {"FabricHost::advanceImageAnimations",
     [](Hosts& hosts) { hosts.fabricHost->advanceImageAnimations(kFrameMilliseconds); }},
    {"FabricHost::advanceControlAnimations",
     [](Hosts& hosts) { hosts.fabricHost->advanceControlAnimations(kFrameMilliseconds); }},
    {"FabricHost::induceEventBeat", [](Hosts& hosts) { hosts.fabricHost->induceEventBeat(); }},
    {"FabricHost::tickAnimations",
     [](Hosts& hosts) { hosts.fabricHost->tickAnimations(std::chrono::steady_clock::now()); }},
    {"FabricHost::hasPendingWork", [](Hosts& hosts) { hosts.fabricHost->hasPendingWork(); }},
    {"FabricHost::takeFrame", [](Hosts& hosts) { hosts.fabricHost->takeFrame(); }},
    {"FabricHost::snapshotScene", [](Hosts& hosts) { hosts.fabricHost->snapshotScene(); }},
    {"FabricHost::findNodeAtPoint", [](Hosts& hosts) { hosts.fabricHost->findNodeAtPoint({}); }},
    {"FabricHost::dumpScene", [](Hosts& hosts) { hosts.fabricHost->dumpScene(); }},
    {"FabricHost::visualTreeNodes", [](Hosts& hosts) { hosts.fabricHost->visualTreeNodes(); }},
    {"FabricHost::takeAccessibilityChanges", [](Hosts& hosts) { hosts.fabricHost->takeAccessibilityChanges(); }},
    {"FabricHost::~FabricHost", [](Hosts& hosts) { hosts.fabricHost.reset(); }},
};

void callFromForeignThread(const std::function<void(Hosts&)>& call) {
    Hosts hosts;

    std::thread(call, std::ref(hosts)).join();
}

/** What glibc's `assert` prints for the member's failed `react_native_assert`, and nothing a crash would print. */
std::string assertionFailureOf(const std::string& member) {
    return member + R"(\(.*: Assertion .std::this_thread::get_id\(\) == owningThread_. failed)";
}

std::string testNameOf(const testing::TestParamInfo<ForeignThreadCall>& info) {
    const std::size_t separator = info.param.member.find("::");
    const std::string name = info.param.member.substr(separator + 2);

    return info.param.member.substr(0, separator) + "_" + (name.starts_with('~') ? "destructor" : name);
}

class ThreadAffinityTest : public testing::TestWithParam<ForeignThreadCall> {};

/**
 * Issue #77, the thread-affinity half of the contracts in ReactHost.h and FabricHost.h: each public member, the
 * destructors included, called from a thread other than the one that built the hosts, fails its debug assertion
 * instead of running. The threadsafe style re-executes this binary for the child rather than forking the parent,
 * which earlier tests in the same run may have left with threads of their own holding locks.
 */
TEST_P(ThreadAffinityTest, ACallFromAForeignThreadFailsTheAssertion) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH(callFromForeignThread(GetParam().call), assertionFailureOf(GetParam().member));
}

INSTANTIATE_TEST_SUITE_P(PublicMembers, ThreadAffinityTest, testing::ValuesIn(kForeignThreadCalls), testNameOf);

} // namespace
} // namespace react_native_linux
