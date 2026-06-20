# Phase 10 — JSON Library Shootout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add hand-written JSON encode/decode for 4 libraries (nlohmann/json, RapidJSON, yyjson, cJSON) on the existing `BuildTextMessage`/`BuildMergedForwardMessage` fixtures, benchmark them, and pick a winner for the future Phase 11 vs-Protobuf PK.

**Architecture:** Each library gets its own namespaced fixture file pair (mirrors `sbe_message_fixtures`), sharing one `DecodedTextMessage`/`DecodedMergedForwardMessage` struct pair for decode output. `proto_test` round-trip-checks each library; `proto_bench` times encode/decode for each.

**Tech Stack:** C++20, CMake `FetchContent`, Google Benchmark, nlohmann/json v3.12.0, RapidJSON v1.1.0, yyjson 0.12.0, cJSON v1.7.19.

## Global Constraints

- Field names: camelCase. int64 fields and enum fields serialize as JSON strings (not JSON number/native enum) — see spec section 1 for the exact two field tables (`TextMessage`, `MergedForwardMessage`).
- Output must be compact/minified (no pretty-print) for every library.
- No protobuf reflection involved anywhere (`chat.proto` is `LITE_RUNTIME`) — all mapping is hand-written.
- Always build with `cmake --build build -j$(nproc)` (explicit job count).
- Full spec: `docs/superpowers/specs/2026-06-20-benchmark-phase10-json-shootout-design.md`.

---

## Task 1: CMake — fetch and link the 4 JSON libraries

**Files:**
- Modify: `CMakeLists.txt` (append after the existing last line)

**Interfaces:**
- Produces: link targets `nlohmann_json::nlohmann_json`, `yyjson`, `cjson` available to `proto_bench`/`proto_test`; include path `${rapidjson_SOURCE_DIR}/include` and `${cjson_SOURCE_DIR}` available via `target_include_directories`.

- [ ] **Step 1: Append the FetchContent block**

Add to the end of `CMakeLists.txt` (current last line is `target_link_libraries(proto_bench PRIVATE chat_sbe)`):

```cmake

# ---- Phase 10: JSON library shootout (nlohmann/json, RapidJSON, yyjson, cJSON) ----
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG v1.1.0
  GIT_SHALLOW TRUE
)
set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidjson)
# RapidJSON's own CMakeLists.txt does not export a consumable target (its
# include_directories() call is scoped to its own subdirectory only), so we
# add its include path manually below.

FetchContent_Declare(
  yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(yyjson)

FetchContent_Declare(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.19
  GIT_SHALLOW TRUE
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)
# cJSON's CMakeLists.txt does not call target_include_directories() on its
# own target, so we add ${cjson_SOURCE_DIR} manually below (cJSON.h sits at
# the repo root, so the include is "cJSON.h", not "cjson/cJSON.h").

target_include_directories(proto_test PRIVATE ${rapidjson_SOURCE_DIR}/include ${cjson_SOURCE_DIR})
target_include_directories(proto_bench PRIVATE ${rapidjson_SOURCE_DIR}/include ${cjson_SOURCE_DIR})
target_link_libraries(proto_test PRIVATE nlohmann_json::nlohmann_json yyjson cjson)
target_link_libraries(proto_bench PRIVATE nlohmann_json::nlohmann_json yyjson cjson)
```

- [ ] **Step 2: Configure and build**

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: configure succeeds (4 new `FetchContent_Declare`/`MakeAvailable` pairs fetch and configure without error), build succeeds, `proto_test` and `proto_bench` still run exactly as before (the 4 libraries are linked but not yet referenced by any source file, so behavior is unchanged — this step only proves the dependency plumbing works before any fixture code is written).

- [ ] **Step 3: Run the existing tests to confirm nothing broke**

Run: `./build/proto_test`
Expected: `All protobuf round-trip tests passed.` and `All SBE round-trip tests passed.` (identical to pre-Task-1 output).

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "Add CMake FetchContent wiring for nlohmann/json, RapidJSON, yyjson, cJSON"
```

---

## Task 2: Shared decoded structs + nlohmann/json fixtures + round-trip test

**Files:**
- Create: `src/json_message_decoded.h`
- Create: `src/json_nlohmann_fixtures.h`
- Create: `src/json_nlohmann_fixtures.cpp`
- Modify: `CMakeLists.txt:49` (add_executable proto_test) and `CMakeLists.txt:62` (add_executable proto_bench)
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `DecodedTextMessage`, `DecodedForwardedItem`, `DecodedMergedForwardMessage` structs (shared by every later library task). `json_nlohmann::EncodeTextMessageJson() -> std::string`, `json_nlohmann::DecodeTextMessageJson(const std::string&) -> DecodedTextMessage`, `json_nlohmann::EncodeMergedForwardMessageJson() -> std::string`, `json_nlohmann::DecodeMergedForwardMessageJson(const std::string&) -> DecodedMergedForwardMessage`.

- [ ] **Step 1: Create `src/json_message_decoded.h`**

```cpp
#ifndef PROTO_TEST_JSON_MESSAGE_DECODED_H_
#define PROTO_TEST_JSON_MESSAGE_DECODED_H_

#include <cstdint>
#include <string>
#include <vector>

struct DecodedTextMessage {
  std::int64_t message_id;
  std::string client_msg_id;
  std::int64_t conversation_id;
  std::string conversation_type;
  std::int64_t sender_id;
  std::int64_t seq;
  std::int64_t client_timestamp_ms;
  std::int64_t server_timestamp_ms;
  std::string status;
  std::int64_t quoted_message_id;
  std::int64_t quoted_sender_id;
  std::string content_preview;
  std::vector<std::int64_t> mentioned_user_ids;
  std::string body;
};

struct DecodedForwardedItem {
  std::int64_t message_id;
  std::int64_t sender_id;
  std::int64_t timestamp_ms;
  std::string body;
};

struct DecodedMergedForwardMessage {
  std::int64_t message_id;
  std::string client_msg_id;
  std::int64_t conversation_id;
  std::string conversation_type;
  std::int64_t sender_id;
  std::int64_t seq;
  std::int64_t client_timestamp_ms;
  std::int64_t server_timestamp_ms;
  std::string status;
  std::string title;
  std::vector<DecodedForwardedItem> items;
};

#endif  // PROTO_TEST_JSON_MESSAGE_DECODED_H_
```

- [ ] **Step 2: Create `src/json_nlohmann_fixtures.h`**

```cpp
#ifndef PROTO_TEST_JSON_NLOHMANN_FIXTURES_H_
#define PROTO_TEST_JSON_NLOHMANN_FIXTURES_H_

#include <string>

#include "json_message_decoded.h"

namespace json_nlohmann {

std::string EncodeTextMessageJson();
DecodedTextMessage DecodeTextMessageJson(const std::string& json_text);
std::string EncodeMergedForwardMessageJson();
DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text);

}  // namespace json_nlohmann

#endif  // PROTO_TEST_JSON_NLOHMANN_FIXTURES_H_
```

- [ ] **Step 3: Add the round-trip test to `src/main.cpp` (RED — function bodies don't exist yet)**

In `src/main.cpp`, add an include after the existing `#include "sbe_message_fixtures.h"` (line 12):

```cpp
#include "json_message_decoded.h"
#include "json_nlohmann_fixtures.h"
```

Add these two functions inside the anonymous `namespace { ... }` block, right before the closing `}  // namespace` (currently line 268, immediately after `TestSbeLargeSweepRoundTrip()`):

