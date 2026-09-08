#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * A seeded, single-threaded executor for tests, modelled on `crates/scheduler/src/test_scheduler.rs` in
 * `zed-industries/zed` (#346, research section E20).
 *
 * TSan finds data races and a hand-written test asserts one interleaving. Neither finds a *logical* race — a
 * promise resolved between a commit and a mount, a frame callback arriving between a scroll event and its
 * settle. This scheduler makes the interleaving an input: the ready queue takes its next task from a seeded
 * random source, so one seed is one reproducible interleaving and a hundred seeds are a hundred of them.
 *
 * Four properties, each a decision copied from the reference:
 *
 * - **Randomised ready order.** `run` picks the next ready task by seeded choice, not by queue position.
 * - **A virtual clock that jumps.** `scheduleAfter` never sleeps: when nothing is ready the clock moves to the
 *   earliest deadline, so real time is structurally unreachable and a test costs no wall clock.
 * - **Seeded tick counts.** `scheduleAfterSeededTicks` resolves after a seeded number of executed tasks, so a
 *   timeout-versus-completion race is decided by the seed rather than by how fast the machine is.
 * - **Parking is forbidden.** A `beginWait` that no task ever completes is not a hang here; `run` drains, sees
 *   the outstanding wait and returns a report naming it.
 *
 * Threading contract: single-threaded by construction. Every member runs on the thread that constructed the
 * scheduler, including the task bodies, which is what makes an interleaving a property of the seed instead of a
 * property of the operating system's scheduler. Code under test may still own threads of its own; the
 * scheduler then orders the calls made into it, as `SeededInterleavingTest` does with `HostTimerRegistry`.
 */
class SeededTestScheduler final {
public:
    explicit SeededTestScheduler(std::uint64_t seed) noexcept;

    std::uint64_t seed() const noexcept;

    /** The number of tasks executed so far. The unit `scheduleAfterSeededTicks` counts in. */
    std::uint64_t tickCount() const noexcept;

    /** The virtual clock. It only ever moves in `run`, and only to a deadline some timer asked for. */
    std::chrono::milliseconds now() const noexcept;

    void schedule(std::function<void()> task);

    void scheduleAfter(std::chrono::milliseconds delay, std::function<void()> task);

    /** Queues `task` after a seeded count in `[1, maximumTicks]` executed tasks, and returns that count. */
    std::uint64_t scheduleAfterSeededTicks(std::uint64_t maximumTicks, std::function<void()> task);

    /** A seeded choice in `[0, bound)`, or zero when `bound` is zero. The interleaving point production and
     *  test code declare for themselves. */
    std::uint64_t nextChoice(std::uint64_t bound) noexcept;

    /** Declares work the run is not allowed to finish without; the identifier `completeWait` takes. */
    std::uint64_t beginWait(std::string label);

    void completeWait(std::uint64_t wait);

    /**
     * Runs until no task is ready and no timer is pending. `std::nullopt` when the run drained; otherwise the
     * forbidden-parking report, naming the seed and every wait nothing completed.
     */
    std::optional<std::string> run();

private:
    struct DeadlineTimer {
        std::chrono::milliseconds deadline;
        std::function<void()> task;
    };

    struct TickTimer {
        std::uint64_t dueTick;
        std::function<void()> task;
    };

    struct PendingWait {
        std::uint64_t identifier;
        std::string label;
    };

    void releaseDueTickTimers();
    bool releaseEarliestDeadlineTimers();
    bool releaseEarliestTickTimers();
    std::optional<std::string> parkingReport() const;

    std::uint64_t seed_;
    std::uint64_t randomState_;
    std::uint64_t tickCount_{0};
    std::uint64_t nextWaitIdentifier_{1};
    std::chrono::milliseconds now_{0};
    std::vector<std::function<void()>> readyTasks_;
    std::vector<DeadlineTimer> deadlineTimers_;
    std::vector<TickTimer> tickTimers_;
    std::vector<PendingWait> pendingWaits_;
};

/** The seed of a failing iteration and the one-line instruction that replays it. */
struct SeededIterationFailure {
    std::uint64_t seed;
    std::string replayInstruction;
};

/** `SEED` from the environment, or `defaultSeed` when it is unset or not a number. */
std::uint64_t seedFromEnvironment(std::uint64_t defaultSeed);

/** `ITERATIONS` from the environment, or `defaultIterations` when it is unset or not a number. */
std::uint64_t iterationsFromEnvironment(std::uint64_t defaultIterations);

/** The line a failing seeded run prints: the seed, and the command that replays exactly that interleaving. */
std::string replayInstruction(std::uint64_t seed, std::string_view testFilter);

/**
 * Runs `body` once per seed, starting at `SEED` and running `ITERATIONS` times, and stops at the first seed for
 * which `body` returns false — returning that seed and its replay instruction, so a hundred-seed failure names
 * the one interleaving to debug instead of the hundred that were tried.
 */
std::optional<SeededIterationFailure> runSeededIterations(std::string_view testFilter,
                                                          const std::function<bool(SeededTestScheduler&)>& body);

} // namespace react_native_linux
