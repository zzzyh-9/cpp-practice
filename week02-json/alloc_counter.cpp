#include "alloc_counter.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

std::atomic<bool>& enabled() noexcept {
    static std::atomic<bool> value{true};
    return value;
}

std::atomic<std::size_t>& allocationCount() noexcept {
    static std::atomic<std::size_t> value{0};
    return value;
}

std::atomic<std::size_t>& allocatedBytes() noexcept {
    static std::atomic<std::size_t> value{0};
    return value;
}

std::atomic<std::size_t>& deallocationCount() noexcept {
    static std::atomic<std::size_t> value{0};
    return value;
}

std::size_t normalizedSize(std::size_t size) noexcept {
    return size == 0 ? 1 : size;
}

} // namespace

void AllocCounter::reset() noexcept {
    enabled().store(false, std::memory_order_relaxed);
    ::allocationCount().store(0, std::memory_order_relaxed);
    ::allocatedBytes().store(0, std::memory_order_relaxed);
    ::deallocationCount().store(0, std::memory_order_relaxed);
    enabled().store(true, std::memory_order_relaxed);
}

void AllocCounter::report(const char* label) noexcept {
    enabled().store(false, std::memory_order_relaxed);

    const std::size_t allocations =
        ::allocationCount().load(std::memory_order_relaxed);
    const std::size_t bytes =
        ::allocatedBytes().load(std::memory_order_relaxed);
    const std::size_t deallocations =
        ::deallocationCount().load(std::memory_order_relaxed);

    std::printf("  [%s]\n", label ? label : "<unlabeled>");
    std::printf("    allocations : %zu\n", allocations);
    std::printf("    allocated   : %zu bytes\n", bytes);
    std::printf("    deallocations: %zu\n", deallocations);
    std::printf("    outstanding : %zu blocks\n\n",
                allocations >= deallocations ? allocations - deallocations : 0);
    std::fflush(stdout);

    enabled().store(true, std::memory_order_relaxed);
}

void AllocCounter::recordAllocation(std::size_t size) noexcept {
    if (!enabled().load(std::memory_order_relaxed)) {
        return;
    }
    ::allocationCount().fetch_add(1, std::memory_order_relaxed);
    ::allocatedBytes().fetch_add(size, std::memory_order_relaxed);
}

void AllocCounter::recordDeallocation() noexcept {
    if (!enabled().load(std::memory_order_relaxed)) {
        return;
    }
    ::deallocationCount().fetch_add(1, std::memory_order_relaxed);
}

std::size_t AllocCounter::allocationCount() noexcept {
    return ::allocationCount().load(std::memory_order_relaxed);
}

std::size_t AllocCounter::allocatedBytes() noexcept {
    return ::allocatedBytes().load(std::memory_order_relaxed);
}

std::size_t AllocCounter::deallocationCount() noexcept {
    return ::deallocationCount().load(std::memory_order_relaxed);
}

void* operator new(std::size_t size) {
    const std::size_t bytes = normalizedSize(size);
    void* memory = std::malloc(bytes);
    if (!memory) {
        throw std::bad_alloc();
    }
    AllocCounter::recordAllocation(bytes);
    return memory;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    if (!memory) {
        return;
    }
    std::free(memory);
    AllocCounter::recordDeallocation();
}

void operator delete[](void* memory) noexcept {
    ::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}
