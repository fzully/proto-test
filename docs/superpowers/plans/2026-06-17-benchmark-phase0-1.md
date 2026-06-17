# Benchmark Phase 0+1 (Infra + Throughput/Size) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a Google Benchmark harness for this protobuf experiment and measure serialize/deserialize throughput, latency, and on-wire size for the existing `text` and `merged_forward` `ChatMessage` fixtures.

**Architecture:** Extract the existing message-construction code into a shared `message_fixtures` module so both the correctness test (`proto_test`) and the new benchmark binary (`proto_bench`) build identical test data from one source. Move the protobuf-generated code into a small `chat_proto` static library so two executables can link it without invoking `protoc` twice. Add Google Benchmark via the same `FetchContent` pattern already used for protobuf, and write four benchmark functions that isolate serialize/parse cost from fixture construction.

**Tech Stack:** C++20, CMake 3.16+ `FetchContent`, protobuf v35.1 (already pinned), Google Benchmark v1.9.1.

## Global Constraints

- C++ standard is fixed at C++20 (`CMakeLists.txt:4-5`) — do not change.
- All third-party dependencies are pulled via CMake `FetchContent` from pinned git tags, never system packages — matches the existing protobuf setup.
- `protobuf_BUILD_TESTS`/`protobuf_INSTALL` stay `OFF`; the equivalent Google Benchmark testing options (`BENCHMARK_ENABLE_TESTING`, `BENCHMARK_ENABLE_GTEST_TESTS`) must also be `OFF` so no extra test framework gets pulled in.
- This plan covers Phase 0 (infra) + Phase 1 (throughput/latency/size on the 2 existing message types) only. Do not add benchmarks for field-fill-rate, scalability sweeps, Arena, API-overhead, perf counters, concurrency, or malformed-input — those are separate future phases.
- No protobuf vs JSON comparison — explicitly out of scope per the design spec.

---

### Task 1: Extract message fixtures into a shared module

**Files:**
- Create: `src/message_fixtures.h`
- Create: `src/message_fixtures.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt:22` (the `add_executable(proto_test ...)` line)

**Interfaces:**
- Produces: `ChatMessage BuildTextMessage();` and `ChatMessage BuildMergedForwardMessage();`, declared in `src/message_fixtures.h` in the global namespace, returning `im::chat::v1::ChatMessage` by value. Task 3's `bench.cpp` will call these directly.

- [ ] **Step 1: Create `src/message_fixtures.h`**

```cpp
#ifndef PROTO_TEST_MESSAGE_FIXTURES_H_
#define PROTO_TEST_MESSAGE_FIXTURES_H_

#include "chat.pb.h"

im::chat::v1::ChatMessage BuildTextMessage();
im::chat::v1::ChatMessage BuildMergedForwardMessage();

#endif  // PROTO_TEST_MESSAGE_FIXTURES_H_
```

- [ ] **Step 2: Create `src/message_fixtures.cpp` with the bodies moved out of `main.cpp`**

```cpp
#include "message_fixtures.h"

using im::chat::v1::ChatMessage;
using im::chat::v1::ForwardedItem;
using im::chat::v1::MergedForwardContent;
using im::chat::v1::QuoteInfo;

ChatMessage BuildTextMessage() {
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

  QuoteInfo* quote = msg.mutable_quote();
  quote->set_quoted_message_id(998);
  quote->set_quoted_sender_id(7);
  quote->set_content_preview("原消息预览文本");

  msg.add_mentioned_user_ids(7);
  msg.add_mentioned_user_ids(9);

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

- [ ] **Step 3: Update `src/main.cpp` to use the extracted fixtures**

Remove the `BuildTextMessage()` and `BuildMergedForwardMessage()` definitions from the anonymous namespace in `src/main.cpp` (currently lines 14-37 and 72-100), and add the new header to the includes at the top:

```cpp
#include <cassert>
#include <iostream>
#include <string>

