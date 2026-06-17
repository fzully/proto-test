# Benchmark Phase 2 (Field-Fill-Rate + Numeric-Encoding-Efficiency) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure how field-fill-rate (sparse vs. fully-populated optional fields) and `message_id` magnitude (small vs. snowflake-scale, varint-encoded) affect `ChatMessage` serialize/parse latency and on-wire size, using the existing `proto_bench` harness.

**Architecture:** Add two new shared fixtures (`BuildSparseTextMessage()`, `BuildTextMessageWithId(int64_t)`) to the existing `message_fixtures` module, refactor `BuildTextMessage()` to delegate to the new parameterized fixture (no behavior change), then add 6 new benchmark functions to the existing `bench.cpp` reusing the same measurement pattern as Phase 1. No new CMake targets or dependencies.

**Tech Stack:** C++20, the existing `proto_bench`/Google Benchmark harness from Phase 0+1 (no changes to build infrastructure).

## Global Constraints

- C++ standard is fixed at C++20 — do not change.
- No new CMake targets, no new `FetchContent` dependencies — this phase only adds C++ source code to existing files/targets.
- Do NOT delete, move, or empty the `build/` directory or anything under `build/_deps/`. This project's `build/_deps` holds a slow-to-refetch protobuf/Abseil/Benchmark checkout; a prior incident this project deleting it cost a multi-hour stall. Only run `cmake --build build -j20` (incremental) — do NOT run `cmake -S . -B build` unless you have a specific reason tied to a CMakeLists.txt change (this plan makes none).
- Scope is the `text` message type only — do not add fill-rate or numeric-encoding variants for `merged_forward`. Do not implement Phase 3-8 work (scalability, Arena, API-overhead, perf counters, concurrency, malformed-input).
- No placeholder text ("TBD", "<...>") may remain in the committed analysis doc — every number must be a real value read from the benchmark's JSON output.

---

### Task 1: Add `BuildSparseTextMessage()` and `BuildTextMessageWithId()` fixtures

**Files:**
- Modify: `src/message_fixtures.h`
- Modify: `src/message_fixtures.cpp`

**Interfaces:**
- Produces: `im::chat::v1::ChatMessage BuildTextMessageWithId(int64_t message_id);` and `im::chat::v1::ChatMessage BuildSparseTextMessage();`, both declared in `src/message_fixtures.h` in the global namespace. Task 2's `bench.cpp` will call these directly.
- `BuildTextMessage()` keeps its existing signature and behavior (delegates internally to `BuildTextMessageWithId(1001)`), so Phase 1's `BM_SerializeText`/`BM_ParseText`/`proto_test` callers are unaffected.

- [ ] **Step 1: Update `src/message_fixtures.h`**

Replace the file's contents with:

```cpp
#ifndef PROTO_TEST_MESSAGE_FIXTURES_H_
#define PROTO_TEST_MESSAGE_FIXTURES_H_

#include <cstdint>

#include "chat.pb.h"

im::chat::v1::ChatMessage BuildTextMessage();
im::chat::v1::ChatMessage BuildTextMessageWithId(int64_t message_id);
im::chat::v1::ChatMessage BuildSparseTextMessage();
im::chat::v1::ChatMessage BuildMergedForwardMessage();

#endif  // PROTO_TEST_MESSAGE_FIXTURES_H_
```

- [ ] **Step 2: Update `src/message_fixtures.cpp`**

Replace the file's contents with:

