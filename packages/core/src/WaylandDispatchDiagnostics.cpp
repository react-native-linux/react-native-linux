#include "WaylandDispatchDiagnostics.h"

#include <cerrno>

namespace react_native_linux {

WaylandDispatchOutcome classifyWaylandDispatchResult(int dispatchResult, int displayErrno, int callErrno) noexcept {
    if (dispatchResult >= 0) {
        return WaylandDispatchOutcome::Continue;
    }

    if (displayErrno == EAGAIN || callErrno == EAGAIN) {
        return WaylandDispatchOutcome::Retry;
    }

    if (displayErrno == EPROTO) {
        return WaylandDispatchOutcome::ProtocolError;
    }

    return WaylandDispatchOutcome::DisplayError;
}

std::string formatWaylandProtocolError(const WaylandProtocolErrorDetail& detail, const std::string& errnoText) {
    return "wayland protocol error: " + detail.interfaceName + "#" + std::to_string(detail.objectId) + " code " +
           std::to_string(detail.errorCode) + " (" + errnoText + ")";
}

std::string formatWaylandDisplayError(const std::string& errnoText) { return "wayland display error: " + errnoText; }

} // namespace react_native_linux
