# Benchmark Phase 5 (Serialization API Overhead) Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax for tracking. (Executed directly by Claude in this session, autonomously, per user authorization — no subagent dispatch, no per-step confirmation.)

**Goal:** Compare the latency of 3 different protobuf serialization API shapes (fresh-string-per-call, preallocated raw-array, CodedOutputStream-over-ArrayOutputStream) against the existing reused-string baseline, for the same `text` fixture.

**Architecture:** Pure addition to `src/bench.cpp` — 3 new benchmark functions plus 3 new includes. No CMakeLists.txt change (no new source files, no new dependency).

## Global Constraints

- No CMake changes this phase — pure C++ addition.
- Do NOT delete/empty `build/` or `build/_deps/`.
- Scope: serialize-side API comparison only, `text` message type only (reusing `BuildTextMessage()`). No parse-side API variants, no Phase 6-8 work.
- No placeholder text in the committed analysis doc.

---

### Task 1: Add the 3 new serialization-API benchmarks

**Files:**
- Modify: `src/bench.cpp`

- [ ] **Step 1: Add includes**

At the top of `src/bench.cpp`, add after the existing `#include <string>`:

```cpp
#include <vector>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
```

- [ ] **Step 2: Insert the 3 new benchmark functions**

Insert right after the `BM_ParseTextArenaAllocs`/`BENCHMARK(BM_ParseTextArenaAllocs);` block (Phase 4's last addition), before the closing `}  // namespace`:

```cpp
void BM_SerializeToFreshString(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::size_t last_size = 0;
  for (auto _ : state) {
    std::string bytes;
    static_cast<void>(msg.SerializeToString(&bytes));
    last_size = bytes.size();
  }
  state.counters["bytes"] = static_cast<double>(last_size);
}
BENCHMARK(BM_SerializeToFreshString);

void BM_SerializeToPreallocatedArray(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  const int size = static_cast<int>(msg.ByteSizeLong());
  std::vector<char> buffer(static_cast<std::size_t>(size));
  for (auto _ : state) {
    bool ok = msg.SerializeToArray(buffer.data(), size);
    static_cast<void>(ok);
  }
  state.counters["bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_SerializeToPreallocatedArray);

void BM_SerializeToCodedStream(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  const int size = static_cast<int>(msg.ByteSizeLong());
  std::vector<char> buffer(static_cast<std::size_t>(size));
  for (auto _ : state) {
    google::protobuf::io::ArrayOutputStream array_stream(buffer.data(), size);
    google::protobuf::io::CodedOutputStream coded_stream(&array_stream);
    msg.SerializeToCodedStream(&coded_stream);
  }
  state.counters["bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_SerializeToCodedStream);
```

- [ ] **Step 3: Build and run**

Run: `cmake --build build -j20`
Expected: build succeeds, no new warnings.

Run: `./build/proto_bench --benchmark_filter="Serialize"`
Expected: all serialize benchmarks (existing + 3 new) print, `bytes` counter = 117 for the `text`-fixture ones.

- [ ] **Step 4: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, unchanged output.

- [ ] **Step 5: Commit**

```bash
git add src/bench.cpp
git commit -m "Add serialization API overhead benchmarks (fresh-string/array/coded-stream)"
```

---

### Task 2: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase5-2026-06-18.json`
- Create: `docs/benchmarks/phase5-api-overhead-analysis.md`

- [ ] **Step 1: Run with JSON output**

```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase5-2026-06-18.json
```

- [ ] **Step 2: Extract numbers** for `BM_SerializeText` (existing baseline), `BM_SerializeToFreshString`, `BM_SerializeToPreallocatedArray`, `BM_SerializeToCodedStream`: `real_time`, `time_unit`, `bytes`. Compute precise ns/iter and throughput (`1e9/real_time`) via `python3`, not by-hand rounding.

- [ ] **Step 3: Write `docs/benchmarks/phase5-api-overhead-analysis.md`** with a comparison table and 3-5 sentences of analysis: does the fresh-string path show a measurable allocation tax vs the reused-string baseline? Do the zero-copy-buffer paths (array / coded stream) beat or match the reused-string baseline? Any surprises, and a plausible mechanism.

- [ ] **Step 4: Commit**

```bash
git add results/phase5-2026-06-18.json docs/benchmarks/phase5-api-overhead-analysis.md
git commit -m "Run Phase 5 benchmarks and record API overhead analysis"
```
