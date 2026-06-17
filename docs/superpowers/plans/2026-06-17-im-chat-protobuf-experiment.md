# IM Chat Protobuf Serialization Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal C++20/CMake project that defines a production-style IM chat message protobuf schema and proves serialization/deserialization round-trips correctly, including the oneof content, quote snapshot, and merged-forward nesting.

**Architecture:** A single CMake project fetches and builds protobuf from source via `FetchContent` (no system package), compiles `proto/chat.proto` into C++ bindings via protobuf's `protobuf_generate()` CMake function, and links a single executable (`src/main.cpp`) that builds `ChatMessage` instances, serializes them to bytes, parses them back, and asserts the round-trip matches.

**Tech Stack:** C++20, CMake >= 3.16, protobuf v35.1 (fetched from `https://github.com/protocolbuffers/protobuf.git` via `FetchContent`, which also auto-fetches its Abseil dependency).

---

## Reference: full chat.proto schema

This is the complete, final schema used across all tasks below (also recorded in `docs/superpowers/specs/2026-06-17-im-chat-protobuf-design.md`):

```protobuf
syntax = "proto3";

package im.chat.v1;

// ---- Enums ----

enum ConversationType {
  CONVERSATION_TYPE_UNSPECIFIED = 0;
  CONVERSATION_TYPE_SINGLE = 1;
  CONVERSATION_TYPE_GROUP = 2;
  CONVERSATION_TYPE_CHANNEL = 3;
}

enum MessageStatus {
  MESSAGE_STATUS_UNSPECIFIED = 0;
  MESSAGE_STATUS_SENDING = 1;
  MESSAGE_STATUS_SENT = 2;
  MESSAGE_STATUS_DELIVERED = 3;
  MESSAGE_STATUS_READ = 4;
  MESSAGE_STATUS_FAILED = 5;
  MESSAGE_STATUS_RECALLED = 6;
}

enum SystemEventType {
  SYSTEM_EVENT_TYPE_UNSPECIFIED = 0;
  SYSTEM_EVENT_TYPE_GROUP_JOIN = 1;
  SYSTEM_EVENT_TYPE_GROUP_LEAVE = 2;
  SYSTEM_EVENT_TYPE_GROUP_KICKED = 3;
  SYSTEM_EVENT_TYPE_GROUP_RENAMED = 4;
}

// ---- Content payloads ----

message TextContent {
  string body = 1;
}

message ImageContent {
  string url = 1;
  string thumbnail_url = 2;
  int32 width = 3;
  int32 height = 4;
  int64 size_bytes = 5;
  string md5 = 6;
}

message AudioContent {
  string url = 1;
  int32 duration_ms = 2;
  int64 size_bytes = 3;
}

message VideoContent {
  string url = 1;
  string thumbnail_url = 2;
  int32 duration_ms = 3;
  int32 width = 4;
  int32 height = 5;
  int64 size_bytes = 6;
}

message FileContent {
  string url = 1;
  string file_name = 2;
  int64 size_bytes = 3;
  string md5 = 4;
}

message RecallNotice {
  int64 original_message_id = 1;
}

message SystemEvent {
  SystemEventType event_type = 1;
  repeated int64 target_user_ids = 2;
  string text = 3;
}

// ---- Quote ----

message QuoteInfo {
  int64 quoted_message_id = 1;
  int64 quoted_sender_id = 2;
  string content_preview = 3;
}

// ---- Forward ----

message ForwardInfo {
  int64 original_message_id = 1;
  int64 original_sender_id = 2;
  int64 original_conversation_id = 3;
  int32 forward_depth = 4;
}

message ForwardedItem {
  int64 message_id = 1;
  int64 sender_id = 2;
  int64 timestamp_ms = 3;
  oneof content {
    TextContent text = 10;
    ImageContent image = 11;
    AudioContent audio = 12;
    VideoContent video = 13;
    FileContent file = 14;
  }
}

message MergedForwardContent {
  string title = 1;
  repeated ForwardedItem items = 2;
}

// ---- Envelope ----

message ChatMessage {
  int64 message_id = 1;
  string client_msg_id = 2;
  int64 conversation_id = 3;
  ConversationType conversation_type = 4;
  int64 sender_id = 5;
  int64 seq = 6;
  int64 client_timestamp_ms = 7;
  int64 server_timestamp_ms = 8;
  MessageStatus status = 9;

  QuoteInfo quote = 10;
  repeated int64 mentioned_user_ids = 11;
  bool edited = 12;
  ForwardInfo forward_info = 13;

  oneof content {
    TextContent text = 20;
    ImageContent image = 21;
    AudioContent audio = 22;
    VideoContent video = 23;
    FileContent file = 24;
    RecallNotice recall = 25;
    SystemEvent system_event = 26;
    MergedForwardContent merged_forward = 27;
  }

  map<string, string> extra = 30;
}
```

