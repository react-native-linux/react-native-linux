#include "ThermalSampler.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

namespace react_native_linux {

namespace {

constexpr double kSeriousCriticalFraction = 0.9;

std::optional<double> readNumber(const std::filesystem::path& file) {
    std::ifstream stream(file);
    double value = 0.0;

    if (stream >> value) {
        return value;
    }

    return std::nullopt;
}

struct ZoneTrips {
    std::optional<double> throttle;
    std::optional<double> critical;
};

// The kernel numbers a zone's trips from zero without gaps, so the first missing `trip_point_<n>_type` ends them.
ZoneTrips readTrips(const std::filesystem::path& zone) {
    ZoneTrips trips;

    for (int trip = 0;; ++trip) {
        const std::string prefix = "trip_point_" + std::to_string(trip);
        std::ifstream typeFile(zone / (prefix + "_type"));
        std::string type;

        if (!(typeFile >> type)) {
            return trips;
        }

        const bool isThrottle = type == "passive" || type == "hot";

        if (!isThrottle && type != "critical") {
            continue;
        }

        const std::optional<double> temperature = readNumber(zone / (prefix + "_temp"));

        if (temperature.has_value() && temperature.value() > 0.0) {
            std::optional<double>& slot = isThrottle ? trips.throttle : trips.critical;

            slot = std::min(slot.value_or(temperature.value()), temperature.value());
        }
    }
}

} // namespace

ThermalSampler::ThermalSampler(std::filesystem::path thermalRoot, std::chrono::milliseconds interval)
    : thread_([this, root = std::move(thermalRoot), interval](const std::stop_token& stop) {
          std::mutex waitMutex;
          std::unique_lock lock(waitMutex);

          while (!stop.stop_requested()) {
              state_.store(read(root));
              wake_.wait_for(lock, stop, interval, [] { return false; });
          }
      }) {}

FrameClock::ThermalState ThermalSampler::state() const noexcept { return state_.load(); }

FrameClock::ThermalState ThermalSampler::read(const std::filesystem::path& thermalRoot) {
    FrameClock::ThermalState worst = FrameClock::ThermalState::Nominal;
    std::error_code error;

    for (std::filesystem::directory_iterator zone(thermalRoot, error), end; !error && zone != end;
         zone.increment(error)) {
        if (!zone->path().filename().string().starts_with("thermal_zone")) {
            continue;
        }

        const ZoneTrips trips = readTrips(zone->path());

        if (!trips.throttle.has_value() && !trips.critical.has_value()) {
            continue;
        }

        const std::optional<double> temperature = readNumber(zone->path() / "temp");

        if (temperature.has_value()) {
            worst = std::max(worst, zoneState(temperature.value(), trips.throttle, trips.critical));
        }
    }

    return worst;
}

FrameClock::ThermalState ThermalSampler::zoneState(double temperature, std::optional<double> throttleTrip,
                                                   std::optional<double> criticalTrip) {
    if (criticalTrip.has_value() && temperature >= criticalTrip.value()) {
        return FrameClock::ThermalState::Critical;
    }

    const bool isSerious =
        throttleTrip.has_value()
            ? temperature >= throttleTrip.value()
            : criticalTrip.has_value() && temperature >= criticalTrip.value() * kSeriousCriticalFraction;

    return isSerious ? FrameClock::ThermalState::Serious : FrameClock::ThermalState::Nominal;
}

} // namespace react_native_linux