#include "chat.pb.h"
#include "message_fixtures.h"
```

After this change, `src/main.cpp` should contain only `TestTextMessageRoundTrip()`, `TestMergedForwardRoundTrip()`, and `main()` inside the anonymous namespace (plus `main()` outside it, unchanged) — both test functions already call `BuildTextMessage()` / `BuildMergedForwardMessage()` unqualified, which now resolve to the global-namespace declarations from `message_fixtures.h`.

- [ ] **Step 4: Add the new source file to the `proto_test` target**

In `CMakeLists.txt`, change:

```cmake
add_executable(proto_test src/main.cpp)
```

to:

```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp)
```

- [ ] **Step 5: Build and verify behavior is unchanged**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: build succeeds, no errors.

Run: `./build/proto_test`
Expected: exit code 0, output ends with `All protobuf round-trip tests passed.` (same as before the refactor — this confirms the extraction didn't change behavior).

- [ ] **Step 6: Commit**

```bash
git add src/message_fixtures.h src/message_fixtures.cpp src/main.cpp CMakeLists.txt
git commit -m "Extract ChatMessage fixtures into a shared module"
```

---

### Task 2: Share generated protobuf code via a `chat_proto` library

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: CMake target `chat_proto` (STATIC library), exposing `chat.pb.h` via `PUBLIC` include directory and linking `protobuf::libprotobuf` `PUBLIC`. Task 3's `proto_bench` target links against `chat_proto` instead of running `protobuf_generate()` a second time.
- Consumes: nothing from Task 1.

- [ ] **Step 1: Replace the `proto_test`-specific generation with a shared `chat_proto` library**

Edit `CMakeLists.txt` so the section from `add_executable(proto_test ...)` through `target_include_directories(proto_test ...)` becomes:

```cmake
# chat_proto holds the generated chat.pb.{h,cc} so both proto_test and
# proto_bench can link the same compiled protobuf code instead of running
# protoc twice.
add_library(chat_proto STATIC)
target_link_libraries(chat_proto PUBLIC protobuf::libprotobuf)

# protobuf_generate() invokes protoc with --cpp_out pointed at this directory
# but does not create it itself, so it must exist before the build runs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)