```cpp
void TestJsonNlohmannTextRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string json_text = json_nlohmann::EncodeTextMessageJson();
  DecodedTextMessage decoded = json_nlohmann::DecodeTextMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_GROUP");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.quoted_message_id == original.quote().quoted_message_id());
  assert(decoded.quoted_sender_id == original.quote().quoted_sender_id());
  assert(decoded.content_preview == original.quote().content_preview());
  assert(decoded.mentioned_user_ids.size() == 2);
  assert(decoded.mentioned_user_ids[0] == original.mentioned_user_ids(0));
  assert(decoded.mentioned_user_ids[1] == original.mentioned_user_ids(1));
  assert(decoded.body == original.text().body());

  std::cout << "TestJsonNlohmannTextRoundTrip passed, encoded size = " << json_text.size()
            << " bytes\n";
}

void TestJsonNlohmannMergedForwardRoundTrip() {
  ChatMessage original = BuildMergedForwardMessage();

  std::string json_text = json_nlohmann::EncodeMergedForwardMessageJson();
  DecodedMergedForwardMessage decoded = json_nlohmann::DecodeMergedForwardMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_SINGLE");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.title == original.merged_forward().title());
  assert(decoded.items.size() == 2);
  assert(decoded.items[0].message_id == original.merged_forward().items(0).message_id());
  assert(decoded.items[0].sender_id == original.merged_forward().items(0).sender_id());
  assert(decoded.items[0].timestamp_ms == original.merged_forward().items(0).timestamp_ms());
  assert(decoded.items[0].body == original.merged_forward().items(0).text().body());
  assert(decoded.items[1].message_id == original.merged_forward().items(1).message_id());
  assert(decoded.items[1].sender_id == original.merged_forward().items(1).sender_id());
  assert(decoded.items[1].timestamp_ms == original.merged_forward().items(1).timestamp_ms());
  assert(decoded.items[1].body == original.merged_forward().items(1).text().body());

  std::cout << "TestJsonNlohmannMergedForwardRoundTrip passed, encoded size = "
            << json_text.size() << " bytes\n";
}
```

Add these two calls in `main()`, right before `return 0;`:

```cpp
  TestJsonNlohmannTextRoundTrip();
  TestJsonNlohmannMergedForwardRoundTrip();
  std::cout << "All nlohmann/json round-trip tests passed.\n";
```

- [ ] **Step 4: Add the not-yet-existing source file to both `add_executable` lines, run, verify it fails**

In `CMakeLists.txt`, change:
```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp src/sbe_message_fixtures.cpp)
```
to:
```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp src/sbe_message_fixtures.cpp src/json_nlohmann_fixtures.cpp)
```
and change:
```cmake
add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp src/sbe_message_fixtures.cpp)
```
to:
```cmake
add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp src/sbe_message_fixtures.cpp src/json_nlohmann_fixtures.cpp)
```

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: FAIL — CMake/the generator reports `src/json_nlohmann_fixtures.cpp` does not exist (the file isn't created until Step 5).

- [ ] **Step 5: Create `src/json_nlohmann_fixtures.cpp` (GREEN)**

```cpp
#include "json_nlohmann_fixtures.h"

#include <nlohmann/json.hpp>

namespace json_nlohmann {

namespace {
using nlohmann::json;
}

std::string EncodeTextMessageJson() {
  json j;
  j["messageId"] = "1001";
  j["clientMsgId"] = "client-uuid-abc123";
  j["conversationId"] = "555";
  j["conversationType"] = "CONVERSATION_TYPE_GROUP";
  j["senderId"] = "42";
  j["seq"] = "17";
  j["clientTimestampMs"] = "1750000000000";
  j["serverTimestampMs"] = "1750000000050";
  j["status"] = "MESSAGE_STATUS_SENT";
  j["quotedMessageId"] = "998";
  j["quotedSenderId"] = "7";
  j["contentPreview"] = "原消息预览文本";
  j["mentionedUserIds"] = json::array({"7", "9"});
  j["body"] = "Hello, this is a test message.";
  return j.dump();
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  json j = json::parse(json_text);
  DecodedTextMessage msg;
  msg.message_id = std::stoll(j.at("messageId").get<std::string>());
  msg.client_msg_id = j.at("clientMsgId").get<std::string>();
  msg.conversation_id = std::stoll(j.at("conversationId").get<std::string>());
  msg.conversation_type = j.at("conversationType").get<std::string>();
  msg.sender_id = std::stoll(j.at("senderId").get<std::string>());
  msg.seq = std::stoll(j.at("seq").get<std::string>());
  msg.client_timestamp_ms = std::stoll(j.at("clientTimestampMs").get<std::string>());
  msg.server_timestamp_ms = std::stoll(j.at("serverTimestampMs").get<std::string>());
  msg.status = j.at("status").get<std::string>();
  msg.quoted_message_id = std::stoll(j.at("quotedMessageId").get<std::string>());
  msg.quoted_sender_id = std::stoll(j.at("quotedSenderId").get<std::string>());
  msg.content_preview = j.at("contentPreview").get<std::string>();
  for (const auto& id : j.at("mentionedUserIds")) {
    msg.mentioned_user_ids.push_back(std::stoll(id.get<std::string>()));
  }
  msg.body = j.at("body").get<std::string>();
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  json j;
  j["messageId"] = "2002";
  j["clientMsgId"] = "client-uuid-def456";
  j["conversationId"] = "777";
  j["conversationType"] = "CONVERSATION_TYPE_SINGLE";
  j["senderId"] = "42";
  j["seq"] = "18";
  j["clientTimestampMs"] = "1750000001000";
  j["serverTimestampMs"] = "1750000001050";
  j["status"] = "MESSAGE_STATUS_SENT";
  j["title"] = "群聊的聊天记录";

  json items = json::array();
  json item1;
  item1["messageId"] = "101";
  item1["senderId"] = "7";
  item1["timestampMs"] = "1749999999000";
  item1["body"] = "第一条被转发的消息";
  items.push_back(item1);

  json item2;
  item2["messageId"] = "102";
  item2["senderId"] = "9";
  item2["timestampMs"] = "1749999999500";
  item2["body"] = "第二条被转发的消息";
  items.push_back(item2);

  j["items"] = items;
  return j.dump();
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  json j = json::parse(json_text);
  DecodedMergedForwardMessage msg;
  msg.message_id = std::stoll(j.at("messageId").get<std::string>());
  msg.client_msg_id = j.at("clientMsgId").get<std::string>();
  msg.conversation_id = std::stoll(j.at("conversationId").get<std::string>());
  msg.conversation_type = j.at("conversationType").get<std::string>();
  msg.sender_id = std::stoll(j.at("senderId").get<std::string>());
  msg.seq = std::stoll(j.at("seq").get<std::string>());
  msg.client_timestamp_ms = std::stoll(j.at("clientTimestampMs").get<std::string>());
  msg.server_timestamp_ms = std::stoll(j.at("serverTimestampMs").get<std::string>());
  msg.status = j.at("status").get<std::string>();
  msg.title = j.at("title").get<std::string>();
  for (const auto& item : j.at("items")) {
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = std::stoll(item.at("messageId").get<std::string>());
    decoded_item.sender_id = std::stoll(item.at("senderId").get<std::string>());
    decoded_item.timestamp_ms = std::stoll(item.at("timestampMs").get<std::string>());
    decoded_item.body = item.at("body").get<std::string>();
    msg.items.push_back(decoded_item);
  }
  return msg;
}

}  // namespace json_nlohmann
```

- [ ] **Step 6: Build and run, verify pass**

Run: `cmake --build build -j$(nproc) && ./build/proto_test`
Expected: ends with `All nlohmann/json round-trip tests passed.` (after the existing protobuf/SBE passed lines), exit code 0.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/json_message_decoded.h src/json_nlohmann_fixtures.h src/json_nlohmann_fixtures.cpp src/main.cpp
git commit -m "Add nlohmann/json fixtures and round-trip test for Phase 10"
```

---

## Task 3: RapidJSON fixtures + round-trip test

**Files:**
- Create: `src/json_rapidjson_fixtures.h`
- Create: `src/json_rapidjson_fixtures.cpp`
- Modify: `CMakeLists.txt` (both `add_executable` lines, appending to the list Task 2 left)
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `DecodedTextMessage`, `DecodedForwardedItem`, `DecodedMergedForwardMessage` from `json_message_decoded.h` (Task 2).
- Produces: `json_rapidjson::EncodeTextMessageJson() -> std::string`, `json_rapidjson::DecodeTextMessageJson(const std::string&) -> DecodedTextMessage`, `json_rapidjson::EncodeMergedForwardMessageJson() -> std::string`, `json_rapidjson::DecodeMergedForwardMessageJson(const std::string&) -> DecodedMergedForwardMessage`.

- [ ] **Step 1: Create `src/json_rapidjson_fixtures.h`**

```cpp
#ifndef PROTO_TEST_JSON_RAPIDJSON_FIXTURES_H_
#define PROTO_TEST_JSON_RAPIDJSON_FIXTURES_H_

#include <string>

#include "json_message_decoded.h"

namespace json_rapidjson {

std::string EncodeTextMessageJson();
DecodedTextMessage DecodeTextMessageJson(const std::string& json_text);
std::string EncodeMergedForwardMessageJson();
DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text);

}  // namespace json_rapidjson

