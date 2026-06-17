# Benchmark Phase 8 (Malformed-Input Parse Failure Cost) Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax for tracking. (Executed directly by Claude in this session, autonomously, per user authorization — no subagent dispatch, no per-step confirmation.)

**Goal:** Compare the cost of parsing a truncated byte string and an all-`0xFF` garbage byte string (both deterministically derived from the valid `text` fixture, both expected to fail) against the cost of successfully parsing the valid message (existing `BM_ParseText`).

**Architecture:** Pure addition to `src/bench.cpp` — 2 new benchmark functions. No CMakeLists.txt change.

## Global Constraints

- No CMake changes this phase — pure C++ addition.
- Do NOT delete/empty `build/` or `build/_deps/`.
- Scope: `text` message type only; 2 deterministic malformed-input shapes (truncated, all-0xFF garbage). No random/network-derived fuzzing.
- No placeholder text in the committed analysis doc.
- This is the last benchmark-implementation phase before the final report (Phase 6 is already documented as skipped).

---

### Task 1: Add the 2 new malformed-input benchmarks

**Files:**
- Modify: `src/bench.cpp`

- [ ] **Step 1: Insert the 2 new benchmark functions**

Insert right after the `BM_ConcurrentParseText`/`BENCHMARK(BM_ConcurrentParseText)...` block (Phase 7's last addition), before the closing `}  // namespace`:

```cpp
void BM_ParseTruncatedText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string full_bytes;
  static_cast<void>(original.SerializeToString(&full_bytes));
  const std::string truncated = full_bytes.substr(0, full_bytes.size() / 2);
  bool last_ok = true;
  for (auto _ : state) {
    ChatMessage parsed;
    last_ok = parsed.ParseFromString(truncated);
  }
  state.counters["parse_ok"] = last_ok ? 1.0 : 0.0;
  state.counters["bytes"] = static_cast<double>(truncated.size());
}
BENCHMARK(BM_ParseTruncatedText);

void BM_ParseGarbageText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string full_bytes;
  static_cast<void>(original.SerializeToString(&full_bytes));
  const std::string garbage(full_bytes.size(), '\xFF');
  bool last_ok = true;
  for (auto _ : state) {
    ChatMessage parsed;
    last_ok = parsed.ParseFromString(garbage);
  }
  state.counters["parse_ok"] = last_ok ? 1.0 : 0.0;
  state.counters["bytes"] = static_cast<double>(garbage.size());
}
BENCHMARK(BM_ParseGarbageText);
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j20`
Expected: build succeeds, no new warnings.

Run: `./build/proto_bench --benchmark_filter="^BM_ParseTruncatedText$|^BM_ParseGarbageText$"`
Expected: 2 rows, both with `parse_ok=0`. If either shows `parse_ok=1`, the malformed-input construction failed to actually be malformed — stop and reconstruct it (e.g. make the truncation shorter, or verify the 0xFF run is long enough to exceed the 10-byte varint limit) rather than silently reporting a misleading "successful parse" benchmark as a failure-path benchmark.

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, unchanged output.

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add malformed-input (truncated/garbage) parse failure benchmarks"
```

---

### Task 2: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase8-2026-06-18.json`
- Create: `docs/benchmarks/phase8-malformed-input-analysis.md`

- [ ] **Step 1: Run with JSON output**, including the existing `BM_ParseText` as the valid-input baseline:

```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase8-2026-06-18.json --benchmark_filter="^BM_ParseText$|^BM_ParseTruncatedText$|^BM_ParseGarbageText$"
```

- [ ] **Step 2: Extract numbers via python3**: `real_time`, `parse_ok`, `bytes` for all 3 rows.

- [ ] **Step 3: Write `docs/benchmarks/phase8-malformed-input-analysis.md`** with a comparison table and 3-5 sentences: is either failure path faster or slower than the successful-parse baseline, and why (truncated input should fail fast — the parser hits end-of-buffer immediately; the all-0xFF garbage forces the parser through a malformed-varint detection loop, up to the 10-byte varint cap, before erroring — plausibly slightly slower than truncation but still cheaper than fully parsing a valid 117-byte message). Confirm `parse_ok` is 0 for both malformed cases and 1 for the baseline.

- [ ] **Step 4: Commit**

```bash
git add results/phase8-2026-06-18.json docs/benchmarks/phase8-malformed-input-analysis.md
git commit -m "Run Phase 8 benchmarks and record malformed-input analysis"
```