```cpp
#include "message_fixtures.h"

using im::chat::v1::ChatMessage;
using im::chat::v1::ForwardedItem;
using im::chat::v1::MergedForwardContent;
using im::chat::v1::QuoteInfo;

ChatMessage BuildTextMessageWithId(int64_t message_id) {
  ChatMessage msg;
  msg.set_message_id(message_id);
  msg.set_client_msg_id("client-uuid-abc123");
  msg.set_conversation_id(555);
  msg.set_conversation_type(im::chat::v1::CONVERSATION_TYPE_GROUP);
  msg.set_sender_id(42);
  msg.set_seq(17);
  msg.set_client_timestamp_ms(1750000000000);
  msg.set_server_timestamp_ms(1750000000050);
  msg.set_status(im::chat::v1::MESSAGE_STATUS_SENT);

  QuoteInfo* quote = msg.mutable_quote();
  quote->set_quoted_message_id(998);
  quote->set_quoted_sender_id(7);
  quote->set_content_preview("原消息预览文本");

  msg.add_mentioned_user_ids(7);
  msg.add_mentioned_user_ids(9);

  msg.mutable_text()->set_body("Hello, this is a test message.");

  return msg;
}

ChatMessage BuildTextMessage() { return BuildTextMessageWithId(1001); }

ChatMessage BuildSparseTextMessage() {
  ChatMessage msg;
  msg.set_message_id(1001);
  msg.set_client_msg_id("client-uuid-abc123");
  msg.set_conversation_id(555);
  msg.set_conversation_type(im::chat::v1::CONVERSATION_TYPE_GROUP);
  msg.set_sender_id(42);
  msg.set_seq(17);
  msg.set_client_timestamp_ms(1750000000000);
  msg.set_server_timestamp_ms(1750000000050);
  msg.set_status(im::chat::v1::MESSAGE_STATUS_SENT);

  msg.mutable_text()->set_body("Hello, this is a test message.");

  return msg;
}

ChatMessage BuildMergedForwardMessage() {
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

  ForwardedItem* item1 = merged->add_items();
  item1->set_message_id(101);
  item1->set_sender_id(7);
  item1->set_timestamp_ms(1749999999000);
  item1->mutable_text()->set_body("第一条被转发的消息");

  ForwardedItem* item2 = merged->add_items();
  item2->set_message_id(102);
  item2->set_sender_id(9);
  item2->set_timestamp_ms(1749999999500);
  item2->mutable_text()->set_body("第二条被转发的消息");

  return msg;
}
```

Note: `BuildSparseTextMessage()` deliberately omits `quote`, `mentioned_user_ids`, `edited`, `forward_info`, and `extra` — those fields keep their proto3 default (unset), unlike `BuildTextMessageWithId()` which sets `quote` and `mentioned_user_ids`. This is the only intended difference between the sparse and full fixtures; all other field values match exactly (`message_id=1001`, `conversation_id=555`, etc.) so the size/latency comparison isolates fill-rate, not content.

- [ ] **Step 3: Build and verify `BuildTextMessage()`'s behavior is unchanged**

Run: `cmake --build build -j20`
Expected: build succeeds, no errors, no new warnings.

Run: `./build/proto_test`
Expected: exit code 0, output unchanged from before this task:
```
TestTextMessageRoundTrip passed, serialized size = 117 bytes
TestMergedForwardRoundTrip passed, serialized size = 162 bytes
All protobuf round-trip tests passed.
```
The `117 bytes` and `162 bytes` values must match exactly — this proves `BuildTextMessage()`'s delegation to `BuildTextMessageWithId(1001)` produces a byte-identical message to before the refactor.

- [ ] **Step 4: Commit**

```bash
git add src/message_fixtures.h src/message_fixtures.cpp
git commit -m "Add sparse and parameterized-id text message fixtures"
```

---

### Task 2: Add the 6 Phase 2 benchmark functions to `bench.cpp`

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: `BuildSparseTextMessage()`, `BuildTextMessageWithId(int64_t)` from `src/message_fixtures.h` (Task 1).
- Produces: no new interfaces for later tasks; Task 3 only runs the resulting `proto_bench` binary.

- [ ] **Step 1: Insert 6 new benchmark functions into `src/bench.cpp`**