#endif  // PROTO_TEST_JSON_RAPIDJSON_FIXTURES_H_
```

- [ ] **Step 2: Add the round-trip test to `src/main.cpp` (RED)**

Add this include right after the `#include "json_nlohmann_fixtures.h"` line added in Task 2:

```cpp
#include "json_rapidjson_fixtures.h"
```

Add these two functions in the anonymous namespace, right after `TestJsonNlohmannMergedForwardRoundTrip()`:

```cpp
void TestJsonRapidjsonTextRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string json_text = json_rapidjson::EncodeTextMessageJson();
  DecodedTextMessage decoded = json_rapidjson::DecodeTextMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_GROUP");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.quoted_message_id == original.quote().quoted_message_id());
  assert(decoded.quoted_sender_id == original.quote().quoted_sender_id());
  assert(decoded.content_preview == original.quote().content_preview());
  assert(decoded.mentioned_user_ids.size() == 2);
  assert(decoded.mentioned_user_ids[0] == original.mentioned_user_ids(0));
  assert(decoded.mentioned_user_ids[1] == original.mentioned_user_ids(1));
  assert(decoded.body == original.text().body());

  std::cout << "TestJsonRapidjsonTextRoundTrip passed, encoded size = " << json_text.size()
            << " bytes\n";
}

void TestJsonRapidjsonMergedForwardRoundTrip() {
  ChatMessage original = BuildMergedForwardMessage();

  std::string json_text = json_rapidjson::EncodeMergedForwardMessageJson();
  DecodedMergedForwardMessage decoded = json_rapidjson::DecodeMergedForwardMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_SINGLE");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.title == original.merged_forward().title());
  assert(decoded.items.size() == 2);
  assert(decoded.items[0].message_id == original.merged_forward().items(0).message_id());
  assert(decoded.items[0].sender_id == original.merged_forward().items(0).sender_id());
  assert(decoded.items[0].timestamp_ms == original.merged_forward().items(0).timestamp_ms());
  assert(decoded.items[0].body == original.merged_forward().items(0).text().body());
  assert(decoded.items[1].message_id == original.merged_forward().items(1).message_id());
  assert(decoded.items[1].sender_id == original.merged_forward().items(1).sender_id());
  assert(decoded.items[1].timestamp_ms == original.merged_forward().items(1).timestamp_ms());
  assert(decoded.items[1].body == original.merged_forward().items(1).text().body());

  std::cout << "TestJsonRapidjsonMergedForwardRoundTrip passed, encoded size = "
            << json_text.size() << " bytes\n";
}
```

Add these two calls in `main()`, right after `TestJsonNlohmannMergedForwardRoundTrip();` / before the `"All nlohmann/json round-trip tests passed."` line, or right after that line — exact placement is: directly below `std::cout << "All nlohmann/json round-trip tests passed.\n";`:

```cpp
  TestJsonRapidjsonTextRoundTrip();
  TestJsonRapidjsonMergedForwardRoundTrip();
  std::cout << "All RapidJSON round-trip tests passed.\n";
```

- [ ] **Step 3: Add the not-yet-existing source file, run, verify it fails**

In `CMakeLists.txt`, change:
```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp src/sbe_message_fixtures.cpp src/json_nlohmann_fixtures.cpp)
```
to:
```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp src/sbe_message_fixtures.cpp src/json_nlohmann_fixtures.cpp src/json_rapidjson_fixtures.cpp)
```
and similarly append `src/json_rapidjson_fixtures.cpp` to the `proto_bench` `add_executable` line.

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: FAIL — `src/json_rapidjson_fixtures.cpp` does not exist yet.

- [ ] **Step 4: Create `src/json_rapidjson_fixtures.cpp` (GREEN)**

```cpp
#include "json_rapidjson_fixtures.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdlib>

namespace json_rapidjson {

namespace {

using rapidjson::Document;
using rapidjson::StringBuffer;
using rapidjson::Value;
using rapidjson::Writer;

std::int64_t ParseInt64(const Value& v) { return std::strtoll(v.GetString(), nullptr, 10); }

}  // namespace

std::string EncodeTextMessageJson() {
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("messageId");
  writer.String("1001");
  writer.Key("clientMsgId");
  writer.String("client-uuid-abc123");
  writer.Key("conversationId");
  writer.String("555");
  writer.Key("conversationType");
  writer.String("CONVERSATION_TYPE_GROUP");
  writer.Key("senderId");
  writer.String("42");
  writer.Key("seq");
  writer.String("17");
  writer.Key("clientTimestampMs");
  writer.String("1750000000000");
  writer.Key("serverTimestampMs");
  writer.String("1750000000050");
  writer.Key("status");
  writer.String("MESSAGE_STATUS_SENT");
  writer.Key("quotedMessageId");
  writer.String("998");
  writer.Key("quotedSenderId");
  writer.String("7");
  writer.Key("contentPreview");
  writer.String("原消息预览文本");
  writer.Key("mentionedUserIds");
  writer.StartArray();
  writer.String("7");
  writer.String("9");
  writer.EndArray();
  writer.Key("body");
  writer.String("Hello, this is a test message.");
  writer.EndObject();
  return std::string(buffer.GetString(), buffer.GetSize());
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  Document doc;
  doc.Parse(json_text.c_str(), json_text.size());
  DecodedTextMessage msg;
  msg.message_id = ParseInt64(doc["messageId"]);
  msg.client_msg_id = doc["clientMsgId"].GetString();
  msg.conversation_id = ParseInt64(doc["conversationId"]);
  msg.conversation_type = doc["conversationType"].GetString();
  msg.sender_id = ParseInt64(doc["senderId"]);
  msg.seq = ParseInt64(doc["seq"]);
  msg.client_timestamp_ms = ParseInt64(doc["clientTimestampMs"]);
  msg.server_timestamp_ms = ParseInt64(doc["serverTimestampMs"]);
  msg.status = doc["status"].GetString();
  msg.quoted_message_id = ParseInt64(doc["quotedMessageId"]);
  msg.quoted_sender_id = ParseInt64(doc["quotedSenderId"]);
  msg.content_preview = doc["contentPreview"].GetString();
  for (const auto& id : doc["mentionedUserIds"].GetArray()) {
    msg.mentioned_user_ids.push_back(ParseInt64(id));
  }
  msg.body = doc["body"].GetString();
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("messageId");
  writer.String("2002");
  writer.Key("clientMsgId");
  writer.String("client-uuid-def456");
  writer.Key("conversationId");
  writer.String("777");
  writer.Key("conversationType");
  writer.String("CONVERSATION_TYPE_SINGLE");
  writer.Key("senderId");
  writer.String("42");
  writer.Key("seq");
  writer.String("18");
  writer.Key("clientTimestampMs");
  writer.String("1750000001000");
  writer.Key("serverTimestampMs");
  writer.String("1750000001050");
  writer.Key("status");
  writer.String("MESSAGE_STATUS_SENT");
  writer.Key("title");
  writer.String("群聊的聊天记录");
  writer.Key("items");
  writer.StartArray();
  writer.StartObject();
  writer.Key("messageId");
  writer.String("101");
  writer.Key("senderId");
  writer.String("7");
  writer.Key("timestampMs");
  writer.String("1749999999000");
  writer.Key("body");
  writer.String("第一条被转发的消息");
  writer.EndObject();
  writer.StartObject();
  writer.Key("messageId");
  writer.String("102");
  writer.Key("senderId");
  writer.String("9");
  writer.Key("timestampMs");
  writer.String("1749999999500");
  writer.Key("body");
  writer.String("第二条被转发的消息");
  writer.EndObject();
  writer.EndArray();
  writer.EndObject();
  return std::string(buffer.GetString(), buffer.GetSize());
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  Document doc;
  doc.Parse(json_text.c_str(), json_text.size());
  DecodedMergedForwardMessage msg;
  msg.message_id = ParseInt64(doc["messageId"]);
  msg.client_msg_id = doc["clientMsgId"].GetString();
  msg.conversation_id = ParseInt64(doc["conversationId"]);
  msg.conversation_type = doc["conversationType"].GetString();
  msg.sender_id = ParseInt64(doc["senderId"]);
  msg.seq = ParseInt64(doc["seq"]);
  msg.client_timestamp_ms = ParseInt64(doc["clientTimestampMs"]);
  msg.server_timestamp_ms = ParseInt64(doc["serverTimestampMs"]);
  msg.status = doc["status"].GetString();
  msg.title = doc["title"].GetString();
  for (const auto& item : doc["items"].GetArray()) {
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = ParseInt64(item["messageId"]);
    decoded_item.sender_id = ParseInt64(item["senderId"]);
    decoded_item.timestamp_ms = ParseInt64(item["timestampMs"]);
    decoded_item.body = item["body"].GetString();
    msg.items.push_back(decoded_item);
  }
  return msg;
}

}  // namespace json_rapidjson
```

