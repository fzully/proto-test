# Benchmark Phase 3 (Scalability Sweep) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure how serialize/parse latency and on-wire size scale with the length of `mentioned_user_ids` (repeated scalar) and `merged_forward.items` (repeated message), at lengths 1/10/100/1000, using Google Benchmark's built-in `Arg()` parameterization.

**Architecture:** Add two new independent fixtures (`BuildTextMessageWithMentionCount`, `BuildMergedForwardMessageWithItemCount`) to the existing `message_fixtures` module, then add 4 parameterized benchmark functions to the existing `bench.cpp` that read the sweep length from `state.range(0)`. No new CMake targets or dependencies.

**Tech Stack:** C++20, the existing `proto_bench`/Google Benchmark harness (unchanged build infra).

## Global Constraints

- C++20 fixed. No new CMake targets/dependencies — this phase only adds C++ source.
- Do NOT delete/empty `build/` or `build/_deps/`. Only run `cmake --build build -j20` (incremental); do not run `cmake -S . -B build` (no CMakeLists.txt change in this plan).
- `BuildTextMessage()`, `BuildTextMessageWithId()`, `BuildMergedForwardMessage()`, `BuildSparseTextMessage()` (from Phases 1-2) must remain byte-identical in behavior — the new fixtures in this plan are independent functions, not modifications of those.
- Scope: only `mentioned_user_ids` and `merged_forward.items`, only lengths 1/10/100/1000. No Phase 4-8 work (Arena, API-overhead, perf counters, concurrency, malformed-input).
- No placeholder text in the committed analysis doc.

---

### Task 1: Add scalability fixtures

**Files:**
- Modify: `src/message_fixtures.h`
- Modify: `src/message_fixtures.cpp`

**Interfaces:**
- Produces: `im::chat::v1::ChatMessage BuildTextMessageWithMentionCount(int mention_count);` and `im::chat::v1::ChatMessage BuildMergedForwardMessageWithItemCount(int item_count);`, both in the global namespace, declared in `src/message_fixtures.h`.

- [ ] **Step 1: Add the two new declarations to `src/message_fixtures.h`**

