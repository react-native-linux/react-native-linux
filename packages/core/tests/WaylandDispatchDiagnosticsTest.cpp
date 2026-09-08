#include "WaylandDispatchDiagnostics.h"

#include <cerrno>
#include <cstdint>
#include <gtest/gtest.h>

namespace {

using react_native_linux::classifyWaylandDispatchResult;
using react_native_linux::formatWaylandDisplayError;
using react_native_linux::formatWaylandProtocolError;
using react_native_linux::WaylandDispatchOutcome;
using react_native_linux::WaylandProtocolErrorDetail;

TEST(WaylandDispatchDiagnosticsTest, ASuccessfulResultIsAClosedCleanlyOrNoOpOutcome) {
    EXPECT_EQ(classifyWaylandDispatchResult(0, 0, 0), WaylandDispatchOutcome::Continue);
    EXPECT_EQ(classifyWaylandDispatchResult(1, 0, 0), WaylandDispatchOutcome::Continue);
}

TEST(WaylandDispatchDiagnosticsTest, DisplayErrnoEagainIsRetry) {
    EXPECT_EQ(classifyWaylandDispatchResult(-1, EAGAIN, 0), WaylandDispatchOutcome::Retry);
}

TEST(WaylandDispatchDiagnosticsTest, CallErrnoEagainIsRetryEvenWithNoDisplayError) {
    EXPECT_EQ(classifyWaylandDispatchResult(-1, 0, EAGAIN), WaylandDispatchOutcome::Retry);
}

TEST(WaylandDispatchDiagnosticsTest, EprotoIsProtocolError) {
    EXPECT_EQ(classifyWaylandDispatchResult(-1, EPROTO, 0), WaylandDispatchOutcome::ProtocolError);
}

TEST(WaylandDispatchDiagnosticsTest, EpipeIsDisplayError) {
    EXPECT_EQ(classifyWaylandDispatchResult(-1, EPIPE, EPIPE), WaylandDispatchOutcome::DisplayError);
}

TEST(WaylandDispatchDiagnosticsTest, AnyOtherNegativeErrnoIsDisplayError) {
    EXPECT_EQ(classifyWaylandDispatchResult(-1, EINVAL, EINVAL), WaylandDispatchOutcome::DisplayError);
}

TEST(WaylandDispatchDiagnosticsTest, ProtocolErrorFormatsInterfaceObjectAndCode) {
    const WaylandProtocolErrorDetail detail{
        .interfaceName = "xdg_surface",
        .objectId = 7,
        .errorCode = 3,
    };

    EXPECT_EQ(formatWaylandProtocolError(detail, "Protocol error"),
              "wayland protocol error: xdg_surface#7 code 3 (Protocol error)");
}

TEST(WaylandDispatchDiagnosticsTest, DisplayErrorFormatsTheErrnoText) {
    EXPECT_EQ(formatWaylandDisplayError("Broken pipe"), "wayland display error: Broken pipe");
}

} // namespace