- [ ] **Step 5: Build and run, verify pass**

Run: `cmake --build build -j$(nproc) && ./build/proto_test`
Expected: ends with `All RapidJSON round-trip tests passed.`, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/json_rapidjson_fixtures.h src/json_rapidjson_fixtures.cpp src/main.cpp
git commit -m "Add RapidJSON fixtures and round-trip test for Phase 10"
```

---

## Task 4: yyjson fixtures + round-trip test

**Files:**
- Create: `src/json_yyjson_fixtures.h`
- Create: `src/json_yyjson_fixtures.cpp`
- Modify: `CMakeLists.txt` (both `add_executable` lines, appending to the list Task 3 left)
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `DecodedTextMessage`, `DecodedForwardedItem`, `DecodedMergedForwardMessage` from `json_message_decoded.h` (Task 2).
- Produces: `json_yyjson::EncodeTextMessageJson() -> std::string`, `json_yyjson::DecodeTextMessageJson(const std::string&) -> DecodedTextMessage`, `json_yyjson::EncodeMergedForwardMessageJson() -> std::string`, `json_yyjson::DecodeMergedForwardMessageJson(const std::string&) -> DecodedMergedForwardMessage`.

- [ ] **Step 1: Create `src/json_yyjson_fixtures.h`**

```cpp
#ifndef PROTO_TEST_JSON_YYJSON_FIXTURES_H_
#define PROTO_TEST_JSON_YYJSON_FIXTURES_H_

#include <string>

#include "json_message_decoded.h"

namespace json_yyjson {

std::string EncodeTextMessageJson();
DecodedTextMessage DecodeTextMessageJson(const std::string& json_text);
std::string EncodeMergedForwardMessageJson();
DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text);

}  // namespace json_yyjson

#endif  // PROTO_TEST_JSON_YYJSON_FIXTURES_H_
```

- [ ] **Step 2: Add the round-trip test to `src/main.cpp` (RED)**

Add this include right after the `#include "json_rapidjson_fixtures.h"` line added in Task 3:

```cpp
#include "json_yyjson_fixtures.h"
```

Add these two functions in the anonymous namespace, right after `TestJsonRapidjsonMergedForwardRoundTrip()`:

```cpp
void TestJsonYyjsonTextRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string json_text = json_yyjson::EncodeTextMessageJson();
  DecodedTextMessage decoded = json_yyjson::DecodeTextMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_GROUP");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.quoted_message_id == original.quote().quoted_message_id());
  assert(decoded.quoted_sender_id == original.quote().quoted_sender_id());
  assert(decoded.content_preview == original.quote().content_preview());
  assert(decoded.mentioned_user_ids.size() == 2);
  assert(decoded.mentioned_user_ids[0] == original.mentioned_user_ids(0));
  assert(decoded.mentioned_user_ids[1] == original.mentioned_user_ids(1));
  assert(decoded.body == original.text().body());

  std::cout << "TestJsonYyjsonTextRoundTrip passed, encoded size = " << json_text.size()
            << " bytes\n";
}

void TestJsonYyjsonMergedForwardRoundTrip() {
  ChatMessage original = BuildMergedForwardMessage();

  std::string json_text = json_yyjson::EncodeMergedForwardMessageJson();
  DecodedMergedForwardMessage decoded = json_yyjson::DecodeMergedForwardMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_SINGLE");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.title == original.merged_forward().title());
  assert(decoded.items.size() == 2);
  assert(decoded.items[0].message_id == original.merged_forward().items(0).message_id());
  assert(decoded.items[0].sender_id == original.merged_forward().items(0).sender_id());
  assert(decoded.items[0].timestamp_ms == original.merged_forward().items(0).timestamp_ms());
  assert(decoded.items[0].body == original.merged_forward().items(0).text().body());
  assert(decoded.items[1].message_id == original.merged_forward().items(1).message_id());
  assert(decoded.items[1].sender_id == original.merged_forward().items(1).sender_id());
  assert(decoded.items[1].timestamp_ms == original.merged_forward().items(1).timestamp_ms());
  assert(decoded.items[1].body == original.merged_forward().items(1).text().body());

  std::cout << "TestJsonYyjsonMergedForwardRoundTrip passed, encoded size = "
            << json_text.size() << " bytes\n";
}
```

Add these calls right after `std::cout << "All RapidJSON round-trip tests passed.\n";`:

```cpp
  TestJsonYyjsonTextRoundTrip();
  TestJsonYyjsonMergedForwardRoundTrip();
  std::cout << "All yyjson round-trip tests passed.\n";
```

- [ ] **Step 3: Add the not-yet-existing source file, run, verify it fails**

Append `src/json_yyjson_fixtures.cpp` to both `add_executable` lines in `CMakeLists.txt` (after `src/json_rapidjson_fixtures.cpp`).

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: FAIL — `src/json_yyjson_fixtures.cpp` does not exist yet.

- [ ] **Step 4: Create `src/json_yyjson_fixtures.cpp` (GREEN)**

