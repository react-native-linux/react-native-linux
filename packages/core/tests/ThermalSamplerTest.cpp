#include "ThermalSampler.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using react_native_linux::FrameClock;
using react_native_linux::ThermalSampler;

constexpr std::optional<double> kNoTrip = std::nullopt;

/** A fake `/sys/class/thermal`: each zone has a `temp` and numbered `trip_point_<n>_{type,temp}` files. */
class FakeThermalRoot final {
public:
    FakeThermalRoot()
        : root_(std::filesystem::temp_directory_path() /
                ("rnl-thermal-" + std::to_string(getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name())) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
    }

    FakeThermalRoot(const FakeThermalRoot&) = delete;
    FakeThermalRoot& operator=(const FakeThermalRoot&) = delete;
    FakeThermalRoot(FakeThermalRoot&&) = delete;
    FakeThermalRoot& operator=(FakeThermalRoot&&) = delete;

    ~FakeThermalRoot() { std::filesystem::remove_all(root_); }

    std::filesystem::path zone(const std::string& name, const std::string& temperature,
                               const std::vector<std::pair<std::string, std::string>>& trips) const {
        const std::filesystem::path zone = root_ / name;

        std::filesystem::create_directories(zone);
        write(zone / "temp", temperature);

        for (std::size_t trip = 0; trip < trips.size(); ++trip) {
            const std::string prefix = "trip_point_" + std::to_string(trip);

            write(zone / (prefix + "_type"), trips[trip].first);
            write(zone / (prefix + "_temp"), trips[trip].second);
        }

        return zone;
    }

    void setTemperature(const std::string& name, const std::string& temperature) const {
        write(root_ / name / "temp", temperature);
    }

    const std::filesystem::path& path() const { return root_; }

private:
    static void write(const std::filesystem::path& file, const std::string& text) {
        std::ofstream(file) << text << '\n';
    }

    std::filesystem::path root_;
};

bool waitForState(const ThermalSampler& sampler, FrameClock::ThermalState expected) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (sampler.state() != expected) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return true;
}

