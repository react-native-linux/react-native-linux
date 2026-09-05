#pragma once

#include <optional>
#include <string>

namespace react_native_linux {

/**
 * The socket half of the automation channel (#214): a listening `AF_UNIX` `SOCK_STREAM` socket under
 * `XDG_RUNTIME_DIR` that serves one driver at a time, one line-delimited JSON request in and one out.
 *
 * It knows nothing about what the commands mean — `AutomationProtocol` is the format and `rnl_window` is the
 * dispatch — because the deferral `TakeScreenshot` needs belongs to the frame loop: the picture only exists
 * after the next present, so the loop answers the request a frame later than it read it.
 *
 * Everything here is non-blocking. `takeRequestLine` accepts a pending connection if there is one and returns a
 * whole line if one has arrived, and nothing otherwise, so a window with no driver attached spends nothing on
 * the channel and never stalls its frame loop on a socket.
 *
 * One client at a time, because there is one driver: a second connection waits in the listen backlog until the
 * first closes. A driver that disconnects mid-request loses the response and the next connection starts clean.
 *
 * Threading contract: constructed, polled and destroyed on the platform frame thread, the same thread that owns
 * the Wayland connection and the run loop. Nothing here is called from anywhere else.
 */
class AutomationServer final {
public:
    /** Binds and listens, or throws `std::system_error` naming the syscall that failed. */
    explicit AutomationServer(std::string socketPath);
    AutomationServer(const AutomationServer&) = delete;
    AutomationServer(AutomationServer&&) = delete;
    AutomationServer& operator=(const AutomationServer&) = delete;
    AutomationServer& operator=(AutomationServer&&) = delete;
    ~AutomationServer() noexcept;

    const std::string& socketPath() const noexcept;

    /** The next complete request line, without its newline, or nothing if none has arrived. */
    std::optional<std::string> takeRequestLine();

    /** Writes one already-newline-terminated response line, dropping it if the driver has gone away. */
    void sendResponse(const std::string& line);

private:
    void acceptPendingClient();
    void readPendingBytes();
    void dropClient();

    std::string socketPath_;
    int listenDescriptor_;
    int clientDescriptor_{-1};
    std::string received_;
};

/**
 * `$XDG_RUNTIME_DIR/rnl-automation-<pid>.sock`, falling back to `/tmp` when the runtime directory is unset. The
 * process id is in the name so two windows on one machine never collide, and the driver learns the whole path
 * from the trace line the window prints when it opens the socket.
 */
std::string defaultAutomationSocketPath();

} // namespace react_native_linux
