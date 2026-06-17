#include "alloc_counter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<int64_t> g_alloc_count{0};
std::atomic<int64_t> g_alloc_bytes{0};
}  // namespace

void ResetAllocCounters() {
  g_alloc_count.store(0, std::memory_order_relaxed);
  g_alloc_bytes.store(0, std::memory_order_relaxed);
}

int64_t GetAllocCount() { return g_alloc_count.load(std::memory_order_relaxed); }

int64_t GetAllocBytes() { return g_alloc_bytes.load(std::memory_order_relaxed); }

void* operator new(std::size_t size) {
  g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  g_alloc_bytes.fetch_add(static_cast<int64_t>(size), std::memory_order_relaxed);
  void* ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  return ptr;
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
