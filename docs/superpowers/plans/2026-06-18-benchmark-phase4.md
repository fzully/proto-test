# Benchmark Phase 4 (Memory Allocations / Arena) Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax for tracking. (Executed directly by Claude in this session, autonomously, per user authorization — no subagent dispatch, no per-step confirmation.)

**Goal:** Compare heap-allocation count/bytes per parse between a default heap-allocated `ChatMessage` and a long-lived, reused `google::protobuf::Arena`-allocated one, for the same `text` message fixture.

**Architecture:** Add a `proto_bench`-only allocation-counting translation unit (global `operator new`/`delete` override), wire it into `proto_bench`'s CMake source list, and add 2 new benchmark functions that report both timing (automatic) and allocation count/bytes (custom counters).

**Tech Stack:** C++20, existing `proto_bench`/Google Benchmark harness, `google::protobuf::Arena` (already available via the linked `protobuf::libprotobuf`, no new dependency).

## Global Constraints

- C++20 fixed. The only CMake change allowed in this phase is adding `src/alloc_counter.cpp` to `proto_bench`'s source list — `proto_test`/`chat_proto` targets are untouched.
- Do NOT delete/empty `build/` or `build/_deps/`. A CMakeLists.txt source-list change requires re-running `cmake -S . -B build`, but this does NOT touch any `FetchContent_Declare` block, so no dependency re-fetch should occur — confirm this empirically in Task 1 Step 3 (configure should be fast, not triggering a fresh clone).
- Scope: parse-side allocation comparison only, `text` message type only (reusing `BuildTextMessage()`). No Phase 5-8 work.
- No placeholder text in the committed analysis doc.

---

### Task 1: Add the allocation-counter translation unit and wire it into CMake

**Files:**
- Create: `src/alloc_counter.h`
- Create: `src/alloc_counter.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `void ResetAllocCounters();`, `int64_t GetAllocCount();`, `int64_t GetAllocBytes();`, declared in `src/alloc_counter.h`, global namespace.

- [ ] **Step 1: Create `src/alloc_counter.h`**

```cpp
#ifndef PROTO_TEST_ALLOC_COUNTER_H_
#define PROTO_TEST_ALLOC_COUNTER_H_

#include <cstdint>

void ResetAllocCounters();
int64_t GetAllocCount();
int64_t GetAllocBytes();

#endif  // PROTO_TEST_ALLOC_COUNTER_H_
```

- [ ] **Step 2: Create `src/alloc_counter.cpp`**

```cpp
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
```

This overrides the global allocation operators for the entire `proto_bench` process. `proto_test` is a separate executable and is unaffected. The implementation only calls `std::malloc`/`std::free` directly (never another allocating standard-library facility) to avoid recursive allocation.

- [ ] **Step 3: Wire the new file into `proto_bench`'s CMake target**

In `CMakeLists.txt`, change:

```cmake
add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp)
```

to:

```cmake
add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp)
```

- [ ] **Step 4: Reconfigure and build**

Run: `cmake -S . -B build`
Expected: configure succeeds quickly (no `FetchContent` re-fetch — `protobuf`/`benchmark`'s `GIT_REPOSITORY`/`GIT_TAG` are unchanged, so CMake reuses the already-populated `build/_deps/protobuf-src` and `build/_deps/benchmark-src`; confirm by checking the configure log does not mention cloning).

Run: `cmake --build build -j20`
Expected: build succeeds, `proto_bench` now also compiles `src/alloc_counter.cpp`, no errors/warnings.

- [ ] **Step 5: Verify `proto_test` and `proto_bench` both still work**

Run: `./build/proto_test`
Expected: exit code 0, unchanged output (117 / 162 bytes).

Run: `./build/proto_bench` (no new benchmarks registered yet, this just confirms the binary still links and runs)
Expected: exit code 0, prints the existing benchmark table unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/alloc_counter.h src/alloc_counter.cpp CMakeLists.txt
git commit -m "Add allocation-counting instrumentation for proto_bench"
```

---

### Task 2: Add the heap-vs-Arena parse benchmarks

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: `ResetAllocCounters()`, `GetAllocCount()`, `GetAllocBytes()` from `src/alloc_counter.h` (Task 1); `BuildTextMessage()` from `src/message_fixtures.h` (existing).