---

### Task 1: Write the protobuf schema

**Files:**
- Create: `proto/chat.proto`

- [ ] **Step 1: Create `proto/chat.proto`**

Write the exact schema from the "Reference: full chat.proto schema" section above into `proto/chat.proto`.

- [ ] **Step 2: Sanity-check the file has no syntax typos by eye**

Confirm the file starts with `syntax = "proto3";` and `package im.chat.v1;`, and that every message/enum brace is balanced (this will be definitively checked by `protoc` in Task 2 — this step is just a quick read-through before moving on).

- [ ] **Step 3: Commit**

```bash
git add proto/chat.proto
git commit -m "Add chat.proto schema for IM message experiment"
```

---

### Task 2: CMake project skeleton that fetches protobuf and compiles the schema

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp` (placeholder, just proves the generated headers compile and link)

- [ ] **Step 1: Write `CMakeLists.txt`**

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

add_executable(proto_test src/main.cpp)
target_link_libraries(proto_test PRIVATE protobuf::libprotobuf)

protobuf_generate(
  TARGET proto_test
  PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/proto/chat.proto
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_include_directories(proto_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
```

- [ ] **Step 2: Write a placeholder `src/main.cpp`**

This only proves the generated `chat.pb.h` compiles and links against `protobuf::libprotobuf` — the real round-trip tests come in Tasks 3 and 4.

```cpp
#include <iostream>

#include "chat.pb.h"

int main() {
  im::chat::v1::ChatMessage msg;
  msg.set_message_id(1);
  std::cout << "ChatMessage compiled OK, message_id=" << msg.message_id() << "\n";
  return 0;
}
```

- [ ] **Step 3: Configure the build**

Run: `cmake -S . -B build`

Expected: configure succeeds and ends with `-- Generating done` / `-- Build files have been written to: .../build`. This step fetches protobuf's source via git — it needs network access.

If `GIT_TAG v35.1` fails to fetch (e.g. tag renamed upstream), run `git ls-remote --tags https://github.com/protocolbuffers/protobuf | grep -oE 'refs/tags/v[0-9]+\.[0-9]+(\.[0-9]+)?$' | sort -V | tail -5` to find the current latest stable tag and update `GIT_TAG` in `CMakeLists.txt` accordingly.

- [ ] **Step 4: Build**

Run: `cmake --build build -j`

Expected: this compiles Abseil, protobuf (including `protoc`), generates `chat.pb.h`/`chat.pb.cc` from `proto/chat.proto`, and links `proto_test`. First build takes several minutes since protobuf compiles from source; expect `[100%] Built target proto_test` at the end with no errors.

- [ ] **Step 5: Run and verify**

Run: `./build/proto_test`

Expected output: `ChatMessage compiled OK, message_id=1`

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/main.cpp
git commit -m "Add CMake project that fetches protobuf and compiles chat.proto"
```

---

### Task 3: Round-trip test for a single text ChatMessage

**Files:**
- Modify: `src/main.cpp` (replace placeholder body entirely)

- [ ] **Step 1: Replace `src/main.cpp` with the text-message round-trip test**

```cpp
#include <cassert>
#include <iostream>
#include <string>

#include "chat.pb.h"

using im::chat::v1::ChatMessage;
using im::chat::v1::QuoteInfo;

namespace {

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

void TestTextMessageRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string bytes;
  bool serialized = original.SerializeToString(&bytes);
  assert(serialized);

  ChatMessage parsed;
  bool parsed_ok = parsed.ParseFromString(bytes);
  assert(parsed_ok);

  assert(parsed.message_id() == original.message_id());
  assert(parsed.client_msg_id() == original.client_msg_id());
  assert(parsed.conversation_id() == original.conversation_id());
  assert(parsed.conversation_type() == original.conversation_type());
  assert(parsed.sender_id() == original.sender_id());
  assert(parsed.seq() == original.seq());
  assert(parsed.status() == original.status());
  assert(parsed.quote().quoted_message_id() == original.quote().quoted_message_id());
  assert(parsed.quote().content_preview() == original.quote().content_preview());
  assert(parsed.mentioned_user_ids_size() == original.mentioned_user_ids_size());
  assert(parsed.content_case() == ChatMessage::kText);
  assert(parsed.text().body() == original.text().body());