```cpp
#include "json_yyjson_fixtures.h"

#include <yyjson.h>

#include <cstdlib>

namespace json_yyjson {

namespace {

std::string WriteAndFree(yyjson_mut_doc* doc) {
  std::size_t len = 0;
  char* json_str = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
  std::string result(json_str, len);
  free(json_str);
  yyjson_mut_doc_free(doc);
  return result;
}

std::int64_t GetInt64(yyjson_val* obj, const char* key) {
  return std::strtoll(yyjson_get_str(yyjson_obj_get(obj, key)), nullptr, 10);
}

std::string GetStr(yyjson_val* obj, const char* key) {
  return yyjson_get_str(yyjson_obj_get(obj, key));
}

}  // namespace

std::string EncodeTextMessageJson() {
  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "messageId", "1001");
  yyjson_mut_obj_add_str(doc, root, "clientMsgId", "client-uuid-abc123");
  yyjson_mut_obj_add_str(doc, root, "conversationId", "555");
  yyjson_mut_obj_add_str(doc, root, "conversationType", "CONVERSATION_TYPE_GROUP");
  yyjson_mut_obj_add_str(doc, root, "senderId", "42");
  yyjson_mut_obj_add_str(doc, root, "seq", "17");
  yyjson_mut_obj_add_str(doc, root, "clientTimestampMs", "1750000000000");
  yyjson_mut_obj_add_str(doc, root, "serverTimestampMs", "1750000000050");
  yyjson_mut_obj_add_str(doc, root, "status", "MESSAGE_STATUS_SENT");
  yyjson_mut_obj_add_str(doc, root, "quotedMessageId", "998");
  yyjson_mut_obj_add_str(doc, root, "quotedSenderId", "7");
  yyjson_mut_obj_add_str(doc, root, "contentPreview", "原消息预览文本");

  yyjson_mut_val* mentions = yyjson_mut_arr(doc);
  yyjson_mut_arr_add_str(doc, mentions, "7");
  yyjson_mut_arr_add_str(doc, mentions, "9");
  yyjson_mut_obj_add_val(doc, root, "mentionedUserIds", mentions);

  yyjson_mut_obj_add_str(doc, root, "body", "Hello, this is a test message.");

  return WriteAndFree(doc);
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  yyjson_doc* doc = yyjson_read(json_text.data(), json_text.size(), 0);
  yyjson_val* root = yyjson_doc_get_root(doc);

  DecodedTextMessage msg;
  msg.message_id = GetInt64(root, "messageId");
  msg.client_msg_id = GetStr(root, "clientMsgId");
  msg.conversation_id = GetInt64(root, "conversationId");
  msg.conversation_type = GetStr(root, "conversationType");
  msg.sender_id = GetInt64(root, "senderId");
  msg.seq = GetInt64(root, "seq");
  msg.client_timestamp_ms = GetInt64(root, "clientTimestampMs");
  msg.server_timestamp_ms = GetInt64(root, "serverTimestampMs");
  msg.status = GetStr(root, "status");
  msg.quoted_message_id = GetInt64(root, "quotedMessageId");
  msg.quoted_sender_id = GetInt64(root, "quotedSenderId");
  msg.content_preview = GetStr(root, "contentPreview");

  yyjson_val* mentions = yyjson_obj_get(root, "mentionedUserIds");
  std::size_t idx, max;
  yyjson_val* id_val;
  yyjson_arr_foreach(mentions, idx, max, id_val) {
    msg.mentioned_user_ids.push_back(std::strtoll(yyjson_get_str(id_val), nullptr, 10));
  }

  msg.body = GetStr(root, "body");

  yyjson_doc_free(doc);
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "messageId", "2002");
  yyjson_mut_obj_add_str(doc, root, "clientMsgId", "client-uuid-def456");
  yyjson_mut_obj_add_str(doc, root, "conversationId", "777");
  yyjson_mut_obj_add_str(doc, root, "conversationType", "CONVERSATION_TYPE_SINGLE");
  yyjson_mut_obj_add_str(doc, root, "senderId", "42");
  yyjson_mut_obj_add_str(doc, root, "seq", "18");
  yyjson_mut_obj_add_str(doc, root, "clientTimestampMs", "1750000001000");
  yyjson_mut_obj_add_str(doc, root, "serverTimestampMs", "1750000001050");
  yyjson_mut_obj_add_str(doc, root, "status", "MESSAGE_STATUS_SENT");
  yyjson_mut_obj_add_str(doc, root, "title", "群聊的聊天记录");

  yyjson_mut_val* items = yyjson_mut_arr(doc);

  yyjson_mut_val* item1 = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, item1, "messageId", "101");
  yyjson_mut_obj_add_str(doc, item1, "senderId", "7");
  yyjson_mut_obj_add_str(doc, item1, "timestampMs", "1749999999000");
  yyjson_mut_obj_add_str(doc, item1, "body", "第一条被转发的消息");
  yyjson_mut_arr_add_val(items, item1);

  yyjson_mut_val* item2 = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, item2, "messageId", "102");
  yyjson_mut_obj_add_str(doc, item2, "senderId", "9");
  yyjson_mut_obj_add_str(doc, item2, "timestampMs", "1749999999500");
  yyjson_mut_obj_add_str(doc, item2, "body", "第二条被转发的消息");
  yyjson_mut_arr_add_val(items, item2);

  yyjson_mut_obj_add_val(doc, root, "items", items);

  return WriteAndFree(doc);
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  yyjson_doc* doc = yyjson_read(json_text.data(), json_text.size(), 0);
  yyjson_val* root = yyjson_doc_get_root(doc);

  DecodedMergedForwardMessage msg;
  msg.message_id = GetInt64(root, "messageId");
  msg.client_msg_id = GetStr(root, "clientMsgId");
  msg.conversation_id = GetInt64(root, "conversationId");
  msg.conversation_type = GetStr(root, "conversationType");
  msg.sender_id = GetInt64(root, "senderId");
  msg.seq = GetInt64(root, "seq");
  msg.client_timestamp_ms = GetInt64(root, "clientTimestampMs");
  msg.server_timestamp_ms = GetInt64(root, "serverTimestampMs");
  msg.status = GetStr(root, "status");
  msg.title = GetStr(root, "title");

  yyjson_val* items = yyjson_obj_get(root, "items");
  std::size_t idx, max;
  yyjson_val* item_val;
  yyjson_arr_foreach(items, idx, max, item_val) {
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = GetInt64(item_val, "messageId");
    decoded_item.sender_id = GetInt64(item_val, "senderId");
    decoded_item.timestamp_ms = GetInt64(item_val, "timestampMs");
    decoded_item.body = GetStr(item_val, "body");
    msg.items.push_back(decoded_item);
  }

  yyjson_doc_free(doc);
  return msg;
}

}  // namespace json_yyjson
```

- [ ] **Step 5: Build and run, verify pass**

Run: `cmake --build build -j$(nproc) && ./build/proto_test`
Expected: ends with `All yyjson round-trip tests passed.`, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/json_yyjson_fixtures.h src/json_yyjson_fixtures.cpp src/main.cpp
git commit -m "Add yyjson fixtures and round-trip test for Phase 10"
```

---

## Task 5: cJSON fixtures + round-trip test

**Files:**
- Create: `src/json_cjson_fixtures.h`
- Create: `src/json_cjson_fixtures.cpp`
- Modify: `CMakeLists.txt` (both `add_executable` lines, appending to the list Task 4 left)
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `DecodedTextMessage`, `DecodedForwardedItem`, `DecodedMergedForwardMessage` from `json_message_decoded.h` (Task 2).
- Produces: `json_cjson::EncodeTextMessageJson() -> std::string`, `json_cjson::DecodeTextMessageJson(const std::string&) -> DecodedTextMessage`, `json_cjson::EncodeMergedForwardMessageJson() -> std::string`, `json_cjson::DecodeMergedForwardMessageJson(const std::string&) -> DecodedMergedForwardMessage`.

- [ ] **Step 1: Create `src/json_cjson_fixtures.h`**

```cpp
#ifndef PROTO_TEST_JSON_CJSON_FIXTURES_H_
#define PROTO_TEST_JSON_CJSON_FIXTURES_H_

