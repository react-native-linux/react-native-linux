#pragma once

#include "SingleInstanceActivation.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <systemd/sd-bus.h>

namespace react_native_linux {

/**
 * The sd-bus half of single-instance activation (#363): the well-known name of `SingleInstanceActivation.h` is
 * requested with `sd_bus_request_name(..., 0)` — no queueing, no replacing — so the request itself is the
 * arbitration a second launch and the first race on. Whichever process's request the bus answers first is the
 * primary instance; the other decodes the same failure as "somebody already owns this", forwards its own `argv`
 * and `cwd` to whoever does over that name, and is done — `isPrimaryInstance()` is `false` and the caller in
 * `WindowMain` exits without ever opening a Wayland connection.
 *
 * This is a second sd-bus connection, not a share of `AppearancePortal`'s. The instance decision has to happen
 * before a `WindowSession` — and therefore an `AppearancePortal` — exists at all, or a second launch would pay
 * for the Wayland and Vulkan bring-up single-instance activation exists to avoid; sharing would mean handing
 * `AppearancePortal` a bus it did not open, which is a real refactor of an already-landed, coverage-gated class
 * (#385) this issue does not need to make to be correct. What is reused is the *threading contract*: this
 * connection is opened once, pumped only from the platform frame thread's existing per-frame beat — the same
 * place `AppearancePortal::processPendingSignals` is pumped from — and never given a dispatch thread of its own.
 *
 * The primary instance keeps the connection open for as long as the process runs and exports one object,
 * `kSingleInstanceObjectPath`, with one method, `Activate`, signature `aay ay` — `argv` as an array of byte
 * arrays and `cwd` as one, never `as`/`s`: a D-Bus `STRING` must be valid UTF-8, and neither is guaranteed to be
 * (PR #387's review). `takePendingActivationUrl`, pumped once per frame, is `sd_bus_process` followed by reading
 * whatever the last `Activate` call decoded — the same "drain, then read one recorded value" shape
 * `AppearancePortal::processPendingSignals` uses for `SettingChanged`, and for the same reason: the vtable
 * callback and the frame-thread caller are the same thread, so the recorded field needs no lock.
 *
 * When there is no session bus at all, `sd_bus_open_user` fails and this falls back to "always the primary
 * instance": single-instance activation is simply not available, the same documented shape
 * `AppearancePortal`'s no-portal fallback takes, rather than a launch that refuses to start.
 */
class SingleInstanceCoordinator final {
public:
    SingleInstanceCoordinator(std::string_view applicationId, const ActivationRequest& ownActivation);
    SingleInstanceCoordinator(const SingleInstanceCoordinator&) = delete;
    SingleInstanceCoordinator(SingleInstanceCoordinator&&) = delete;
    SingleInstanceCoordinator& operator=(const SingleInstanceCoordinator&) = delete;
    SingleInstanceCoordinator& operator=(SingleInstanceCoordinator&&) = delete;
    ~SingleInstanceCoordinator() noexcept = default;

    /**
     * `false` means a running instance already owns the name and `ownActivation` has already been forwarded to
     * it: the caller's whole job is to exit without creating a window.
     */
    bool isPrimaryInstance() const noexcept;

    /**
     * Drains the bus and returns the URL a later instance's forwarded `argv` decoded to, if the last call since
     * this was last called carried one. Called once per frame, only meaningful when `isPrimaryInstance()`.
     */
    std::optional<std::string> takePendingActivationUrl();

private:
    struct BusDeleter {
        void operator()(sd_bus* bus) const noexcept;
    };

    struct SlotDeleter {
        void operator()(sd_bus_slot* slot) const noexcept;
    };

    static int onActivate(sd_bus_message* message, void* userData, sd_bus_error* error);

    void forwardToPrimaryInstance(const std::string& busName, const ActivationRequest& ownActivation);
    void exportActivationObject();

    std::unique_ptr<sd_bus, BusDeleter> bus_;
    std::unique_ptr<sd_bus_slot, SlotDeleter> activationSlot_;
    bool isPrimaryInstance_{true};
    std::optional<std::string> pendingActivationUrl_;
};

} // namespace react_native_linux