  std::cout << "TestTextMessageRoundTrip passed, serialized size = " << bytes.size()
            << " bytes\n";
}

}  // namespace

int main() {
  TestTextMessageRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";
  return 0;
}
```

- [ ] **Step 2: Rebuild**

Run: `cmake --build build -j`

Expected: `[100%] Built target proto_test` with no errors (this is an incremental build now, should take only a few seconds).

- [ ] **Step 3: Run and verify**

Run: `./build/proto_test`

Expected output:
```
TestTextMessageRoundTrip passed, serialized size = <some number> bytes
All protobuf round-trip tests passed.
```

If any `assert` fails, the program aborts with a `Assertion '...' failed` message and a non-zero exit code — that signals the round-trip is broken and must be fixed before continuing.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Add text ChatMessage serialization round-trip test"
```

---

### Task 4: Round-trip test for a merged-forward ChatMessage

**Files:**
- Modify: `src/main.cpp` (add a second test, keep the first)

- [ ] **Step 1: Add `BuildMergedForwardMessage` and `TestMergedForwardRoundTrip`, and call it from `main`**

Replace the entire contents of `src/main.cpp` with:

```cpp
#include <cassert>
#include <iostream>
#include <string>

#include "chat.pb.h"

using im::chat::v1::ChatMessage;
using im::chat::v1::ForwardedItem;
using im::chat::v1::MergedForwardContent;
using im::chat::v1::QuoteInfo;

namespace {

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

void TestTextMessageRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string bytes;
  bool serialized = original.SerializeToString(&bytes);
  assert(serialized);

  ChatMessage parsed;
  bool parsed_ok = parsed.ParseFromString(bytes);
  assert(parsed_ok);

  assert(parsed.message_id() == original.message_id());
  assert(parsed.client_msg_id() == original.client_msg_id());
  assert(parsed.conversation_id() == original.conversation_id());
  assert(parsed.conversation_type() == original.conversation_type());
  assert(parsed.sender_id() == original.sender_id());
  assert(parsed.seq() == original.seq());
  assert(parsed.status() == original.status());
  assert(parsed.quote().quoted_message_id() == original.quote().quoted_message_id());
  assert(parsed.quote().content_preview() == original.quote().content_preview());
  assert(parsed.mentioned_user_ids_size() == original.mentioned_user_ids_size());
  assert(parsed.content_case() == ChatMessage::kText);
  assert(parsed.text().body() == original.text().body());

  std::cout << "TestTextMessageRoundTrip passed, serialized size = " << bytes.size()
            << " bytes\n";
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

void TestMergedForwardRoundTrip() {
  ChatMessage original = BuildMergedForwardMessage();

  std::string bytes;
  bool serialized = original.SerializeToString(&bytes);
  assert(serialized);

  ChatMessage parsed;
  bool parsed_ok = parsed.ParseFromString(bytes);
  assert(parsed_ok);

  assert(parsed.content_case() == ChatMessage::kMergedForward);
  assert(parsed.merged_forward().title() == original.merged_forward().title());
  assert(parsed.merged_forward().items_size() == 2);
  assert(parsed.merged_forward().items(0).message_id() == 101);
  assert(parsed.merged_forward().items(0).text().body() ==
         original.merged_forward().items(0).text().body());
  assert(parsed.merged_forward().items(1).sender_id() == 9);

  std::cout << "TestMergedForwardRoundTrip passed, serialized size = " << bytes.size()
            << " bytes\n";
}

}  // namespace

int main() {
  TestTextMessageRoundTrip();
  TestMergedForwardRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";
  return 0;
}
```

- [ ] **Step 2: Rebuild**

Run: `cmake --build build -j`

Expected: `[100%] Built target proto_test` with no errors.

- [ ] **Step 3: Run and verify**

Run: `./build/proto_test`

Expected output:
```
TestTextMessageRoundTrip passed, serialized size = <N> bytes
TestMergedForwardRoundTrip passed, serialized size = <M> bytes
All protobuf round-trip tests passed.
```

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Add merged-forward ChatMessage serialization round-trip test"
```

---

### Task 5: Add a .gitignore for the build directory

**Files:**
- Create: `.gitignore`

- [ ] **Step 1: Create `.gitignore`**

```
build/
```

- [ ] **Step 2: Verify the build directory is now ignored**

Run: `git status --short`

Expected: `build/` does not appear in the output (only `.gitignore` shows as untracked, if not yet staged).

- [ ] **Step 3: Commit**

```bash
git add .gitignore
git commit -m "Ignore build directory"
```
