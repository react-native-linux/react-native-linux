#pragma once

#include "FrameClock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <thread>

namespace react_native_linux {

/**
 * The machine's thermal state for `FrameClock::fallbackInterval` (#335, #774), sampled off the render thread
 * because a sysfs read can stall for longer than a frame: some drivers switch sensor channels on every read.
 *
 * Threading contract: the constructor starts one sampling thread, which alone reads the thermal directory, once
 * immediately and then once per `interval`; until that first read lands the state is nominal. `state()` is a
 * lock-free atomic load, safe from any thread, and is what the frame loop reads. The destructor requests a stop,
 * which wakes the sampling thread's wait at once, and joins it, so the owner's scope bounds the thread's life.
 */
class ThermalSampler final {
public:
    ThermalSampler(std::filesystem::path thermalRoot, std::chrono::milliseconds interval);

    [[nodiscard]] FrameClock::ThermalState state() const noexcept;

    /**
     * The worst state over the `thermal_zone*` entries of a sysfs thermal class directory (`/sys/class/thermal` in
     * production; `RNL_THERMAL_ROOT` replaces it, and the headless rig points it at a directory without zones). A
     * zone's trip types are read first, so a zone without a positive `passive`, `hot` or `critical` trip never pays
     * for a temperature read. A missing root or a file that fails to read is nominal, not an error.
     */
    [[nodiscard]] static FrameClock::ThermalState read(const std::filesystem::path& thermalRoot);

    /**
     * One zone's state in the OS thermal-pressure sense: critical at its `critical` trip, where Linux shuts the
     * machine down; serious from its lowest `passive` or `hot` trip, where the kernel starts throttling, or, for a
     * zone whose only trip is critical, from 0.9 of that trip.
     */
    [[nodiscard]] static FrameClock::ThermalState zoneState(double temperature, std::optional<double> throttleTrip,
                                                            std::optional<double> criticalTrip);

private:
    std::atomic<FrameClock::ThermalState> state_{FrameClock::ThermalState::Nominal};
    std::condition_variable_any wake_;
    std::jthread thread_;
};

} // namespace react_native_linux
