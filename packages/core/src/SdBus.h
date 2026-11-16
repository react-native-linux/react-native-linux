#pragma once

#include <functional>
#include <systemd/sd-bus.h>

namespace react_native_linux {

/** Fills its argument with a connected bus and answers its result, as `sd_bus_open_user` does. */
using SdBusOpener = std::function<int(sd_bus**)>;

/** The error a failed sd-bus call fills, freed with its scope. */
struct SdBusError {
    SdBusError() = default;
    SdBusError(const SdBusError&) = delete;
    SdBusError(SdBusError&&) = delete;
    SdBusError& operator=(const SdBusError&) = delete;
    SdBusError& operator=(SdBusError&&) = delete;

    ~SdBusError() { sd_bus_error_free(&value); }

    sd_bus_error value{};
};

} // namespace react_native_linux