#include <string>

#include "json_message_decoded.h"

namespace json_cjson {

std::string EncodeTextMessageJson();
DecodedTextMessage DecodeTextMessageJson(const std::string& json_text);
std::string EncodeMergedForwardMessageJson();
DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text);

}  // namespace json_cjson

#endif  // PROTO_TEST_JSON_CJSON_FIXTURES_H_
```

- [ ] **Step 2: Add the round-trip test to `src/main.cpp` (RED)**

Add this include right after the `#include "json_yyjson_fixtures.h"` line added in Task 4:

```cpp
#include "json_cjson_fixtures.h"
```

Add these two functions in the anonymous namespace, right after `TestJsonYyjsonMergedForwardRoundTrip()`:

```cpp
void TestJsonCjsonTextRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string json_text = json_cjson::EncodeTextMessageJson();
  DecodedTextMessage decoded = json_cjson::DecodeTextMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_GROUP");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.quoted_message_id == original.quote().quoted_message_id());
  assert(decoded.quoted_sender_id == original.quote().quoted_sender_id());
  assert(decoded.content_preview == original.quote().content_preview());
  assert(decoded.mentioned_user_ids.size() == 2);
  assert(decoded.mentioned_user_ids[0] == original.mentioned_user_ids(0));
  assert(decoded.mentioned_user_ids[1] == original.mentioned_user_ids(1));
  assert(decoded.body == original.text().body());

  std::cout << "TestJsonCjsonTextRoundTrip passed, encoded size = " << json_text.size()
            << " bytes\n";
}

void TestJsonCjsonMergedForwardRoundTrip() {
  ChatMessage original = BuildMergedForwardMessage();

  std::string json_text = json_cjson::EncodeMergedForwardMessageJson();
  DecodedMergedForwardMessage decoded = json_cjson::DecodeMergedForwardMessageJson(json_text);

  assert(decoded.message_id == original.message_id());
  assert(decoded.client_msg_id == original.client_msg_id());
  assert(decoded.conversation_id == original.conversation_id());
  assert(decoded.conversation_type == "CONVERSATION_TYPE_SINGLE");
  assert(decoded.sender_id == original.sender_id());
  assert(decoded.seq == original.seq());
  assert(decoded.client_timestamp_ms == original.client_timestamp_ms());
  assert(decoded.server_timestamp_ms == original.server_timestamp_ms());
  assert(decoded.status == "MESSAGE_STATUS_SENT");
  assert(decoded.title == original.merged_forward().title());
  assert(decoded.items.size() == 2);
  assert(decoded.items[0].message_id == original.merged_forward().items(0).message_id());
  assert(decoded.items[0].sender_id == original.merged_forward().items(0).sender_id());
  assert(decoded.items[0].timestamp_ms == original.merged_forward().items(0).timestamp_ms());
  assert(decoded.items[0].body == original.merged_forward().items(0).text().body());
  assert(decoded.items[1].message_id == original.merged_forward().items(1).message_id());
  assert(decoded.items[1].sender_id == original.merged_forward().items(1).sender_id());
  assert(decoded.items[1].timestamp_ms == original.merged_forward().items(1).timestamp_ms());
  assert(decoded.items[1].body == original.merged_forward().items(1).text().body());

  std::cout << "TestJsonCjsonMergedForwardRoundTrip passed, encoded size = "
            << json_text.size() << " bytes\n";
}
```

Add these calls right after `std::cout << "All yyjson round-trip tests passed.\n";`, then change `return 0;` is unaffected (still the last statement):

```cpp
  TestJsonCjsonTextRoundTrip();
  TestJsonCjsonMergedForwardRoundTrip();
  std::cout << "All cJSON round-trip tests passed.\n";
```

- [ ] **Step 3: Add the not-yet-existing source file, run, verify it fails**

Append `src/json_cjson_fixtures.cpp` to both `add_executable` lines in `CMakeLists.txt` (after `src/json_yyjson_fixtures.cpp`).

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: FAIL — `src/json_cjson_fixtures.cpp` does not exist yet.

- [ ] **Step 4: Create `src/json_cjson_fixtures.cpp` (GREEN)**

```cpp
#include "json_cjson_fixtures.h"

#include <cJSON.h>

#include <cstdlib>

namespace json_cjson {

namespace {

std::string PrintAndDelete(cJSON* root) {
  char* text = cJSON_PrintUnformatted(root);
  std::string result(text);
  free(text);
  cJSON_Delete(root);
  return result;
}

std::int64_t GetInt64(const cJSON* obj, const char* key) {
  return std::strtoll(cJSON_GetObjectItem(obj, key)->valuestring, nullptr, 10);
}

std::string GetStr(const cJSON* obj, const char* key) {
  return cJSON_GetObjectItem(obj, key)->valuestring;
}

}  // namespace

std::string EncodeTextMessageJson() {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "messageId", "1001");
  cJSON_AddStringToObject(root, "clientMsgId", "client-uuid-abc123");
  cJSON_AddStringToObject(root, "conversationId", "555");
  cJSON_AddStringToObject(root, "conversationType", "CONVERSATION_TYPE_GROUP");
  cJSON_AddStringToObject(root, "senderId", "42");
  cJSON_AddStringToObject(root, "seq", "17");
  cJSON_AddStringToObject(root, "clientTimestampMs", "1750000000000");
  cJSON_AddStringToObject(root, "serverTimestampMs", "1750000000050");
  cJSON_AddStringToObject(root, "status", "MESSAGE_STATUS_SENT");
  cJSON_AddStringToObject(root, "quotedMessageId", "998");
  cJSON_AddStringToObject(root, "quotedSenderId", "7");
  cJSON_AddStringToObject(root, "contentPreview", "原消息预览文本");

  cJSON* mentions = cJSON_CreateArray();
  cJSON_AddItemToArray(mentions, cJSON_CreateString("7"));
  cJSON_AddItemToArray(mentions, cJSON_CreateString("9"));
  cJSON_AddItemToObject(root, "mentionedUserIds", mentions);

  cJSON_AddStringToObject(root, "body", "Hello, this is a test message.");

  return PrintAndDelete(root);
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  cJSON* root = cJSON_Parse(json_text.c_str());

  DecodedTextMessage msg;
  msg.message_id = GetInt64(root, "messageId");
  msg.client_msg_id = GetStr(root, "clientMsgId");
  msg.conversation_id = GetInt64(root, "conversationId");
  msg.conversation_type = GetStr(root, "conversationType");
  msg.sender_id = GetInt64(root, "senderId");
  msg.seq = GetInt64(root, "seq");
  msg.client_timestamp_ms = GetInt64(root, "clientTimestampMs");
  msg.server_timestamp_ms = GetInt64(root, "serverTimestampMs");
  msg.status = GetStr(root, "status");
  msg.quoted_message_id = GetInt64(root, "quotedMessageId");
  msg.quoted_sender_id = GetInt64(root, "quotedSenderId");
  msg.content_preview = GetStr(root, "contentPreview");

  cJSON* mentions = cJSON_GetObjectItem(root, "mentionedUserIds");
  const int mention_count = cJSON_GetArraySize(mentions);
  for (int i = 0; i < mention_count; ++i) {
    msg.mentioned_user_ids.push_back(
        std::strtoll(cJSON_GetArrayItem(mentions, i)->valuestring, nullptr, 10));
  }

  msg.body = GetStr(root, "body");

  cJSON_Delete(root);
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "messageId", "2002");
  cJSON_AddStringToObject(root, "clientMsgId", "client-uuid-def456");
  cJSON_AddStringToObject(root, "conversationId", "777");
  cJSON_AddStringToObject(root, "conversationType", "CONVERSATION_TYPE_SINGLE");
  cJSON_AddStringToObject(root, "senderId", "42");
  cJSON_AddStringToObject(root, "seq", "18");
  cJSON_AddStringToObject(root, "clientTimestampMs", "1750000001000");
  cJSON_AddStringToObject(root, "serverTimestampMs", "1750000001050");
  cJSON_AddStringToObject(root, "status", "MESSAGE_STATUS_SENT");
  cJSON_AddStringToObject(root, "title", "群聊的聊天记录");

  cJSON* items = cJSON_CreateArray();

  cJSON* item1 = cJSON_CreateObject();
  cJSON_AddStringToObject(item1, "messageId", "101");
  cJSON_AddStringToObject(item1, "senderId", "7");
  cJSON_AddStringToObject(item1, "timestampMs", "1749999999000");
  cJSON_AddStringToObject(item1, "body", "第一条被转发的消息");
  cJSON_AddItemToArray(items, item1);

  cJSON* item2 = cJSON_CreateObject();
  cJSON_AddStringToObject(item2, "messageId", "102");
  cJSON_AddStringToObject(item2, "senderId", "9");
  cJSON_AddStringToObject(item2, "timestampMs", "1749999999500");
  cJSON_AddStringToObject(item2, "body", "第二条被转发的消息");
  cJSON_AddItemToArray(items, item2);

  cJSON_AddItemToObject(root, "items", items);

  return PrintAndDelete(root);
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  cJSON* root = cJSON_Parse(json_text.c_str());

  DecodedMergedForwardMessage msg;
  msg.message_id = GetInt64(root, "messageId");
  msg.client_msg_id = GetStr(root, "clientMsgId");
  msg.conversation_id = GetInt64(root, "conversationId");
  msg.conversation_type = GetStr(root, "conversationType");
  msg.sender_id = GetInt64(root, "senderId");
  msg.seq = GetInt64(root, "seq");
  msg.client_timestamp_ms = GetInt64(root, "clientTimestampMs");
  msg.server_timestamp_ms = GetInt64(root, "serverTimestampMs");
  msg.status = GetStr(root, "status");
  msg.title = GetStr(root, "title");

  cJSON* items = cJSON_GetObjectItem(root, "items");
  const int item_count = cJSON_GetArraySize(items);
  for (int i = 0; i < item_count; ++i) {
    cJSON* item = cJSON_GetArrayItem(items, i);
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = GetInt64(item, "messageId");
    decoded_item.sender_id = GetInt64(item, "senderId");
    decoded_item.timestamp_ms = GetInt64(item, "timestampMs");
    decoded_item.body = GetStr(item, "body");
    msg.items.push_back(decoded_item);
  }

  cJSON_Delete(root);
  return msg;
}

}  // namespace json_cjson
```

