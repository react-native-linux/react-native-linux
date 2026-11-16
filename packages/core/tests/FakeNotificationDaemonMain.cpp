#include "FakeNotificationDaemon.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <systemd/sd-bus.h>

/**
 * `rnl_fake_notifications`: `FakeNotificationDaemon` on the session bus, owning `org.freedesktop.Notifications` and
 * clicking every notification it shows, so the `expo-notifications` e2e sees the call the window makes and the
 * response that comes back. It prints `[fake-notifications] ready` once it owns the name, and runs until killed.
 */
namespace {

struct BusClose {
    void operator()(sd_bus* bus) const noexcept { sd_bus_flush_close_unref(bus); }
};

} // namespace

int main() {
    sd_bus* opened = nullptr;

    if (sd_bus_open_user(&opened) < 0) {
        std::cerr << "[fake-notifications] no session bus\n";

        return 1;
    }

    const std::unique_ptr<sd_bus, BusClose> bus(opened);
    react_native_linux::FakeNotificationDaemon::Behavior clicking;

    clicking.invokedAction = "default";
    clicking.log = [](const std::string& line) { std::cout << "[fake-notifications] " << line << std::endl; };

    const react_native_linux::FakeNotificationDaemon daemon(bus.get(), clicking);

    if (sd_bus_request_name(bus.get(), "org.freedesktop.Notifications", 0) < 0) {
        std::cerr << "[fake-notifications] cannot own org.freedesktop.Notifications\n";

        return 1;
    }

    std::cout << "[fake-notifications] ready" << std::endl;

    while (sd_bus_process(bus.get(), nullptr) >= 0) {
        sd_bus_wait(bus.get(), UINT64_MAX);
    }

    return 1;
}