protobuf_generate(
  TARGET chat_proto
  PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/proto/chat.proto
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_include_directories(chat_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_executable(proto_test src/main.cpp src/message_fixtures.cpp)
target_link_libraries(proto_test PRIVATE chat_proto)
```

The full file should now read:

```cmake
cmake_minimum_required(VERSION 3.16)
project(proto_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG v35.1
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(protobuf)

# protobuf_generate() is normally pulled in via find_package(protobuf CONFIG),
# but FetchContent builds protobuf as a subdirectory instead, so we include
# the function definition directly from the fetched source tree.
include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)

# chat_proto holds the generated chat.pb.{h,cc} so both proto_test and
# proto_bench can link the same compiled protobuf code instead of running
# protoc twice.
add_library(chat_proto STATIC)
target_link_libraries(chat_proto PUBLIC protobuf::libprotobuf)

# protobuf_generate() invokes protoc with --cpp_out pointed at this directory
# but does not create it itself, so it must exist before the build runs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)

protobuf_generate(
  TARGET chat_proto
  PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/proto/chat.proto
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_include_directories(chat_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_executable(proto_test src/main.cpp src/message_fixtures.cpp)
target_link_libraries(proto_test PRIVATE chat_proto)
```

- [ ] **Step 2: Re-build and verify behavior is unchanged**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: build succeeds. `chat_proto` builds as a static library, `proto_test` links against it.

Run: `./build/proto_test`
Expected: exit code 0, output ends with `All protobuf round-trip tests passed.`

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "Move generated protobuf code into a shared chat_proto library"
```

---

### Task 3: Add Google Benchmark and implement the throughput/size benchmarks

**Files:**
- Create: `src/bench.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `BuildTextMessage()`, `BuildMergedForwardMessage()` from `src/message_fixtures.h` (Task 1); `chat_proto` CMake target (Task 2).
- Produces: executable target `proto_bench`, runnable as `./build/proto_bench`.

- [ ] **Step 1: Add the Google Benchmark dependency and `proto_bench` target**

Append to the end of `CMakeLists.txt`:

```cmake

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp)
target_link_libraries(proto_bench PRIVATE chat_proto benchmark::benchmark)
```

- [ ] **Step 2: Create `src/bench.cpp` with the 4 benchmarks**

```cpp
#include <string>

#include "benchmark/benchmark.h"
#include "chat.pb.h"
#include "message_fixtures.h"

using im::chat::v1::ChatMessage;

namespace {

void BM_SerializeText(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    msg.SerializeToString(&bytes);
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeText);

void BM_ParseText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  original.SerializeToString(&bytes);
  for (auto _ : state) {
    ChatMessage parsed;
    parsed.ParseFromString(bytes);
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseText);

void BM_SerializeMergedForward(benchmark::State& state) {
  ChatMessage msg = BuildMergedForwardMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    msg.SerializeToString(&bytes);
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeMergedForward);

void BM_ParseMergedForward(benchmark::State& state) {
  ChatMessage original = BuildMergedForwardMessage();
  std::string bytes;
  original.SerializeToString(&bytes);
  for (auto _ : state) {
    ChatMessage parsed;
    parsed.ParseFromString(bytes);
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseMergedForward);

}  // namespace

BENCHMARK_MAIN();
```

Note: each `*Serialize*` benchmark builds its fixture once, outside the timed loop, so only the `SerializeToString` call itself is measured. Each `*Parse*` benchmark serializes once outside the loop and only times `ParseFromString`. The `bytes` counter reports the on-wire size for that message type.

- [ ] **Step 3: Build and run**

Run: `cmake -S . -B build && cmake --build build -j`
Expected: build succeeds, `proto_bench` executable produced alongside `proto_test`.

Run: `./build/proto_bench`
Expected: a console table with exactly 4 rows — `BM_SerializeText`, `BM_ParseText`, `BM_SerializeMergedForward`, `BM_ParseMergedForward` — each showing a `Time`, `CPU`, `Iterations` column and a non-zero `bytes` counter. No crashes or errors.

- [ ] **Step 4: Re-verify `proto_test` still passes (shared fixtures/library untouched in behavior)**

Run: `./build/proto_test`
Expected: exit code 0, output ends with `All protobuf round-trip tests passed.`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/bench.cpp
git commit -m "Add Google Benchmark and throughput/size benchmarks for text and merged_forward messages"
```

---

### Task 4: Run benchmarks, save structured results, write the analysis doc

**Files:**
- Create: `results/phase1-2026-06-17.json`
- Create: `docs/benchmarks/phase1-throughput-size-analysis.md`

**Interfaces:**
- Consumes: `proto_bench` executable from Task 3.

- [ ] **Step 1: Create the `results/` directory and run the benchmark with JSON output**

Run:
```bash
mkdir -p results
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase1-2026-06-17.json
```
Expected: command exits 0; `results/phase1-2026-06-17.json` is created and contains a top-level `"benchmarks"` array with 4 entries (one per `BM_*` function), each with `"real_time"`, `"cpu_time"`, `"iterations"`, and a `"bytes"` counter field.

- [ ] **Step 2: Read the JSON output and extract the numbers**

Run: `cat results/phase1-2026-06-17.json`

Record, for each of the 4 benchmark entries: the benchmark `name`, `real_time` (with `time_unit`), `iterations`, and `bytes`.

- [ ] **Step 3: Write `docs/benchmarks/phase1-throughput-size-analysis.md`**

Create the directory and file with this structure, filling in the `<...>` placeholders with the actual numbers read from Step 2 (no placeholder text may remain in the committed file):

```markdown
# Phase 1 — Throughput, Latency, and Size Analysis

Date: 2026-06-17
Raw data: `results/phase1-2026-06-17.json`

## Results

| Benchmark | Operation | Message type | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeText | serialize | text | <...> | <...> | <...> |
| BM_ParseText | parse | text | <...> | <...> | <...> |
| BM_SerializeMergedForward | serialize | merged_forward | <...> | <...> | <...> |
| BM_ParseMergedForward | parse | merged_forward | <...> | <...> | <...> |

Throughput (ops/sec) = 1e9 / time (ns/iter).

## Observations

<Write 2-4 sentences comparing text vs merged_forward: how much slower/larger is merged_forward, and whether that's proportional to its extra nested ForwardedItem messages. Compare serialize vs parse cost for the same message type.>

## Scope note

This covers Phase 1 only (throughput/latency/size on the existing 2 message types). Field-fill-rate, numeric encoding efficiency, scalability sweeps, Arena, API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
```

- [ ] **Step 4: Commit**

```bash
git add results/phase1-2026-06-17.json docs/benchmarks/phase1-throughput-size-analysis.md
git commit -m "Run Phase 1 benchmarks and record throughput/size analysis"
```