Add these two lines right after the existing `BuildSparseTextMessage()` declaration (before `BuildMergedForwardMessage()`'s declaration), so the file reads:

```cpp
#ifndef PROTO_TEST_MESSAGE_FIXTURES_H_
#define PROTO_TEST_MESSAGE_FIXTURES_H_

#include <cstdint>

#include "chat.pb.h"

im::chat::v1::ChatMessage BuildTextMessage();
im::chat::v1::ChatMessage BuildTextMessageWithId(int64_t message_id);
im::chat::v1::ChatMessage BuildSparseTextMessage();
im::chat::v1::ChatMessage BuildTextMessageWithMentionCount(int mention_count);
im::chat::v1::ChatMessage BuildMergedForwardMessage();
im::chat::v1::ChatMessage BuildMergedForwardMessageWithItemCount(int item_count);

#endif  // PROTO_TEST_MESSAGE_FIXTURES_H_
```

- [ ] **Step 2: Add the two new function definitions to `src/message_fixtures.cpp`**

Add `BuildTextMessageWithMentionCount` right after the existing `BuildSparseTextMessage()` function body, and add `BuildMergedForwardMessageWithItemCount` right after the existing `BuildMergedForwardMessage()` function body (at the end of the file). The rest of the file (the existing 4 functions) is untouched.

```cpp
ChatMessage BuildTextMessageWithMentionCount(int mention_count) {
  ChatMessage msg = BuildTextMessageWithId(1001);
  msg.clear_mentioned_user_ids();
  for (int i = 1; i <= mention_count; ++i) {
    msg.add_mentioned_user_ids(i);
  }
  return msg;
}
```

```cpp
ChatMessage BuildMergedForwardMessageWithItemCount(int item_count) {
  ChatMessage msg;
  msg.set_message_id(2002);
  msg.set_client_msg_id("client-uuid-def456");
  msg.set_conversation_id(777);
  msg.set_conversation_type(im::chat::v1::CONVERSATION_TYPE_SINGLE);
  msg.set_sender_id(42);
  msg.set_seq(18);
  msg.set_client_timestamp_ms(1750000001000);
  msg.set_server_timestamp_ms(1750000001050);
  msg.set_status(im::chat::v1::MESSAGE_STATUS_SENT);

  MergedForwardContent* merged = msg.mutable_merged_forward();
  merged->set_title("群聊的聊天记录");

  for (int i = 0; i < item_count; ++i) {
    ForwardedItem* item = merged->add_items();
    item->set_message_id(101 + i);
    item->set_sender_id(7);
    item->set_timestamp_ms(1749999999000 + i);
    item->mutable_text()->set_body("被转发的消息");
  }

  return msg;
}
```

`BuildTextMessageWithMentionCount` reuses `BuildTextMessageWithId(1001)` as its base (same envelope/quote as the Phase 1 full fixture) and only replaces `mentioned_user_ids`. `BuildMergedForwardMessageWithItemCount` is a fully independent function (does NOT call or modify `BuildMergedForwardMessage()`) so the existing fixture's exact byte-for-byte output is unaffected — every `ForwardedItem` uses the same short body text (`"被转发的消息"`) regardless of `item_count`, so size differences come only from the repeated-field count, not from varying text length.

- [ ] **Step 3: Build and verify existing behavior is unchanged**

Run: `cmake --build build -j20`
Expected: build succeeds, no errors, no new warnings.

Run: `./build/proto_test`
Expected: exit code 0, output unchanged:
```
TestTextMessageRoundTrip passed, serialized size = 117 bytes
TestMergedForwardRoundTrip passed, serialized size = 162 bytes
All protobuf round-trip tests passed.
```

- [ ] **Step 4: Commit**

```bash
git add src/message_fixtures.h src/message_fixtures.cpp
git commit -m "Add scalability sweep fixtures for mentioned_user_ids and merged_forward.items"
```

---

### Task 2: Add the 4 parameterized scalability benchmarks to `bench.cpp`

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: `BuildTextMessageWithMentionCount(int)`, `BuildMergedForwardMessageWithItemCount(int)` from `src/message_fixtures.h` (Task 1).

- [ ] **Step 1: Insert 4 new parameterized benchmark functions into `src/bench.cpp`**

Insert the following right after the existing `BM_ParseLargeId`/`BENCHMARK(BM_ParseLargeId);` block, before the closing `}  // namespace` line:

```cpp
void BM_SerializeMentions(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage msg = BuildTextMessageWithMentionCount(n);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeMentions)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_ParseMentions(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage original = BuildTextMessageWithMentionCount(n);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseMentions)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_SerializeMergedItems(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage msg = BuildMergedForwardMessageWithItemCount(n);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeMergedItems)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_ParseMergedItems(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage original = BuildMergedForwardMessageWithItemCount(n);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseMergedItems)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);
```

Each function follows the same isolation pattern as Phases 1-2 (construction outside the timed loop; `static_cast<void>(...)` around Serialize/Parse calls; `state.counters["bytes"]`). `->Arg(1)->Arg(10)->Arg(100)->Arg(1000)` makes Google Benchmark run each function 4 times, once per length, automatically naming the results `BM_SerializeMentions/1`, `BM_SerializeMentions/10`, etc.

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j20`
Expected: build succeeds, no new warnings.

Run: `./build/proto_bench`
Expected: console table now has 16 additional rows (4 functions × 4 args), named `BM_SerializeMentions/1` .. `/1000`, `BM_ParseMentions/1` .. `/1000`, `BM_SerializeMergedItems/1` .. `/1000`, `BM_ParseMergedItems/1` .. `/1000`. Sanity-check: for each of the 4 functions, the `bytes` value at `/1000` must be larger than at `/1`. If not, the fixtures have a bug — stop and report BLOCKED with the actual numbers.

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, unchanged output (117 / 162 bytes).

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add scalability sweep benchmarks (mentioned_user_ids, merged_forward.items)"
```

---

### Task 3: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase3-2026-06-18.json`
- Create: `docs/benchmarks/phase3-scalability-analysis.md`

**Interfaces:**
- Consumes: `proto_bench` executable from Task 2 (already built — do not reconfigure or rebuild from scratch, just run it).

- [ ] **Step 1: Run with JSON output**

Run:
```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase3-2026-06-18.json
```
Expected: exit 0; the `"benchmarks"` array contains all prior entries plus 16 new ones named `BM_SerializeMentions/<n>`, `BM_ParseMentions/<n>`, `BM_SerializeMergedItems/<n>`, `BM_ParseMergedItems/<n>` for `<n>` in `1, 10, 100, 1000`.

- [ ] **Step 2: Read the JSON and extract numbers**

Run: `cat results/phase3-2026-06-18.json`

For each of the 16 new entries, record `name`, `real_time` (+ `time_unit`), and `bytes`.

- [ ] **Step 3: Write `docs/benchmarks/phase3-scalability-analysis.md`**

Use this structure, filling in real numbers (no `<...>` placeholders may remain). Compute "Throughput (ops/sec)" as `1e9 / real_time` from the full-precision JSON value, consistent with prior phases:

```markdown
# Phase 3 — Scalability Sweep Analysis

Date: 2026-06-18
Raw data: `results/phase3-2026-06-18.json`

## mentioned_user_ids (repeated int64)

| n | Operation | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|
| 1 | serialize | <...> | <...> | <...> |
| 10 | serialize | <...> | <...> | <...> |
| 100 | serialize | <...> | <...> | <...> |
| 1000 | serialize | <...> | <...> | <...> |
| 1 | parse | <...> | <...> | <...> |
| 10 | parse | <...> | <...> | <...> |
| 100 | parse | <...> | <...> | <...> |
| 1000 | parse | <...> | <...> | <...> |

<Write 2-4 sentences: is size(n) ≈ size(1) + (n-1)*bytes_per_id roughly linear? What's bytes_per_id empirically (e.g. (size(1000)-size(1))/999)? Is time(n) roughly linear in n, or does it show a non-linear jump at n=1000 (which would suggest allocation overhead)?>

## merged_forward.items (repeated message)

| n | Operation | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|
| 1 | serialize | <...> | <...> | <...> |
| 10 | serialize | <...> | <...> | <...> |
| 100 | serialize | <...> | <...> | <...> |
| 1000 | serialize | <...> | <...> | <...> |
| 1 | parse | <...> | <...> | <...> |
| 10 | parse | <...> | <...> | <...> |
| 100 | parse | <...> | <...> | <...> |
| 1000 | parse | <...> | <...> | <...> |

<Write 2-4 sentences: same linearity analysis for the repeated-message case. Is bytes_per_item larger than bytes_per_id (expected, since each ForwardedItem has its own tag/length framing plus 4 sub-fields)? Compare the per-element latency cost (e.g. (time(1000)-time(1))/999) between the scalar and message cases — which is more expensive per element, and is that consistent with message-type repeated fields requiring more parsing work per element than a scalar varint?>

## Scope note

This covers Phase 3 only (scalability of `mentioned_user_ids` and `merged_forward.items` length, 1/10/100/1000). Arena/memory, API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
```

- [ ] **Step 4: Commit**

```bash
git add results/phase3-2026-06-18.json docs/benchmarks/phase3-scalability-analysis.md
git commit -m "Run Phase 3 benchmarks and record scalability analysis"
```
