#pragma once

#include "SdBus.h"

#include <systemd/sd-bus.h>
#include <unistd.h>

namespace react_native_linux {

/** Connects a client over a copy of `descriptor`, one end of a socket pair, as `sd_bus_open_user` connects one. */
inline SdBusOpener socketPairOpener(int descriptor) {
    return [descriptor](sd_bus** bus) {
        const int client = ::dup(descriptor);

        sd_bus_new(bus);
        sd_bus_set_fd(*bus, client, client);

        return sd_bus_start(*bus);
    };
}

} // namespace react_native_linux