Insert the following functions right after the existing `BM_ParseMergedForward`/`BENCHMARK(BM_ParseMergedForward);` block, and before the closing `}  // namespace` line (the file's overall structure — includes, `using` declaration, anonymous namespace, `BENCHMARK_MAIN();` — stays as-is; this step only adds functions inside the existing anonymous namespace):

```cpp
void BM_SerializeSparseText(benchmark::State& state) {
  ChatMessage msg = BuildSparseTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeSparseText);

void BM_ParseSparseText(benchmark::State& state) {
  ChatMessage original = BuildSparseTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseSparseText);

void BM_SerializeSmallId(benchmark::State& state) {
  ChatMessage msg = BuildTextMessageWithId(1);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeSmallId);

void BM_ParseSmallId(benchmark::State& state) {
  ChatMessage original = BuildTextMessageWithId(1);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseSmallId);

void BM_SerializeLargeId(benchmark::State& state) {
  ChatMessage msg = BuildTextMessageWithId(1950123456789012345LL);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeLargeId);

void BM_ParseLargeId(benchmark::State& state) {
  ChatMessage original = BuildTextMessageWithId(1950123456789012345LL);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseLargeId);
```

Each function follows the same pattern as Phase 1's benchmarks: fixture construction (and, for parse benchmarks, the one-time serialize) happens outside the timed `for (auto _ : state)` loop, so only the target operation (`SerializeToString`/`ParseFromString`) is measured; `state.counters["bytes"]` records the on-wire size.

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j20`
Expected: build succeeds, no new warnings (the `static_cast<void>(...)` pattern from Phase 1 already suppresses the `[[nodiscard]]` warnings — use it consistently here too).

Run: `./build/proto_bench`
Expected: a console table with 10 rows total — the 4 from Phase 1 (`BM_SerializeText`, `BM_ParseText`, `BM_SerializeMergedForward`, `BM_ParseMergedForward`) plus the 6 new ones (`BM_SerializeSparseText`, `BM_ParseSparseText`, `BM_SerializeSmallId`, `BM_ParseSmallId`, `BM_SerializeLargeId`, `BM_ParseLargeId`), each with a non-zero `bytes` counter. Sanity-check the direction of the numbers:
- `BM_SerializeSparseText`'s `bytes` value must be smaller than `BM_SerializeText`'s `117` (sparse omits fields that the full message sets).
- `BM_SerializeLargeId`'s `bytes` value must be larger than `BM_SerializeSmallId`'s (the snowflake-scale `message_id` takes more varint bytes than `1`).

If either direction is wrong, the fixtures in Task 1 have a bug — stop and report BLOCKED rather than continuing.

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, output ends with `All protobuf round-trip tests passed.` (unchanged from Task 1's Step 3).

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add field-fill-rate and numeric-encoding benchmarks"
```

---

### Task 3: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase2-2026-06-18.json`
- Create: `docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md`

**Interfaces:**
- Consumes: `proto_bench` executable from Task 2 (already built — do not reconfigure or rebuild from scratch, just run it; rebuild incrementally with `cmake --build build -j20` only if needed).

- [ ] **Step 1: Run the benchmark with JSON output**

Run:
```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase2-2026-06-18.json
```
Expected: command exits 0; `results/phase2-2026-06-18.json` is created and contains a top-level `"benchmarks"` array with 10 entries (4 from Phase 1, 6 new).

- [ ] **Step 2: Read the JSON output and extract the numbers**

Run: `cat results/phase2-2026-06-18.json`

Record, for each of the 6 new entries (`BM_SerializeSparseText`, `BM_ParseSparseText`, `BM_SerializeSmallId`, `BM_ParseSmallId`, `BM_SerializeLargeId`, `BM_ParseLargeId`) and the 2 Phase 1 entries needed for the fill-rate comparison (`BM_SerializeText`, `BM_ParseText`, both `117` bytes): the benchmark `name`, `real_time` (with `time_unit`), `iterations`, and `bytes`.

- [ ] **Step 3: Write `docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md`**

Create the file with this structure, filling in the `<...>` placeholders with the actual numbers read from Step 2 (no placeholder text may remain in the committed file). Compute "Throughput (ops/sec)" as `1e9 / real_time` using the full-precision `real_time` value from the JSON (not a value rounded for the table), consistent with how the Phase 1 analysis doc was corrected:

```markdown
# Phase 2 — Field-Fill-Rate and Numeric-Encoding-Efficiency Analysis

Date: 2026-06-18
Raw data: `results/phase2-2026-06-18.json`

## Field fill rate: sparse vs. full text message

| Benchmark | Operation | Fill rate | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeSparseText | serialize | sparse | <...> | <...> | <...> |
| BM_SerializeText | serialize | full (Phase 1) | <...> | <...> | 117 |
| BM_ParseSparseText | parse | sparse | <...> | <...> | <...> |
| BM_ParseText | parse | full (Phase 1) | <...> | <...> | 117 |

<Write 2-3 sentences: how many bytes did omitting quote/mentioned_user_ids save, as an absolute and percentage difference from 117 bytes? Is the serialize/parse latency difference proportional to the size difference, smaller, or larger?>

## Numeric encoding: small vs. snowflake-scale message_id

| Benchmark | Operation | message_id | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeSmallId | serialize | 1 | <...> | <...> | <...> |
| BM_SerializeLargeId | serialize | 1950123456789012345 | <...> | <...> | <...> |
| BM_ParseSmallId | parse | 1 | <...> | <...> | <...> |
| BM_ParseLargeId | parse | 1950123456789012345 | <...> | <...> | <...> |

<Write 2-3 sentences: how many extra bytes does the large message_id cost versus the small one? Varint encoding for int64 takes 1 byte for values 0-127 and up to 10 bytes for the largest values — does the observed byte difference match what you'd expect from encoding a ~19-digit number versus a 1-digit number in varint? Is there a measurable latency difference, or is it within noise?>

## Scope note

This covers Phase 2 only (field-fill-rate and message_id numeric-encoding efficiency, both isolated to the `text` message type). Scalability sweeps, Arena, API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
```

- [ ] **Step 4: Commit**

```bash
git add results/phase2-2026-06-18.json docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md
git commit -m "Run Phase 2 benchmarks and record field-fill-rate/numeric-encoding analysis"
```
