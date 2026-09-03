#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

// Issue #124: the frame-thread animation path has to be allocation-free, because allocation is the mechanism
// behind every native-driver regression the issue cites. Counting it needs the global allocation operators, and
// this header is where they are replaced.
//
// Two rules make that safe in a binary that also runs every other suite:
//
//  - **Counting only.** The replacements are `std::malloc`/`std::free` and nothing else. No pooling, no
//    poisoning, no bookkeeping headers, so no test can observe them except through the counters below.
//  - **Only while a scope is open, only on the scope's own thread.** The counters are `thread_local` and are
//    read through a flag that is false everywhere outside an `AllocationScope`. GoogleTest allocates freely while
//    it registers, runs and reports a test; none of that is counted, so nothing here depends on gtest's internals.
//
// The operator definitions are deliberately **not** `inline`: exactly one translation unit may include this
// header, and a second one is a duplicate-symbol link error rather than a silent second replacement. That
// translation unit is `AnimationFrameCostTest.cpp`.
//
// Over-aligned allocations are not counted. `operator new(size_t, align_val_t)` is left to the library, which
// pairs it with the library's own aligned delete, so the counts stay balanced and the uncounted path is one
// nothing on the animation frame path takes. See *Animation frame cost (#124)* in docs/cpp-toolchain.md.

namespace react_native_linux {

inline thread_local bool isAllocationCountingActive = false;

inline thread_local std::size_t allocationCount = 0;

inline thread_local std::size_t deallocationCount = 0;

inline void* countedAllocationOrNull(std::size_t size) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): a replacement operator new is the one place malloc is the primitive
    void* pointer = std::malloc(size == 0 ? 1 : size);

    if (pointer != nullptr && isAllocationCountingActive) {
        ++allocationCount;
    }

    return pointer;
}

inline void* countedAllocation(std::size_t size) {
    void* pointer = countedAllocationOrNull(size);

    if (pointer == nullptr) {
        throw std::bad_alloc();
    }

    return pointer;
}

inline void countedRelease(void* pointer) noexcept {
    if (pointer != nullptr && isAllocationCountingActive) {
        ++deallocationCount;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): the pair of the malloc above
    std::free(pointer);
}

/**
 * Counts every global allocation and deallocation made on the constructing thread until it goes out of scope.
 *
 * Not nestable: construction resets the counters, so one scope brackets one measurement. Read the counters before
 * the scope ends — reading them allocates nothing — and assert on the values afterwards, where GoogleTest's own
 * allocations no longer land on the count.
 */
class AllocationScope final {
public:
    AllocationScope() {
        allocationCount = 0;
        deallocationCount = 0;
        isAllocationCountingActive = true;
    }

    AllocationScope(const AllocationScope&) = delete;

    AllocationScope(AllocationScope&&) = delete;

    AllocationScope& operator=(const AllocationScope&) = delete;

    AllocationScope& operator=(AllocationScope&&) = delete;

    ~AllocationScope() { isAllocationCountingActive = false; }

    std::size_t allocations() const noexcept { return allocationCount; }

    std::size_t deallocations() const noexcept { return deallocationCount; }
};

/**
 * How many global allocations one frame's worth of work cost. The callable is invoked directly rather than through
 * a `std::function`, so the measurement does not pay for its own indirection.
 */
template <typename FrameCallable>
std::size_t allocationsDuringFrame(const FrameCallable& frame) {
    const AllocationScope scope;

    frame();

    return scope.allocations();
}

} // namespace react_native_linux

void* operator new(std::size_t size) {
    return react_native_linux::countedAllocation(size);
}

void* operator new[](std::size_t size) {
    return react_native_linux::countedAllocation(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return react_native_linux::countedAllocationOrNull(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return react_native_linux::countedAllocationOrNull(size);
}

void operator delete(void* pointer) noexcept {
    react_native_linux::countedRelease(pointer);
}

void operator delete[](void* pointer) noexcept {
    react_native_linux::countedRelease(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    react_native_linux::countedRelease(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    react_native_linux::countedRelease(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    react_native_linux::countedRelease(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    react_native_linux::countedRelease(pointer);
}