- [ ] **Step 5: Build and run, verify pass**

Run: `cmake --build build -j$(nproc) && ./build/proto_test`
Expected: ends with `All cJSON round-trip tests passed.`, exit code 0. All four libraries' round-trip suites (nlohmann/RapidJSON/yyjson/cJSON) plus the pre-existing protobuf/SBE suites should now print in sequence with no assertion failures.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/json_cjson_fixtures.h src/json_cjson_fixtures.cpp src/main.cpp
git commit -m "Add cJSON fixtures and round-trip test for Phase 10"
```

---

## Task 6: Add 16 benchmarks to `bench.cpp`

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: all 4 libraries' `Encode{Text,MergedForward}MessageJson`/`Decode{Text,MergedForward}MessageJson` functions (Tasks 2-5) and `DecodedTextMessage`/`DecodedMergedForwardMessage` (Task 2).

- [ ] **Step 1: Add includes**

In `src/bench.cpp`, add after the existing `#include "sbe_message_fixtures.h"` line:

```cpp
#include "json_cjson_fixtures.h"
#include "json_message_decoded.h"
#include "json_nlohmann_fixtures.h"
#include "json_rapidjson_fixtures.h"
#include "json_yyjson_fixtures.h"
```

- [ ] **Step 2: Add the 16 benchmark functions**

Add these inside the existing anonymous `namespace { ... }` block in `bench.cpp`, right before the closing `}  // namespace` (just before `BENCHMARK_MAIN();`):

```cpp
void BM_EncodeTextJsonNlohmann(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_nlohmann::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonNlohmann);

void BM_DecodeTextJsonNlohmann(benchmark::State& state) {
  const std::string json_text = json_nlohmann::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_nlohmann::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonNlohmann);

void BM_EncodeMergedForwardJsonNlohmann(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_nlohmann::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonNlohmann);

void BM_DecodeMergedForwardJsonNlohmann(benchmark::State& state) {
  const std::string json_text = json_nlohmann::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_nlohmann::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonNlohmann);

void BM_EncodeTextJsonRapidjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_rapidjson::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonRapidjson);

void BM_DecodeTextJsonRapidjson(benchmark::State& state) {
  const std::string json_text = json_rapidjson::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_rapidjson::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonRapidjson);

void BM_EncodeMergedForwardJsonRapidjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_rapidjson::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonRapidjson);

void BM_DecodeMergedForwardJsonRapidjson(benchmark::State& state) {
  const std::string json_text = json_rapidjson::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_rapidjson::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonRapidjson);

void BM_EncodeTextJsonYyjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_yyjson::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonYyjson);

void BM_DecodeTextJsonYyjson(benchmark::State& state) {
  const std::string json_text = json_yyjson::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_yyjson::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonYyjson);

void BM_EncodeMergedForwardJsonYyjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_yyjson::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonYyjson);

void BM_DecodeMergedForwardJsonYyjson(benchmark::State& state) {
  const std::string json_text = json_yyjson::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_yyjson::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonYyjson);

void BM_EncodeTextJsonCJson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_cjson::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonCJson);

void BM_DecodeTextJsonCJson(benchmark::State& state) {
  const std::string json_text = json_cjson::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_cjson::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonCJson);

void BM_EncodeMergedForwardJsonCJson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_cjson::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonCJson);

void BM_DecodeMergedForwardJsonCJson(benchmark::State& state) {
  const std::string json_text = json_cjson::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_cjson::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonCJson);
```

- [ ] **Step 3: Build and smoke-test**

Run: `cmake --build build -j$(nproc) && ./build/proto_bench --benchmark_filter="Json"`
Expected: exactly 16 benchmark result lines, one per function above, each with a `bytes` counter. Sanity check: for a given shape (Text or MergedForward), the `bytes` value should be within the same order of magnitude across all 4 libraries (same field set, same values — only key order/escaping differs). If any library's `bytes` is wildly different (e.g. 10x smaller), stop and re-check that fixture's encode function for a missing field before proceeding — do not report a benchmark whose payload silently differs from the others' field set.

- [ ] **Step 4: Run the full test suite once more for safety**

Run: `./build/proto_test`
Expected: all round-trip suites (protobuf, SBE, nlohmann, RapidJSON, yyjson, cJSON) pass.

- [ ] **Step 5: Commit**

```bash
git add src/bench.cpp
git commit -m "Add Phase 10 JSON encode/decode benchmarks for 4 libraries"
```

---

## Task 7: Run benchmarks, save results, write the analysis doc

**Files:**
- Create: `results/phase10-2026-06-20.json`
- Create: `docs/benchmarks/phase10-json-shootout-analysis.md`

**Interfaces:**
- Consumes: the 16 benchmarks from Task 6, run via the real `proto_bench` binary — this task's "test" is the benchmark run itself, not a unit test.

- [ ] **Step 1: Run the benchmark suite and save JSON output**