- [ ] **Step 1: Add includes**

At the top of `src/bench.cpp`, add two includes after the existing ones:

```cpp
#include <google/protobuf/arena.h>

#include "alloc_counter.h"
```

- [ ] **Step 2: Insert the 2 new benchmark functions**

Insert right after the existing `BM_ParseMergedItems`/`BENCHMARK(BM_ParseMergedItems)...` block, before the closing `}  // namespace`:

```cpp
void BM_ParseTextHeapAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextHeapAllocs);

void BM_ParseTextArenaAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));

  google::protobuf::Arena arena;
  {
    ChatMessage* warm = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(warm->ParseFromString(bytes));
  }
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage* parsed = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(parsed->ParseFromString(bytes));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextArenaAllocs);
```

`BM_ParseTextArenaAllocs` reuses one `arena` across the entire timed loop (no per-iteration `Reset()`), modeling a long-lived arena that processes many messages — this is what makes Arena's amortized-allocation benefit visible. The one warm-up parse before `ResetAllocCounters()` keeps the arena's first-block setup cost out of the measured counters.

- [ ] **Step 3: Build and run**

Run: `cmake --build build -j20`
Expected: build succeeds, no new warnings.

Run: `./build/proto_bench --benchmark_filter="Alloc"`
Expected: 2 rows, `BM_ParseTextHeapAllocs` and `BM_ParseTextArenaAllocs`, each showing `allocs_per_iter` and `bytes_per_iter` counters alongside Time/CPU. Sanity check: `BM_ParseTextArenaAllocs`'s `allocs_per_iter` must be substantially smaller than `BM_ParseTextHeapAllocs`'s. If not, something is wrong with the arena reuse or the warm-up — stop and report the actual numbers rather than guessing a fix.

- [ ] **Step 4: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, unchanged output.

- [ ] **Step 5: Commit**

```bash
git add src/bench.cpp
git commit -m "Add heap-vs-Arena parse allocation benchmarks"
```

---

### Task 3: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase4-2026-06-18.json`
- Create: `docs/benchmarks/phase4-memory-arena-analysis.md`

- [ ] **Step 1: Run with JSON output**

```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase4-2026-06-18.json
```

- [ ] **Step 2: Read the JSON and extract numbers**

For `BM_ParseTextHeapAllocs` and `BM_ParseTextArenaAllocs`: `real_time`, `time_unit`, `allocs_per_iter`, `bytes_per_iter`.

- [ ] **Step 3: Write `docs/benchmarks/phase4-memory-arena-analysis.md`**

```markdown
# Phase 4 — Memory Allocations / Arena Comparison Analysis

Date: 2026-06-18
Raw data: `results/phase4-2026-06-18.json`

## Heap vs Arena: parsing the same text message

| Benchmark | Time (ns/iter) | Allocs/iter | Bytes/iter |
|---|---|---|---|
| BM_ParseTextHeapAllocs | <...> | <...> | <...> |
| BM_ParseTextArenaAllocs | <...> | <...> | <...> |

<Write 3-5 sentences: how many fewer allocations per parse does the Arena path need (ratio)? Does that match the message's structure (text fixture has a top-level ChatMessage + a QuoteInfo submessage + several strings — each would normally need its own heap allocation on the default path)? Is there a latency difference, and does it move in the direction allocation-count differences would predict (fewer allocations -> usually faster, since malloc/free have real per-call overhead)? Note whether allocs_per_iter for Arena is close to 0 (steady-state reuse) vs some small fraction (occasional new-block fetches), and that this is RAM amortized across many iterations, not literally zero per call.>

## Scope note

This covers Phase 4 only (parse-side heap-vs-Arena allocation comparison for the `text` message type). API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
```

Fill in the real numbers (no `<...>` placeholders may remain).

- [ ] **Step 4: Commit**

```bash
git add results/phase4-2026-06-18.json docs/benchmarks/phase4-memory-arena-analysis.md
git commit -m "Run Phase 4 benchmarks and record memory/Arena analysis"
```
