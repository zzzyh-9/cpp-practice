#pragma once

#include <atomic>
#include <cstddef>

class AllocCounter {
public:
    AllocCounter() = delete;

    static void reset() noexcept;
    static void report(const char* label) noexcept;

    static void recordAllocation(std::size_t size) noexcept;
    static void recordDeallocation() noexcept;

    static std::size_t allocationCount() noexcept;
    static std::size_t allocatedBytes() noexcept;
    static std::size_t deallocationCount() noexcept;
};
