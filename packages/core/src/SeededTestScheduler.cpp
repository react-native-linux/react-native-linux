#include "SeededTestScheduler.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <utility>

namespace react_native_linux {

namespace {

constexpr std::uint64_t kGoldenGammaIncrement = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kFirstMixMultiplier = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t kSecondMixMultiplier = 0x94D049BB133111EBULL;
constexpr unsigned int kFirstMixShift = 30;
constexpr unsigned int kSecondMixShift = 27;
constexpr unsigned int kFinalMixShift = 31;

constexpr std::uint64_t kDefaultSeed = 0;
constexpr std::uint64_t kDefaultIterations = 100;

std::uint64_t nextRandomValue(std::uint64_t& state) noexcept {
    state += kGoldenGammaIncrement;

    std::uint64_t value = state;
    value = (value ^ (value >> kFirstMixShift)) * kFirstMixMultiplier;
    value = (value ^ (value >> kSecondMixShift)) * kSecondMixMultiplier;

    return value ^ (value >> kFinalMixShift);
}

std::uint64_t numberFromEnvironment(const char* variableName, std::uint64_t fallback) {
    const char* rawValue = std::getenv(variableName);

    if (rawValue == nullptr) {
        return fallback;
    }

    std::uint64_t parsedValue = 0;
    const char* const end = rawValue + std::strlen(rawValue);
    const std::from_chars_result outcome = std::from_chars(rawValue, end, parsedValue);

    if (outcome.ec != std::errc{} || outcome.ptr != end) {
        return fallback;
    }

    return parsedValue;
}

} // namespace

SeededTestScheduler::SeededTestScheduler(std::uint64_t seed) noexcept : seed_(seed), randomState_(seed) {}

std::uint64_t SeededTestScheduler::seed() const noexcept { return seed_; }

std::uint64_t SeededTestScheduler::tickCount() const noexcept { return tickCount_; }

std::chrono::milliseconds SeededTestScheduler::now() const noexcept { return now_; }

void SeededTestScheduler::schedule(std::function<void()> task) { readyTasks_.push_back(std::move(task)); }

void SeededTestScheduler::scheduleAfter(std::chrono::milliseconds delay, std::function<void()> task) {
    deadlineTimers_.push_back(DeadlineTimer{now_ + delay, std::move(task)});
}

std::uint64_t SeededTestScheduler::scheduleAfterSeededTicks(std::uint64_t maximumTicks, std::function<void()> task) {
    const std::uint64_t chosenTicks = nextChoice(maximumTicks) + 1;

    tickTimers_.push_back(TickTimer{tickCount_ + chosenTicks, std::move(task)});

    return chosenTicks;
}

std::uint64_t SeededTestScheduler::nextChoice(std::uint64_t bound) noexcept {
    if (bound == 0) {
        return 0;
    }

    return nextRandomValue(randomState_) % bound;
}

std::uint64_t SeededTestScheduler::beginWait(std::string label) {
    const std::uint64_t identifier = nextWaitIdentifier_;
    ++nextWaitIdentifier_;

    pendingWaits_.push_back(PendingWait{identifier, std::move(label)});

    return identifier;
}

void SeededTestScheduler::completeWait(std::uint64_t wait) {
    for (auto pending = pendingWaits_.begin(); pending != pendingWaits_.end(); ++pending) {
        if (pending->identifier == wait) {
            pendingWaits_.erase(pending);
            return;
        }
    }
}

std::optional<std::string> SeededTestScheduler::run() {
    bool hasWork = true;

    while (hasWork) {
        releaseDueTickTimers();

        if (readyTasks_.empty()) {
            hasWork = releaseEarliestDeadlineTimers() || releaseEarliestTickTimers();
            continue;
        }

        const auto index = static_cast<std::ptrdiff_t>(nextChoice(readyTasks_.size()));
        std::function<void()> task = std::move(*(readyTasks_.begin() + index));

        readyTasks_.erase(readyTasks_.begin() + index);
        task();
        ++tickCount_;
    }

    return parkingReport();
}

void SeededTestScheduler::releaseDueTickTimers() {
    for (auto timer = tickTimers_.begin(); timer != tickTimers_.end();) {
        if (timer->dueTick <= tickCount_) {
            readyTasks_.push_back(std::move(timer->task));
            timer = tickTimers_.erase(timer);
        } else {
            ++timer;
        }
    }
}

bool SeededTestScheduler::releaseEarliestDeadlineTimers() {
    if (deadlineTimers_.empty()) {
        return false;
    }

    now_ = std::min_element(
               deadlineTimers_.begin(), deadlineTimers_.end(),
               [](const DeadlineTimer& left, const DeadlineTimer& right) { return left.deadline < right.deadline; })
               ->deadline;

    for (auto timer = deadlineTimers_.begin(); timer != deadlineTimers_.end();) {
        if (timer->deadline <= now_) {
            readyTasks_.push_back(std::move(timer->task));
            timer = deadlineTimers_.erase(timer);
        } else {
            ++timer;
        }
    }

    return true;
}

bool SeededTestScheduler::releaseEarliestTickTimers() {
    if (tickTimers_.empty()) {
        return false;
    }

    tickCount_ =
        std::min_element(tickTimers_.begin(), tickTimers_.end(), [](const TickTimer& left, const TickTimer& right) {
            return left.dueTick < right.dueTick;
        })->dueTick;
    releaseDueTickTimers();

    return true;
}

std::optional<std::string> SeededTestScheduler::parkingReport() const {
    if (pendingWaits_.empty()) {
        return std::nullopt;
    }

    std::string report = "forbidden parking: seed " + std::to_string(seed_) + " drained after " +
                         std::to_string(tickCount_) + " ticks with unfinished waits:";

    for (const PendingWait& pending : pendingWaits_) {
        report += " " + pending.label;
    }

    return report;
}

std::uint64_t seedFromEnvironment(std::uint64_t defaultSeed) { return numberFromEnvironment("SEED", defaultSeed); }

std::uint64_t iterationsFromEnvironment(std::uint64_t defaultIterations) {
    return numberFromEnvironment("ITERATIONS", defaultIterations);
}

std::string replayInstruction(std::uint64_t seed, std::string_view testFilter) {
    return "failing seed: " + std::to_string(seed) + " — replay with: SEED=" + std::to_string(seed) +
           " ITERATIONS=1 ctest --preset test -R " + std::string(testFilter);
}

std::optional<SeededIterationFailure> runSeededIterations(std::string_view testFilter,
                                                          const std::function<bool(SeededTestScheduler&)>& body) {
    const std::uint64_t baseSeed = seedFromEnvironment(kDefaultSeed);
    const std::uint64_t iterations = iterationsFromEnvironment(kDefaultIterations);

    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        const std::uint64_t seed = baseSeed + iteration;
        SeededTestScheduler scheduler(seed);

        if (!body(scheduler)) {
            return SeededIterationFailure{seed, replayInstruction(seed, testFilter)};
        }
    }

    return std::nullopt;
}

} // namespace react_native_linux
