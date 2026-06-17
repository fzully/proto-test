# Benchmark Phase 7 (Concurrent Throughput Scaling) Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax for tracking. (Executed directly by Claude in this session, autonomously, per user authorization — no subagent dispatch, no per-step confirmation.)

**Goal:** Measure aggregate serialize/parse throughput for the `text` fixture across 1/2/4/8/16/20 concurrent threads, each thread fully independent (no shared mutable state), to see how close to linear the scaling is on this 20-core host.

**Architecture:** Pure addition to `src/bench.cpp` — 2 new benchmark functions (identical bodies to existing `BM_SerializeText`/`BM_ParseText`) registered with `->Threads(N)` for N in {1,2,4,8,16,20}. No CMakeLists.txt change.

## Global Constraints

- No CMake changes this phase — pure C++ addition, Google Benchmark's `->Threads()` is already part of the linked library.
- Do NOT delete/empty `build/` or `build/_deps/`.
- Scope: `text` message type only, serialize+parse only. No shared/locked state between threads — each thread builds its own local message/buffer.
- No placeholder text in the committed analysis doc.

---

### Task 1: Add the 2 new concurrent benchmarks

**Files:**
- Modify: `src/bench.cpp`

- [ ] **Step 1: Insert the 2 new benchmark functions**

Insert right after the `BM_SerializeToCodedStream`/`BENCHMARK(BM_SerializeToCodedStream);` block (Phase 5's last addition), before the closing `}  // namespace`:

```cpp
void BM_ConcurrentSerializeText(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentSerializeText)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ConcurrentParseText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentParseText)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j20`
Expected: build succeeds, no new warnings.

Run: `./build/proto_bench --benchmark_filter="Concurrent"`
Expected: 12 rows (2 functions × 6 thread counts), `bytes` counter = 117 throughout.

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, unchanged output.

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add concurrent serialize/parse throughput-scaling benchmarks"
```

---

### Task 2: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase7-2026-06-18.json`
- Create: `docs/benchmarks/phase7-concurrency-analysis.md`

- [ ] **Step 1: Run with JSON output**

```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase7-2026-06-18.json --benchmark_filter="Concurrent"
```

- [ ] **Step 2: Extract numbers and compute aggregate throughput via python3** for each (function, thread count): `real_time` (ns, one iteration's wall-clock across all threads), aggregate throughput = `threads * 1e9 / real_time`. Compute the scaling ratio relative to the 1-thread aggregate throughput (ideal = thread count).

- [ ] **Step 3: Write `docs/benchmarks/phase7-concurrency-analysis.md`** with 2 tables (serialize, parse) of thread-count / real_time / aggregate throughput / scaling ratio, and 3-5 sentences of analysis: how close to linear is the scaling up to 20 threads, where (if anywhere) does it visibly deviate, and a plausible mechanism (turbo-boost frequency scaling down under full core load is the most likely candidate on this host, given each thread's working set is tiny and independent).

- [ ] **Step 4: Commit**

```bash
git add results/phase7-2026-06-18.json docs/benchmarks/phase7-concurrency-analysis.md
git commit -m "Run Phase 7 benchmarks and record concurrency scaling analysis"
```