Run:
```bash
./build/proto_bench --benchmark_filter="Json" --benchmark_format=json --benchmark_out=results/phase10-2026-06-20.json
```
Expected: command exits 0, `results/phase10-2026-06-20.json` contains a `benchmarks` array with 16 entries, each with `name`, `real_time`, `cpu_time`, `time_unit`, and a `bytes` counter.

- [ ] **Step 2: Extract the 16 numbers into a table**

Read `results/phase10-2026-06-20.json` and, for each of the 16 `name` entries, record `real_time` (the wall-clock ns/op Google Benchmark reports) and `bytes`. Build two tables (one per shape), one row per library, columns: Encode time (ns), Decode time (ns), Encode+Decode sum (ns), bytes.

- [ ] **Step 3: Write `docs/benchmarks/phase10-json-shootout-analysis.md`**

Use this skeleton, filling every `<...>` placeholder with the real number extracted in Step 2 (do not leave any placeholder unfilled — every `<...>` below must be replaced with an actual measured value before this file is considered done):

```markdown
# Phase 10 — JSON Library Shootout Analysis

Date: 2026-06-20

## Method

4 libraries (nlohmann/json, RapidJSON, yyjson, cJSON) hand-encode/decode the same
two logical messages (`BuildTextMessage`, `BuildMergedForwardMessage`) into JSON,
using the field mapping defined in
`docs/superpowers/specs/2026-06-20-benchmark-phase10-json-shootout-design.md`
(camelCase keys, int64/enum as JSON strings, compact output). simdjson was
excluded — it has no symmetric high-performance writer, so it cannot be ranked
on the same "encode+decode total time" metric as the other 4.

Ranking metric: sum of encode + decode `real_time` (ns/op), summed across both
shapes (Text + MergedForward). Lowest total wins.

## Results — TextMessage shape

| Library | Encode (ns) | Decode (ns) | Encode+Decode (ns) | bytes |
| --- | --- | --- | --- | --- |
| nlohmann/json | <real_time of BM_EncodeTextJsonNlohmann> | <real_time of BM_DecodeTextJsonNlohmann> | <sum> | <bytes> |
| RapidJSON | <real_time of BM_EncodeTextJsonRapidjson> | <real_time of BM_DecodeTextJsonRapidjson> | <sum> | <bytes> |
| yyjson | <real_time of BM_EncodeTextJsonYyjson> | <real_time of BM_DecodeTextJsonYyjson> | <sum> | <bytes> |
| cJSON | <real_time of BM_EncodeTextJsonCJson> | <real_time of BM_DecodeTextJsonCJson> | <sum> | <bytes> |

## Results — MergedForwardMessage shape

| Library | Encode (ns) | Decode (ns) | Encode+Decode (ns) | bytes |
| --- | --- | --- | --- | --- |
| nlohmann/json | <real_time of BM_EncodeMergedForwardJsonNlohmann> | <real_time of BM_DecodeMergedForwardJsonNlohmann> | <sum> | <bytes> |
| RapidJSON | <real_time of BM_EncodeMergedForwardJsonRapidjson> | <real_time of BM_DecodeMergedForwardJsonRapidjson> | <sum> | <bytes> |
| yyjson | <real_time of BM_EncodeMergedForwardJsonYyjson> | <real_time of BM_DecodeMergedForwardJsonYyjson> | <sum> | <bytes> |
| cJSON | <real_time of BM_EncodeMergedForwardJsonCJson> | <real_time of BM_DecodeMergedForwardJsonCJson> | <sum> | <bytes> |

## Ranking (both shapes summed)

| Rank | Library | Total Encode+Decode (ns, both shapes) |
| --- | --- | --- |
| 1 | <winner> | <total> |
| 2 | <...> | <total> |
| 3 | <...> | <total> |
| 4 | <...> | <total> |

**Winner: `<library name>`.** <one or two sentences explaining the gap to 2nd
place and any counter-intuitive result observed — report the real numbers
honestly even if they contradict the libraries' marketing claims, consistent
with this project's existing phase docs.>

## Carried into Phase 11

`<winner>` is the JSON library used for the Phase 11 vs-Protobuf PK (encode/decode
time + size, aligned with Phase 1/9's scope).
```

- [ ] **Step 4: Commit**

```bash
git add results/phase10-2026-06-20.json docs/benchmarks/phase10-json-shootout-analysis.md
git commit -m "Run Phase 10 JSON shootout benchmarks and record analysis"
```

---

## Task 8: Fold Phase 10 into the final report

**Files:**
- Modify: rename `docs/benchmarks/final-report-phases-0-9.md` → `docs/benchmarks/final-report-phases-0-10.md`
- Modify: rename `docs/benchmarks/final-report-phases-0-9.zh-CN.md` → `docs/benchmarks/final-report-phases-0-10.zh-CN.md`

**Interfaces:**
- Consumes: the ranking table and winner from Task 7's `docs/benchmarks/phase10-json-shootout-analysis.md`.

- [ ] **Step 1: Rename both report files**

```bash
git mv docs/benchmarks/final-report-phases-0-9.md docs/benchmarks/final-report-phases-0-10.md
git mv docs/benchmarks/final-report-phases-0-9.zh-CN.md docs/benchmarks/final-report-phases-0-10.zh-CN.md
```

- [ ] **Step 2: Add a Phase 10 section to `final-report-phases-0-10.md`**

Find the existing per-phase index/table (the section listing Phase 1 through Phase 9 with links to their spec/plan/results/analysis files) and add a Phase 10 row following the exact same column layout used by the Phase 9 row, pointing at:
- Spec: `docs/superpowers/specs/2026-06-20-benchmark-phase10-json-shootout-design.md`
- Plan: `docs/superpowers/plans/2026-06-20-benchmark-phase10-json-shootout.md`
- Results: `results/phase10-2026-06-20.json`
- Analysis: `docs/benchmarks/phase10-json-shootout-analysis.md`

In the "Cross-cutting takeaways" section, add one bullet stating the Task 7 winner and the real margin over the runner-up (use the actual numbers from `phase10-json-shootout-analysis.md`, not a placeholder).

- [ ] **Step 3: Mirror the same two edits into `final-report-phases-0-10.zh-CN.md`**

Add the equivalent Phase 10 row and cross-cutting takeaway bullet, translated to Chinese, matching the style of the existing Phase 9 entries in that file.

- [ ] **Step 4: Commit**

```bash
git add docs/benchmarks/final-report-phases-0-10.md docs/benchmarks/final-report-phases-0-10.zh-CN.md
git commit -m "Fold Phase 10 JSON shootout into the final report"
```

---

## Self-Review

- **Spec coverage:** Task 1 covers spec §2 (CMake). Tasks 2-5 cover spec §1 (field tables — encoded literally into each fixture), §3 (decoded structs), §4 (per-library fixtures). Task 6 covers spec §5 (16 benchmarks). Task 7/8 cover spec's "产出物与收尾" section (results JSON, analysis doc, final-report fold-in). The spec's "不包含" exclusions (no scale sweep, no concurrency, no alloc counting, no malformed input, no simdjson) are honored by omission — no task introduces them.
- **Placeholder scan:** All code blocks are complete, verified against the real library headers (yyjson 0.12.0, cJSON v1.7.19) fetched from GitHub during planning — no invented API names. The only `<...>` placeholders are in Task 7's analysis-doc skeleton, which are inherently runtime-dependent (benchmark numbers that don't exist until the suite runs) and are explicitly called out as "must be filled before considered done," not left vague.
- **Type consistency:** `DecodedTextMessage`/`DecodedForwardedItem`/`DecodedMergedForwardMessage` (defined once in Task 2's `json_message_decoded.h`) are used with identical field names across Tasks 2-6. Function names `Encode{Text,MergedForward}MessageJson`/`Decode{Text,MergedForward}MessageJson` are consistent across all 4 namespaces and across the benchmark names in Task 6.