TEST(ThermalSamplerTest, AZoneIsSeriousFromItsLowestThrottlingTripAndCriticalAtItsCriticalTrip) {
    EXPECT_EQ(ThermalSampler::zoneState(84'999, 85'000, kNoTrip), FrameClock::ThermalState::Nominal);
    EXPECT_EQ(ThermalSampler::zoneState(85'000, 85'000, kNoTrip), FrameClock::ThermalState::Serious);
    EXPECT_EQ(ThermalSampler::zoneState(150'000, 85'000, kNoTrip), FrameClock::ThermalState::Serious);

    EXPECT_EQ(ThermalSampler::zoneState(90'000, 85'000, 110'000), FrameClock::ThermalState::Serious);
    EXPECT_EQ(ThermalSampler::zoneState(100'000, 105'000, 110'000), FrameClock::ThermalState::Nominal);
    EXPECT_EQ(ThermalSampler::zoneState(110'000, 85'000, 110'000), FrameClock::ThermalState::Critical);

    EXPECT_EQ(ThermalSampler::zoneState(89'999, kNoTrip, 100'000), FrameClock::ThermalState::Nominal);
    EXPECT_EQ(ThermalSampler::zoneState(90'000, kNoTrip, 100'000), FrameClock::ThermalState::Serious);
    EXPECT_EQ(ThermalSampler::zoneState(100'000, kNoTrip, 100'000), FrameClock::ThermalState::Critical);

    EXPECT_EQ(ThermalSampler::zoneState(200'000, kNoTrip, kNoTrip), FrameClock::ThermalState::Nominal);
}

TEST(ThermalSamplerTest, AMissingOrEmptyThermalRootReadsNominal) {
    const FakeThermalRoot thermal;

    EXPECT_EQ(ThermalSampler::read(thermal.path() / "missing"), FrameClock::ThermalState::Nominal);
    EXPECT_EQ(ThermalSampler::read(thermal.path()), FrameClock::ThermalState::Nominal);
}

TEST(ThermalSamplerTest, TheStateIsTheWorstZoneEachSeriousFromItsLowestThrottlingTrip) {
    const FakeThermalRoot thermal;

    thermal.zone("thermal_zone0", "80000", {{"passive", "95000"}, {"critical", "110000"}});
    EXPECT_EQ(ThermalSampler::read(thermal.path()), FrameClock::ThermalState::Nominal);

    thermal.zone("thermal_zone1", "90000", {{"hot", "85000"}, {"hot", "95000"}});
    EXPECT_EQ(ThermalSampler::read(thermal.path()), FrameClock::ThermalState::Serious);

    thermal.zone("thermal_zone2", "110000", {{"active", "60000"}, {"critical", "110000"}});
    EXPECT_EQ(ThermalSampler::read(thermal.path()), FrameClock::ThermalState::Critical);
}

/**
 * The most severe zone wins, not the hottest one nor the one the directory lists first: a cooler zone at its own
 * trip outranks a hotter zone only over its passive trip. A directory lists by creation order or by a name hash,
 * so the severe zone is created first under the first name in one root and last under the second in the other.
 */
TEST(ThermalSamplerTest, TheMostSevereZoneWinsNotTheHottestOrTheFirstListed) {
    const FakeThermalRoot thermal;

    for (const bool isSevereCreatedFirst : {true, false}) {
        const std::string root = isSevereCreatedFirst ? "severe-first" : "severe-last";
        const std::string severe = root + (isSevereCreatedFirst ? "/thermal_zone0" : "/thermal_zone1");
        const std::string hotter = root + (isSevereCreatedFirst ? "/thermal_zone1" : "/thermal_zone0");

        if (isSevereCreatedFirst) {
            thermal.zone(severe, "100000", {{"critical", "100000"}});
        }

        thermal.zone(hotter, "120000", {{"passive", "115000"}, {"critical", "150000"}});

        if (!isSevereCreatedFirst) {
            thermal.zone(severe, "100000", {{"critical", "100000"}});
        }

        EXPECT_EQ(ThermalSampler::read(thermal.path() / root), FrameClock::ThermalState::Critical) << root;

        thermal.zone(hotter, "120000", {{"passive", "125000"}, {"critical", "150000"}});
        thermal.setTemperature(severe, "90000");
        EXPECT_EQ(ThermalSampler::read(thermal.path() / root), FrameClock::ThermalState::Serious) << root;
    }
}

TEST(ThermalSamplerTest, OnlyAThermalZoneWithAReadableTemperatureAndAPositiveTripCounts) {
    const FakeThermalRoot thermal;

    thermal.zone("cooling_device0", "200000", {{"critical", "100000"}});
    thermal.zone("thermal_zone0", "200000", {{"active", "50000"}});
    thermal.zone("thermal_zone1", "200000", {{"passive", "0"}, {"critical", "0"}});
    thermal.zone("thermal_zone2", "not-a-number", {{"critical", "100000"}});
    std::filesystem::remove(thermal.zone("thermal_zone3", "", {{"critical", "100000"}}) / "temp");
    const std::filesystem::path unreadableTemperature =
        thermal.zone("thermal_zone4", "", {{"critical", "100000"}}) / "temp";
    std::filesystem::remove(unreadableTemperature);
    std::filesystem::create_directory(unreadableTemperature);
    std::filesystem::remove(thermal.zone("thermal_zone5", "200000", {{"critical", "100000"}}) / "trip_point_0_temp");

    EXPECT_EQ(ThermalSampler::read(thermal.path()), FrameClock::ThermalState::Nominal);
}

TEST(ThermalSamplerTest, TheSampledStateFollowsTheZoneFiles) {
    const FakeThermalRoot thermal;

    thermal.zone("thermal_zone0", "50000", {{"passive", "85000"}, {"critical", "100000"}});

    const ThermalSampler sampler(thermal.path(), std::chrono::milliseconds(1));

    EXPECT_TRUE(waitForState(sampler, FrameClock::ThermalState::Nominal));

    thermal.setTemperature("thermal_zone0", "90000");
    EXPECT_TRUE(waitForState(sampler, FrameClock::ThermalState::Serious));

    thermal.setTemperature("thermal_zone0", "100000");
    EXPECT_TRUE(waitForState(sampler, FrameClock::ThermalState::Critical));

    thermal.setTemperature("thermal_zone0", "40000");
    EXPECT_TRUE(waitForState(sampler, FrameClock::ThermalState::Nominal));
}

TEST(ThermalSamplerTest, StoppingWakesTheSamplerOutOfItsWaitAtOnce) {
    const FakeThermalRoot thermal;

    thermal.zone("thermal_zone0", "100000", {{"critical", "100000"}});

    std::optional<ThermalSampler> sampler(std::in_place, thermal.path(), std::chrono::hours(1));

    ASSERT_TRUE(waitForState(sampler.value(), FrameClock::ThermalState::Critical));

    const std::chrono::steady_clock::time_point stopping = std::chrono::steady_clock::now();

    sampler.reset();

    EXPECT_LT(std::chrono::steady_clock::now() - stopping, std::chrono::seconds(1));
}

} // namespace
