# Benchmark Phase 9 (SBE Comparison) Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax for tracking. (To be executed directly by Claude in this session, autonomously, per explicit user authorization to proceed — no subagent dispatch, no per-step confirmation, matching how Phases 3-8 were executed.)

**Goal:** Add an SBE (Simple Binary Encoding) encode/decode benchmark suite for the same logical IM message content as Phases 1/2/3/4/7, so the final report can show real measured Protobuf-vs-SBE numbers instead of the earlier estimate.

**Architecture:** A new `schema/chat.sbe.xml` compiled at build time (via a downloaded `sbe-all-1.38.1.jar` + `java -jar`) into header-only C++ codecs, exposed as an `INTERFACE` library `chat_sbe`. New `src/sbe_message_fixtures.{h,cpp}` mirror the existing protobuf fixtures field-for-field. New benchmarks in `src/bench.cpp` follow the exact naming/structure conventions of Phases 1-4 and 7. New round-trip checks in `src/main.cpp` are the only correctness safety net (SBE has no self-validating decode).

**Tech Stack:** C++20, existing `proto_bench`/`proto_test`/Google Benchmark harness, SBE C++ codegen from `uk.co.real-logic:sbe-all:1.38.1` (Java 21 confirmed present on this host via `java -version`; Maven Central confirmed reachable).

## Global Constraints

- `sbe-all` version is pinned to **1.38.1** (confirmed real via Maven Central `maven-metadata.xml`), jar SHA1 pinned to `ef7dd43a54a0269854ac2a296c2f6ba25edbaeff` (confirmed via the published `.sha1` file on Maven Central) in the `file(DOWNLOAD ... EXPECTED_HASH SHA1=...)` call.
- **Confirmed by a recon spike before this plan was written** (downloaded the jar, ran codegen against the exact schema below, hand-wrote and compiled a standalone round-trip test against the real generated headers): the generated C++ code is **header-only** (no `.cpp` files) — `chat_sbe` must be an `INTERFACE` library, not `STATIC`. Generated files, confirmed to exist at exactly these paths under the output dir: `im_chat_sbe/ConversationType.h`, `im_chat_sbe/GroupSizeEncoding.h`, `im_chat_sbe/MergedForwardChatMessage.h`, `im_chat_sbe/MessageHeader.h`, `im_chat_sbe/MessageStatus.h`, `im_chat_sbe/TextChatMessage.h`, `im_chat_sbe/VarStringEncoding.h`.
- **Critical correctness rule, confirmed empirically in the same recon spike (it does NOT show up in the official docs/examples and is easy to get silently wrong):** SBE decode of variable-length `<data>` fields and repeating-`<group>` entries uses one shared "running position" cursor per message. Every `<data>` field must be read via its `getXxx()`/`putXxx()` accessor **in exact schema-declared order**, and every group entry's own `<data>` field must be read for **every** entry the group is iterated over (even entries whose value you don't otherwise need) — skipping one desyncs the cursor for everything that follows, silently returning garbage for subsequent reads (it does not throw in an obvious spot; it throws/garbles somewhere downstream, e.g. a `getXxx()` call several fields later, or just returns wrong bytes with no error at all). This governs the exact code in Tasks 2-8 below — do not "optimize" by skipping a field's accessor call.
- `groupSizeEncoding`'s `numInGroup` is defined as `uint16` (not the docs' example default of `uint8`), because Phase 3's sweep goes to n=1000 and the default `uint8` caps groups at 254 entries (confirmed by the recon spike: this throws `std::runtime_error("count outside of allowed range [E110]")` at n=1000 with the default `uint8` composite).
- This is a pure addition phase: no existing protobuf code, schema, fixtures, or benchmarks are modified. `proto/chat.proto` and `src/message_fixtures.{h,cpp}` are untouched.
- Do NOT delete/empty `build/` or `build/_deps/`.
- No placeholder text/numbers in the committed analysis doc (Task 9).
- Build with `cmake --build build -j$(nproc)` (use the explicit job count, not bare `-j`).

---

### Task 1: SBE schema + CMake codegen wiring + `chat_sbe` library

**Files:**
- Create: `schema/chat.sbe.xml`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: an `INTERFACE` CMake target `chat_sbe` that exposes the generated headers under namespace `im_chat_sbe` (e.g. `#include "im_chat_sbe/TextChatMessage.h"`).

- [ ] **Step 1: Create `schema/chat.sbe.xml`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<sbe:messageSchema xmlns:sbe="http://fixprotocol.io/2016/sbe"
                    package="im_chat_sbe"
                    id="1"
                    version="0"
                    semanticVersion="1.0"
                    byteOrder="littleEndian">
  <types>
    <composite name="messageHeader" description="Message identifiers and length of message root">
      <type name="blockLength" primitiveType="uint16"/>
      <type name="templateId" primitiveType="uint16"/>
      <type name="schemaId" primitiveType="uint16"/>
      <type name="version" primitiveType="uint16"/>
    </composite>
    <composite name="groupSizeEncoding" description="Repeating group dimensions (uint16 count to support up to 1000-element sweeps)">
      <type name="blockLength" primitiveType="uint16"/>
      <type name="numInGroup" primitiveType="uint16" semanticType="NumInGroup"/>
    </composite>
    <composite name="varStringEncoding">
      <type name="length" primitiveType="uint32" maxValue="1073741824"/>
      <type name="varData" primitiveType="uint8" length="0" characterEncoding="UTF-8"/>
    </composite>
    <enum name="ConversationType" encodingType="uint8">
      <validValue name="UNSPECIFIED">0</validValue>
      <validValue name="SINGLE">1</validValue>
      <validValue name="GROUP">2</validValue>
      <validValue name="CHANNEL">3</validValue>
    </enum>
    <enum name="MessageStatus" encodingType="uint8">
      <validValue name="UNSPECIFIED">0</validValue>
      <validValue name="SENDING">1</validValue>
      <validValue name="SENT">2</validValue>
      <validValue name="DELIVERED">3</validValue>
      <validValue name="READ">4</validValue>
      <validValue name="FAILED">5</validValue>
      <validValue name="RECALLED">6</validValue>
    </enum>
  </types>

  <sbe:message name="TextChatMessage" id="1">
    <field name="messageId" id="1" type="int64"/>
    <field name="conversationId" id="2" type="int64"/>
    <field name="conversationType" id="3" type="ConversationType"/>
    <field name="senderId" id="4" type="int64"/>
    <field name="seq" id="5" type="int64"/>
    <field name="clientTimestampMs" id="6" type="int64"/>
    <field name="serverTimestampMs" id="7" type="int64"/>
    <field name="status" id="8" type="MessageStatus"/>
    <field name="quotedMessageId" id="9" type="int64" presence="optional"/>
    <field name="quotedSenderId" id="10" type="int64" presence="optional"/>
    <group name="mentionedUserIds" id="11" dimensionType="groupSizeEncoding">
      <field name="userId" id="12" type="int64"/>
    </group>
    <data name="clientMsgId" id="13" type="varStringEncoding"/>
    <data name="contentPreview" id="14" type="varStringEncoding"/>
    <data name="body" id="15" type="varStringEncoding"/>
  </sbe:message>

  <sbe:message name="MergedForwardChatMessage" id="2">
    <field name="messageId" id="1" type="int64"/>
    <field name="conversationId" id="2" type="int64"/>
    <field name="conversationType" id="3" type="ConversationType"/>
    <field name="senderId" id="4" type="int64"/>
    <field name="seq" id="5" type="int64"/>
    <field name="clientTimestampMs" id="6" type="int64"/>
    <field name="serverTimestampMs" id="7" type="int64"/>
    <field name="status" id="8" type="MessageStatus"/>
    <group name="items" id="9" dimensionType="groupSizeEncoding">
      <field name="messageId" id="10" type="int64"/>
      <field name="senderId" id="11" type="int64"/>
      <field name="timestampMs" id="12" type="int64"/>
      <data name="body" id="13" type="varStringEncoding"/>
    </group>
    <data name="clientMsgId" id="14" type="varStringEncoding"/>
    <data name="title" id="15" type="varStringEncoding"/>
  </sbe:message>
</sbe:messageSchema>
```

- [ ] **Step 2: Add SBE codegen wiring to `CMakeLists.txt`**

Append at the end of `CMakeLists.txt` (after the existing `proto_bench` target):

```cmake
find_package(Java REQUIRED COMPONENTS Runtime)

set(SBE_TOOL_VERSION 1.38.1)
set(SBE_JAR ${CMAKE_CURRENT_BINARY_DIR}/sbe-all-${SBE_TOOL_VERSION}.jar)
file(DOWNLOAD
  https://repo1.maven.org/maven2/uk/co/real-logic/sbe-all/${SBE_TOOL_VERSION}/sbe-all-${SBE_TOOL_VERSION}.jar
  ${SBE_JAR}
  EXPECTED_HASH SHA1=ef7dd43a54a0269854ac2a296c2f6ba25edbaeff
)

set(SBE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated-sbe)
file(MAKE_DIRECTORY ${SBE_GENERATED_DIR})

set(SBE_GENERATED_HEADERS
  ${SBE_GENERATED_DIR}/im_chat_sbe/ConversationType.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/GroupSizeEncoding.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/MergedForwardChatMessage.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/MessageHeader.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/MessageStatus.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/TextChatMessage.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/VarStringEncoding.h
)

add_custom_command(
  OUTPUT ${SBE_GENERATED_HEADERS}
  COMMAND ${Java_JAVA_EXECUTABLE}
          --add-opens java.base/jdk.internal.misc=ALL-UNNAMED
          -Dsbe.output.dir=${SBE_GENERATED_DIR}
          -Dsbe.target.language=Cpp
          -Dsbe.errorLog=yes
          -jar ${SBE_JAR}
          ${CMAKE_CURRENT_SOURCE_DIR}/schema/chat.sbe.xml
  DEPENDS ${SBE_JAR} ${CMAKE_CURRENT_SOURCE_DIR}/schema/chat.sbe.xml
  COMMENT "Generating SBE C++ codecs from schema/chat.sbe.xml"
)
add_custom_target(generate_chat_sbe DEPENDS ${SBE_GENERATED_HEADERS})

add_library(chat_sbe INTERFACE)
target_include_directories(chat_sbe INTERFACE ${SBE_GENERATED_DIR})
add_dependencies(chat_sbe generate_chat_sbe)

target_link_libraries(proto_test PRIVATE chat_sbe)
target_link_libraries(proto_bench PRIVATE chat_sbe)
```

- [ ] **Step 3: Configure and build**

Run: `cmake -S . -B build`
Expected: configure succeeds. This adds a new `find_package(Java REQUIRED COMPONENTS Runtime)` call — if it fails, the host's Java is not on `PATH` for CMake's search; re-run with `-DJava_JAVA_EXECUTABLE=$(which java)` and report this deviation rather than silently changing the plan's approach.

Run: `cmake --build build -j$(nproc)`
Expected: the `generate_chat_sbe` custom command runs once (downloads nothing new since the jar URL/hash is unchanged from Step 2's first configure-time download — note the `file(DOWNLOAD)` happens at **configure** time, so it already ran in Step 3's `cmake -S . -B build`; the **codegen** itself runs at **build** time via `add_custom_command`), producing the 7 headers under `build/generated-sbe/im_chat_sbe/`. `proto_test` and `proto_bench` still link and build successfully (they don't reference any SBE symbols yet, so this just proves the new target wires in cleanly).

- [ ] **Step 4: Verify the generated headers exist and match the recon spike**

Run: `ls build/generated-sbe/im_chat_sbe/`
Expected: exactly the 7 files listed in Step 2's `SBE_GENERATED_HEADERS`. If the list differs, STOP and reconcile `CMakeLists.txt`'s `OUTPUT` list with the actual files before continuing (a stale `OUTPUT` list breaks incremental rebuilds silently).

- [ ] **Step 5: Commit**

```bash
git add schema/chat.sbe.xml CMakeLists.txt
git commit -m "Add SBE schema and codegen wiring for Phase 9 comparison"
```

---

### Task 2: `src/sbe_message_fixtures.{h,cpp}`

**Files:**
- Create: `src/sbe_message_fixtures.h`
- Create: `src/sbe_message_fixtures.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `chat_sbe` target (Task 1) — `im_chat_sbe::MessageHeader`, `im_chat_sbe::TextChatMessage`, `im_chat_sbe::MergedForwardChatMessage`.
- Produces:
  - `std::size_t EncodeTextMessageSbe(char* buffer, std::size_t capacity);`
  - `std::size_t EncodeTextMessageWithIdSbe(char* buffer, std::size_t capacity, std::int64_t message_id);`
  - `std::size_t EncodeSparseTextMessageSbe(char* buffer, std::size_t capacity);`
  - `std::size_t EncodeTextMessageWithMentionCountSbe(char* buffer, std::size_t capacity, int mention_count);`
  - `std::size_t EncodeMergedForwardMessageSbe(char* buffer, std::size_t capacity);`
  - `std::size_t EncodeMergedForwardMessageWithItemCountSbe(char* buffer, std::size_t capacity, int item_count);`
  - All return the total encoded length (`MessageHeader::encodedLength() + body.encodedLength()`).

- [ ] **Step 1: Create `src/sbe_message_fixtures.h`**

```cpp
#ifndef PROTO_TEST_SBE_MESSAGE_FIXTURES_H_
#define PROTO_TEST_SBE_MESSAGE_FIXTURES_H_

#include <cstddef>
#include <cstdint>

std::size_t EncodeTextMessageSbe(char* buffer, std::size_t capacity);
std::size_t EncodeTextMessageWithIdSbe(char* buffer, std::size_t capacity, std::int64_t message_id);
std::size_t EncodeSparseTextMessageSbe(char* buffer, std::size_t capacity);
std::size_t EncodeTextMessageWithMentionCountSbe(char* buffer, std::size_t capacity, int mention_count);
std::size_t EncodeMergedForwardMessageSbe(char* buffer, std::size_t capacity);
std::size_t EncodeMergedForwardMessageWithItemCountSbe(char* buffer, std::size_t capacity, int item_count);

#endif  // PROTO_TEST_SBE_MESSAGE_FIXTURES_H_
```

- [ ] **Step 2: Create `src/sbe_message_fixtures.cpp`**

Field values are copied verbatim from `src/message_fixtures.cpp` (same IDs, same strings, same timestamps) so the SBE and Protobuf benchmarks encode the same logical content.

```cpp
#include "sbe_message_fixtures.h"

#include <string>

#include "im_chat_sbe/MergedForwardChatMessage.h"
#include "im_chat_sbe/MessageHeader.h"
#include "im_chat_sbe/TextChatMessage.h"

using im_chat_sbe::ConversationType;
using im_chat_sbe::MergedForwardChatMessage;
using im_chat_sbe::MessageHeader;
using im_chat_sbe::MessageStatus;
using im_chat_sbe::TextChatMessage;

std::size_t EncodeTextMessageWithIdSbe(char* buffer, std::size_t capacity, std::int64_t message_id) {
  TextChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(message_id)
      .conversationId(555)
      .conversationType(ConversationType::Value::GROUP)
      .senderId(42)
      .seq(17)
      .clientTimestampMs(1750000000000LL)
      .serverTimestampMs(1750000000050LL)
      .status(MessageStatus::Value::SENT)
      .quotedMessageId(998)
      .quotedSenderId(7);

  auto& mentions = msg.mentionedUserIdsCount(2);
  mentions.next().userId(7);
  mentions.next().userId(9);

  msg.putClientMsgId(std::string("client-uuid-abc123"));
  msg.putContentPreview(std::string("原消息预览文本"));
  msg.putBody(std::string("Hello, this is a test message."));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeTextMessageSbe(char* buffer, std::size_t capacity) {
  return EncodeTextMessageWithIdSbe(buffer, capacity, 1001);
}

std::size_t EncodeSparseTextMessageSbe(char* buffer, std::size_t capacity) {
  TextChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(1001)
      .conversationId(555)
      .conversationType(ConversationType::Value::GROUP)
      .senderId(42)
      .seq(17)
      .clientTimestampMs(1750000000000LL)
      .serverTimestampMs(1750000000050LL)
      .status(MessageStatus::Value::SENT)
      .quotedMessageId(TextChatMessage::quotedMessageIdNullValue())
      .quotedSenderId(TextChatMessage::quotedSenderIdNullValue());

  msg.mentionedUserIdsCount(0);

  msg.putClientMsgId(std::string("client-uuid-abc123"));
  msg.putContentPreview(std::string(""));
  msg.putBody(std::string("Hello, this is a test message."));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeTextMessageWithMentionCountSbe(char* buffer, std::size_t capacity, int mention_count) {
  TextChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(1001)
      .conversationId(555)
      .conversationType(ConversationType::Value::GROUP)
      .senderId(42)
      .seq(17)
      .clientTimestampMs(1750000000000LL)
      .serverTimestampMs(1750000000050LL)
      .status(MessageStatus::Value::SENT)
      .quotedMessageId(998)
      .quotedSenderId(7);

  auto& mentions = msg.mentionedUserIdsCount(static_cast<std::uint16_t>(mention_count));
  for (int i = 1; i <= mention_count; ++i) {
    mentions.next().userId(i);
  }

  msg.putClientMsgId(std::string("client-uuid-abc123"));
  msg.putContentPreview(std::string("原消息预览文本"));
  msg.putBody(std::string("Hello, this is a test message."));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeMergedForwardMessageWithItemCountSbe(char* buffer, std::size_t capacity, int item_count) {
  MergedForwardChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(2002)
      .conversationId(777)
      .conversationType(ConversationType::Value::SINGLE)
      .senderId(42)
      .seq(18)
      .clientTimestampMs(1750000001000LL)
      .serverTimestampMs(1750000001050LL)
      .status(MessageStatus::Value::SENT);

  auto& items = msg.itemsCount(static_cast<std::uint16_t>(item_count));
  for (int i = 0; i < item_count; ++i) {
    items.next()
        .messageId(101 + i)
        .senderId(7)
        .timestampMs(1749999999000LL + i)
        .putBody(std::string("被转发的消息"));
  }

  msg.putClientMsgId(std::string("client-uuid-def456"));
  msg.putTitle(std::string("群聊的聊天记录"));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeMergedForwardMessageSbe(char* buffer, std::size_t capacity) {
  MergedForwardChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(2002)
      .conversationId(777)
      .conversationType(ConversationType::Value::SINGLE)
      .senderId(42)
      .seq(18)
      .clientTimestampMs(1750000001000LL)
      .serverTimestampMs(1750000001050LL)
      .status(MessageStatus::Value::SENT);

  auto& items = msg.itemsCount(2);
  items.next().messageId(101).senderId(7).timestampMs(1749999999000LL).putBody(std::string("第一条被转发的消息"));
  items.next().messageId(102).senderId(9).timestampMs(1749999999500LL).putBody(std::string("第二条被转发的消息"));

  msg.putClientMsgId(std::string("client-uuid-def456"));
  msg.putTitle(std::string("群聊的聊天记录"));

  return MessageHeader::encodedLength() + msg.encodedLength();
}
```

Note: `BuildMergedForwardMessage()` in the existing protobuf fixtures uses item bodies `"第一条被转发的消息"`/`"第二条被转发的消息"` (distinct from `BuildMergedForwardMessageWithItemCount`'s uniform `"被转发的消息"` for every item) — this mirrors that exactly; do not unify them.

- [ ] **Step 3: Add the new files to both `proto_test` and `proto_bench` in `CMakeLists.txt`**

Change:
```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp)
```
to:
```cmake
add_executable(proto_test src/main.cpp src/message_fixtures.cpp src/sbe_message_fixtures.cpp)
```

Change:
```cmake
add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp)
```
to:
```cmake
add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp src/sbe_message_fixtures.cpp)
```

- [ ] **Step 4: Reconfigure and build**

Run: `cmake -S . -B build && cmake --build build -j$(nproc)`
Expected: builds successfully. `proto_test`/`proto_bench` don't call the new functions yet (Tasks 3-4 do), so this only proves the new translation unit compiles and links against `chat_sbe`.

- [ ] **Step 5: Commit**

```bash
git add src/sbe_message_fixtures.h src/sbe_message_fixtures.cpp CMakeLists.txt
git commit -m "Add SBE fixture encoders mirroring message_fixtures.cpp"
```

---

### Task 3: SBE round-trip correctness checks in `proto_test`

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: all 6 functions from Task 2; `im_chat_sbe::MessageHeader`/`TextChatMessage`/`MergedForwardChatMessage` decode API.

This is the only correctness safety net for the new schema — SBE decode does not self-validate, so a wrong field offset or a skipped var-data read produces wrong values silently rather than a thrown error. It uses a dedicated `Check()` helper instead of `assert()` because this project's pinned Release build (`CMAKE_CXX_FLAGS_RELEASE = -O3 -DNDEBUG`, confirmed via `cmake -LA`) compiles `assert()` to a no-op, and unlike the existing Protobuf checks (where Protobuf's own upstream test suite is the real correctness backstop), this hand-written schema has no other safety net.

- [ ] **Step 1: Add includes and the `Check()` helper to `src/main.cpp`**

At the top of `src/main.cpp`, add after the existing includes:

```cpp
#include <cstdlib>

#include "im_chat_sbe/MergedForwardChatMessage.h"
#include "im_chat_sbe/MessageHeader.h"
#include "im_chat_sbe/TextChatMessage.h"
#include "sbe_message_fixtures.h"
```

Inside the existing anonymous `namespace {` block (before `TestTextMessageRoundTrip`), add:

```cpp
void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "SBE check failed: " << message << "\n";
    std::abort();
  }
}
```

- [ ] **Step 2: Add `TestSbeTextMessageRoundTrip()`**

Insert after the existing `TestMergedForwardRoundTrip()` function, still inside the anonymous namespace:

```cpp
void TestSbeTextMessageRoundTrip() {
  using im_chat_sbe::ConversationType;
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::MessageStatus;
  using im_chat_sbe::TextChatMessage;

  char buffer[512];
  const std::size_t total_len = EncodeTextMessageSbe(buffer, sizeof(buffer));

  MessageHeader hdr;
  hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
  Check(hdr.templateId() == TextChatMessage::sbeTemplateId(), "templateId mismatch");

  TextChatMessage dec;
  dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));

  Check(dec.messageId() == 1001, "messageId");
  Check(dec.conversationId() == 555, "conversationId");
  Check(dec.conversationType() == ConversationType::Value::GROUP, "conversationType");
  Check(dec.senderId() == 42, "senderId");
  Check(dec.seq() == 17, "seq");
  Check(dec.clientTimestampMs() == 1750000000000LL, "clientTimestampMs");
  Check(dec.serverTimestampMs() == 1750000000050LL, "serverTimestampMs");
  Check(dec.status() == MessageStatus::Value::SENT, "status");
  Check(dec.quotedMessageId() == 998, "quotedMessageId");
  Check(dec.quotedSenderId() == 7, "quotedSenderId");

  auto& mentions = dec.mentionedUserIds();
  Check(mentions.count() == 2, "mentionedUserIds count");
  Check(mentions.hasNext(), "mentionedUserIds[0] hasNext");
  Check(mentions.next().userId() == 7, "mentionedUserIds[0]");
  Check(mentions.hasNext(), "mentionedUserIds[1] hasNext");
  Check(mentions.next().userId() == 9, "mentionedUserIds[1]");
  Check(!mentions.hasNext(), "mentionedUserIds exhausted");

  char tmp[256];
  std::uint64_t n = dec.getClientMsgId(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "client-uuid-abc123", "clientMsgId");
  n = dec.getContentPreview(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "原消息预览文本", "contentPreview");
  n = dec.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "Hello, this is a test message.", "body");

  std::cout << "TestSbeTextMessageRoundTrip passed, encoded size = " << total_len << " bytes\n";
}

void TestSbeSparseTextMessageRoundTrip() {
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::TextChatMessage;

  char buffer[512];
  const std::size_t total_len = EncodeSparseTextMessageSbe(buffer, sizeof(buffer));

  MessageHeader hdr;
  hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
  TextChatMessage dec;
  dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));

  Check(dec.quotedMessageId() == TextChatMessage::quotedMessageIdNullValue(), "sparse quotedMessageId null");
  Check(dec.quotedSenderId() == TextChatMessage::quotedSenderIdNullValue(), "sparse quotedSenderId null");

  auto& mentions = dec.mentionedUserIds();
  Check(mentions.count() == 0, "sparse mentionedUserIds count");
  Check(!mentions.hasNext(), "sparse mentionedUserIds empty");

  char tmp[256];
  std::uint64_t n = dec.getClientMsgId(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "client-uuid-abc123", "sparse clientMsgId");
  n = dec.getContentPreview(tmp, sizeof(tmp));
  Check(n == 0, "sparse contentPreview empty");
  n = dec.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "Hello, this is a test message.", "sparse body");

  std::cout << "TestSbeSparseTextMessageRoundTrip passed, encoded size = " << total_len << " bytes\n";
}

void TestSbeMergedForwardRoundTrip() {
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::MergedForwardChatMessage;

  char buffer[2048];
  const std::size_t total_len = EncodeMergedForwardMessageSbe(buffer, sizeof(buffer));

  MessageHeader hdr;
  hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
  Check(hdr.templateId() == MergedForwardChatMessage::sbeTemplateId(), "templateId mismatch");

  MergedForwardChatMessage dec;
  dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));

  Check(dec.messageId() == 2002, "messageId");
  Check(dec.conversationId() == 777, "conversationId");

  auto& items = dec.items();
  Check(items.count() == 2, "items count");

  char tmp[256];
  items.next();
  Check(items.messageId() == 101, "item0 messageId");
  Check(items.senderId() == 7, "item0 senderId");
  Check(items.timestampMs() == 1749999999000LL, "item0 timestampMs");
  std::uint64_t n = items.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "第一条被转发的消息", "item0 body");

  items.next();
  Check(items.messageId() == 102, "item1 messageId");
  n = items.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "第二条被转发的消息", "item1 body");
  Check(!items.hasNext(), "items exhausted");

  n = dec.getClientMsgId(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "client-uuid-def456", "clientMsgId");
  n = dec.getTitle(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "群聊的聊天记录", "title");

  std::cout << "TestSbeMergedForwardRoundTrip passed, encoded size = " << total_len << " bytes\n";
}

void TestSbeLargeSweepRoundTrip() {
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::MergedForwardChatMessage;
  using im_chat_sbe::TextChatMessage;

  // Mentions sweep at n=1000 (validates the uint16 groupSizeEncoding fix).
  static char mention_buf[1 << 16];
  const std::size_t mention_len = EncodeTextMessageWithMentionCountSbe(mention_buf, sizeof(mention_buf), 1000);
  MessageHeader mention_hdr;
  mention_hdr.wrap(mention_buf, 0, MessageHeader::sbeSchemaVersion(), sizeof(mention_buf));
  TextChatMessage mention_dec;
  mention_dec.wrapForDecode(
      mention_buf, MessageHeader::encodedLength(), mention_hdr.blockLength(), mention_hdr.version(),
      sizeof(mention_buf));
  auto& mentions = mention_dec.mentionedUserIds();
  Check(mentions.count() == 1000, "1000-mention count");
  int mention_seen = 0;
  while (mentions.hasNext()) {
    mentions.next();
    ++mention_seen;
  }
  Check(mention_seen == 1000, "1000-mention iteration count");
  std::cout << "TestSbeLargeSweepRoundTrip mentions n=1000 passed, encoded size = " << mention_len << " bytes\n";

  // Merged-forward items sweep at n=1000.
  static char items_buf[1 << 16];
  const std::size_t items_len = EncodeMergedForwardMessageWithItemCountSbe(items_buf, sizeof(items_buf), 1000);
  MessageHeader items_hdr;
  items_hdr.wrap(items_buf, 0, MessageHeader::sbeSchemaVersion(), sizeof(items_buf));
  MergedForwardChatMessage items_dec;
  items_dec.wrapForDecode(
      items_buf, MessageHeader::encodedLength(), items_hdr.blockLength(), items_hdr.version(), sizeof(items_buf));
  auto& items = items_dec.items();
  Check(items.count() == 1000, "1000-item count");
  char tmp[256];
  int items_seen = 0;
  while (items.hasNext()) {
    items.next();
    items.getBody(tmp, sizeof(tmp));
    ++items_seen;
  }
  Check(items_seen == 1000, "1000-item iteration count");
  std::cout << "TestSbeLargeSweepRoundTrip items n=1000 passed, encoded size = " << items_len << " bytes\n";
}
```

- [ ] **Step 3: Call the new checks from `main()`**

Change:
```cpp
int main() {
  TestTextMessageRoundTrip();
  TestMergedForwardRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";
  return 0;
}
```
to:
```cpp
int main() {
  TestTextMessageRoundTrip();
  TestMergedForwardRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";

  TestSbeTextMessageRoundTrip();
  TestSbeSparseTextMessageRoundTrip();
  TestSbeMergedForwardRoundTrip();
  TestSbeLargeSweepRoundTrip();
  std::cout << "All SBE round-trip tests passed.\n";
  return 0;
}
```

- [ ] **Step 4: Build and run**

Run: `cmake --build build -j$(nproc) && ./build/proto_test`
Expected: exit code 0, all 6 lines printed (2 existing protobuf + 4 new SBE), ending with both "All protobuf round-trip tests passed." and "All SBE round-trip tests passed." If any `Check()` fails, the program prints `SBE check failed: <message>` to stderr and aborts (nonzero exit / SIGABRT) — do not proceed to Task 4 until this is exit-code 0.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "Add SBE round-trip correctness checks to proto_test"
```

---

### Task 4: Phase-1-equivalent SBE benchmarks (encode/decode, text + merged_forward)

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: Task 2's encode functions; `im_chat_sbe::MessageHeader`/`TextChatMessage`/`MergedForwardChatMessage` decode API.

- [ ] **Step 1: Add includes**

At the top of `src/bench.cpp`, add:

```cpp
#include "im_chat_sbe/MergedForwardChatMessage.h"
#include "im_chat_sbe/MessageHeader.h"
#include "im_chat_sbe/TextChatMessage.h"
#include "sbe_message_fixtures.h"
```

- [ ] **Step 2: Insert the Phase-1-equivalent benchmarks**

Insert inside the anonymous namespace, after the existing `BM_ParseGarbageText`/`BENCHMARK(BM_ParseGarbageText)` block (the last entry), before the closing `}  // namespace`:

```cpp
using im_chat_sbe::ConversationType;
using im_chat_sbe::MergedForwardChatMessage;
using im_chat_sbe::MessageHeader;
using im_chat_sbe::MessageStatus;
using im_chat_sbe::TextChatMessage;

void BM_EncodeTextSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeTextSbe);

void BM_DecodeTextSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeTextSbe);

void BM_EncodeMergedForwardSbe(benchmark::State& state) {
  char buffer[2048];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeMergedForwardMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeMergedForwardSbe);

void BM_DecodeMergedForwardSbe(benchmark::State& state) {
  char buffer[2048];
  const std::size_t len = EncodeMergedForwardMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    MergedForwardChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    auto& items = dec.items();
    while (items.hasNext()) {
      items.next();
      benchmark::DoNotOptimize(items.messageId());
      benchmark::DoNotOptimize(items.senderId());
      benchmark::DoNotOptimize(items.timestampMs());
      benchmark::DoNotOptimize(items.getBody(tmp, sizeof(tmp)));
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getTitle(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeMergedForwardSbe);
```

- [ ] **Step 3: Build and run**

Run: `cmake --build build -j$(nproc) && ./build/proto_bench --benchmark_filter="Sbe"`
Expected: 4 rows (`BM_EncodeTextSbe`, `BM_DecodeTextSbe`, `BM_EncodeMergedForwardSbe`, `BM_DecodeMergedForwardSbe`), each with a `bytes` counter. Sanity check: `BM_EncodeTextSbe`'s `bytes` should be in the 150-200 range (Phase 1's protobuf `text` is 117 bytes; SBE's fixed-width fields make this larger, which is expected, not a bug) and `BM_EncodeMergedForwardSbe`'s `bytes` should be larger still (2-item group). If `BM_DecodeTextSbe`/`BM_DecodeMergedForwardSbe` throw at runtime (`std::runtime_error`), the var-data/group read order was violated — re-check against this plan's Global Constraints note on running-cursor order, do not silently catch and ignore the exception.

- [ ] **Step 4: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0, all 6 round-trip lines still pass.

- [ ] **Step 5: Commit**

```bash
git add src/bench.cpp
git commit -m "Add SBE encode/decode benchmarks for text and merged_forward (Phase 1 equivalent)"
```

---

### Task 5: Phase-2-equivalent SBE benchmarks (sparse, small/large ID)

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: `EncodeSparseTextMessageSbe`, `EncodeTextMessageWithIdSbe` (Task 2).

- [ ] **Step 1: Insert after Task 4's benchmarks, before the closing `}  // namespace`**

```cpp
void BM_EncodeSparseTextSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeSparseTextMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeSparseTextSbe);

void BM_DecodeSparseTextSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeSparseTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeSparseTextSbe);

void BM_EncodeSmallIdSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeSmallIdSbe);

void BM_DecodeSmallIdSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeSmallIdSbe);

void BM_EncodeLargeIdSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1950123456789012345LL);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeLargeIdSbe);

void BM_DecodeLargeIdSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1950123456789012345LL);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeLargeIdSbe);
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j$(nproc) && ./build/proto_bench --benchmark_filter="SmallIdSbe|LargeIdSbe|SparseTextSbe"`
Expected: 6 rows. Sanity check (the key prediction from the earlier estimate): `BM_EncodeSmallIdSbe`'s `bytes` and `real_time` should be **identical or near-identical** to `BM_EncodeLargeIdSbe`'s (unlike Protobuf's varint-driven Phase 2 result, where the large ID costs 8 more bytes and ~4% more time) — SBE's `messageId` is a fixed 8-byte field regardless of value. If they differ by more than noise, something is wrong with the encoding (e.g. accidentally var-length-encoding something).

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add SBE sparse/small-id/large-id benchmarks (Phase 2 equivalent)"
```

---

### Task 6: Phase-3-equivalent SBE benchmarks (mentions sweep, merged items sweep)

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: `EncodeTextMessageWithMentionCountSbe`, `EncodeMergedForwardMessageWithItemCountSbe` (Task 2).

- [ ] **Step 1: Insert after Task 5's benchmarks, before the closing `}  // namespace`**

```cpp
void BM_EncodeMentionsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageWithMentionCountSbe(buffer, sizeof(buffer), n);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeMentionsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_DecodeMentionsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  const std::size_t len = EncodeTextMessageWithMentionCountSbe(buffer, sizeof(buffer), n);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeMentionsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_EncodeMergedItemsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeMergedForwardMessageWithItemCountSbe(buffer, sizeof(buffer), n);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeMergedItemsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_DecodeMergedItemsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  const std::size_t len = EncodeMergedForwardMessageWithItemCountSbe(buffer, sizeof(buffer), n);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    MergedForwardChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    auto& items = dec.items();
    while (items.hasNext()) {
      items.next();
      benchmark::DoNotOptimize(items.messageId());
      benchmark::DoNotOptimize(items.senderId());
      benchmark::DoNotOptimize(items.timestampMs());
      benchmark::DoNotOptimize(items.getBody(tmp, sizeof(tmp)));
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getTitle(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeMergedItemsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);
```

Buffers are `static` (not stack-local) because `1 << 16` = 65536 bytes is large enough that repeated stack allocation across benchmark re-runs is wasteful and, on some platforms, risks stack-size issues — `static` matches the "size once, outside the cost we're measuring" principle already used by `BM_SerializeToPreallocatedArray` in the existing Protobuf benchmarks, just sized for the largest sweep point instead of being computed from `ByteSizeLong()`. The 1000-item merged-forward case was confirmed in the recon spike to encode to 46109 bytes, comfortably under the 65536-byte buffer.

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j$(nproc) && ./build/proto_bench --benchmark_filter="MentionsSbe|MergedItemsSbe"`
Expected: 16 rows (4 benchmarks × 4 `Arg` values each). No thrown exceptions at n=1000 (this is the regression test for the `groupSizeEncoding` uint16 fix — if it throws `count outside of allowed range [E110]`, the schema change in Task 1 Step 1 was not applied correctly).

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add SBE mentions/merged-items scalability sweep benchmarks (Phase 3 equivalent)"
```

---

### Task 7: Phase-4-equivalent SBE benchmark (decode heap allocations)

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- Consumes: `ResetAllocCounters()`/`GetAllocCount()`/`GetAllocBytes()` from `src/alloc_counter.h` (already included in `bench.cpp`).

- [ ] **Step 1: Insert after Task 6's benchmarks, before the closing `}  // namespace`**

```cpp
void BM_DecodeTextHeapAllocsSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  ResetAllocCounters();
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeTextHeapAllocsSbe);
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j$(nproc) && ./build/proto_bench --benchmark_filter="DecodeTextHeapAllocsSbe"`
Expected: 1 row, `allocs_per_iter` at or extremely close to `0` (the flyweight decode does no heap allocation — `std::string`/`std::string_view` are not used in this decode path, only `char` buffers and `getXxx(char*, length)` copies). Compare informally against Phase 4's existing `BM_ParseTextHeapAllocs` value (~9 allocs/iter) when writing the Task 9 analysis doc — do not re-run Phase 4, just reference its already-committed number from `results/phase4-2026-06-18.json`.

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add SBE decode allocation-counting benchmark (Phase 4 equivalent)"
```

---

### Task 8: Phase-7-equivalent SBE benchmarks (concurrency scaling)

**Files:**
- Modify: `src/bench.cpp`

**Interfaces:**
- None beyond Task 2/4's existing functions.

- [ ] **Step 1: Insert after Task 7's benchmark, before the closing `}  // namespace`**

```cpp
void BM_ConcurrentEncodeTextSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_ConcurrentEncodeTextSbe)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ConcurrentDecodeTextSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_ConcurrentDecodeTextSbe)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);
```

Each thread gets its own `buffer`/`tmp` (declared inside the benchmark function, called once per thread by Google Benchmark) — no shared mutable state, matching the existing `BM_ConcurrentSerializeText`/`BM_ConcurrentParseText` design from Phase 7.

- [ ] **Step 2: Build and run**

Run: `cmake --build build -j$(nproc) && ./build/proto_bench --benchmark_filter="ConcurrentEncodeTextSbe|ConcurrentDecodeTextSbe"`
Expected: 12 rows (2 benchmarks × 6 thread counts).

- [ ] **Step 3: Re-verify `proto_test` still passes**

Run: `./build/proto_test`
Expected: exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/bench.cpp
git commit -m "Add SBE concurrent encode/decode benchmarks (Phase 7 equivalent)"
```

---

### Task 9: Run the full SBE suite, save results, write the Phase 9 analysis doc

**Files:**
- Create: `results/phase9-2026-06-19.json`
- Create: `docs/benchmarks/phase9-sbe-comparison-analysis.md`

- [ ] **Step 1: Run all SBE benchmarks plus their Protobuf counterparts together, JSON output**

```bash
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase9-2026-06-19.json \
  --benchmark_filter="Sbe|^BM_SerializeText$|^BM_ParseText$|^BM_SerializeMergedForward$|^BM_ParseMergedForward$|^BM_SerializeSparseText$|^BM_ParseSparseText$|^BM_SerializeSmallId$|^BM_ParseSmallId$|^BM_SerializeLargeId$|^BM_ParseLargeId$|^BM_SerializeMentions$|^BM_ParseMentions$|^BM_SerializeMergedItems$|^BM_ParseMergedItems$|^BM_ConcurrentSerializeText$|^BM_ConcurrentParseText$"
```

This produces one JSON file containing both the new SBE numbers and the matching Protobuf baselines from the same run/host/moment, so the analysis doc can cite exact numbers from a single source file instead of cross-referencing old phase JSONs for the Protobuf side.

- [ ] **Step 2: Extract and tabulate the numbers**

Read `results/phase9-2026-06-19.json` and, for each Protobuf/SBE pair, compute the ratio (Protobuf ns / SBE ns). Also pull Phase 4's existing `allocs_per_iter` for `BM_ParseTextHeapAllocs` from `results/phase4-2026-06-18.json` for the allocation-comparison row (do not re-run Phase 4).

- [ ] **Step 3: Write `docs/benchmarks/phase9-sbe-comparison-analysis.md`**

Structure (fill every `<...>` with the real number from Step 2 — no placeholders may remain in the committed file):

```markdown
# Phase 9 — SBE (Simple Binary Encoding) Comparison Analysis

Date: 2026-06-19
Raw data: `results/phase9-2026-06-19.json` (SBE + same-run Protobuf baselines), `results/phase4-2026-06-18.json` (allocation reference)

This phase replaces the earlier pure estimate (made before any SBE code existed) with measured numbers, for the identical logical message content used throughout Phases 1-8.

## Phase 1 equivalent: basic encode/decode

| Benchmark | Protobuf (ns/iter) | SBE (ns/iter) | Speedup | Protobuf bytes | SBE bytes |
|---|---|---|---|---|---|
| text encode | <...> | <...> | <...>x | <...> | <...> |
| text decode | <...> | <...> | <...>x | <...> | <...> |
| merged_forward encode | <...> | <...> | <...>x | <...> | <...> |
| merged_forward decode | <...> | <...> | <...>x | <...> | <...> |

<3-5 sentences: how do the real speedup numbers compare to the earlier 5-30x estimate? Is SBE larger or smaller on the wire for these specific messages, and does that match the "no varint compaction" prediction?>

## Phase 2 equivalent: sparse fill rate and ID width sensitivity

| Benchmark | Protobuf (ns/iter) | SBE (ns/iter) |
|---|---|---|
| sparse text encode | <...> | <...> |
| sparse text decode | <...> | <...> |
| small-ID text encode | <...> | <...> |
| large-ID text encode | <...> | <...> |

<2-4 sentences: confirm or refute the prediction that SBE's small-ID vs large-ID encode time/size is flat (unlike Protobuf's varint-driven difference), and that SBE's sparse-vs-full difference is smaller than Protobuf's 33-41%.>

## Phase 3 equivalent: scalability sweep (n=1/10/100/1000)

| n | Protobuf mentions encode (ns) | SBE mentions encode (ns) | Protobuf merged items encode (ns) | SBE merged items encode (ns) |
|---|---|---|---|---|
| 1 | <...> | <...> | <...> | <...> |
| 10 | <...> | <...> | <...> | <...> |
| 100 | <...> | <...> | <...> | <...> |
| 1000 | <...> | <...> | <...> | <...> |

<3-5 sentences: per-element marginal cost comparison (ns/element, computed the same way Phase 3 did: (n=1000 time - n=1 time) / 999). Does SBE's repeating-group cost scale anywhere near as steeply as Protobuf's repeated-message cost (Phase 3 measured ~25-108 ns/item for Protobuf)?>

## Phase 4 equivalent: decode allocation count

| Benchmark | allocs_per_iter |
|---|---|
| BM_ParseTextHeapAllocs (Protobuf, from Phase 4) | <...> |
| BM_DecodeTextHeapAllocsSbe (this phase) | <...> |

<2-3 sentences: confirm or refute the "flyweight decode is zero-allocation" prediction with the real number.>

## Phase 7 equivalent: concurrency scaling

| Threads | Protobuf encode scaling ratio | SBE encode scaling ratio | Protobuf decode scaling ratio | SBE decode scaling ratio |
|---|---|---|---|---|
| 1 | 1.00 | 1.00 | 1.00 | 1.00 |
| 2 | <...> | <...> | <...> | <...> |
| 4 | <...> | <...> | <...> | <...> |
| 8 | <...> | <...> | <...> | <...> |
| 16 | <...> | <...> | <...> | <...> |
| 20 | <...> | <...> | <...> | <...> |

<3-5 sentences: does SBE decode scale closer to linear than Protobuf's decode (which dropped to 0.57x at 2 threads per Phase 7), consistent with the zero-allocation finding above?>

## What this phase did not test (and why)

No SBE equivalent of Phase 5 (serialization API overhead — SBE has only one natural encoding path, a flyweight wrap of a caller-provided buffer, so there is no multi-API tradeoff to measure) or Phase 8 (malformed-input cost — SBE decode has no self-validating failure mode; constructing one would mean benchmarking a hand-written bounds-checking wrapper we wrote ourselves, not the library, and risks undefined behavior if done incorrectly). See `docs/superpowers/specs/2026-06-19-benchmark-phase9-sbe-design.md` for the full reasoning.

## Cross-cutting conclusion

<3-5 sentences: overall, by what rough factor is SBE faster, where does Protobuf retain an advantage (if anywhere — e.g. wire size for small-value fields, or self-describing/self-validating decode), and what would have to be true about a real system's message-size/field-sparsity/value-distribution for the protobuf-vs-SBE tradeoff to flip?>
```

- [ ] **Step 4: Commit**

```bash
git add results/phase9-2026-06-19.json docs/benchmarks/phase9-sbe-comparison-analysis.md
git commit -m "Run Phase 9 SBE benchmarks and record comparison analysis"
```

---

### Task 10: Update the final report

**Files:**
- Modify: `docs/benchmarks/final-report-phases-0-8.md`

- [ ] **Step 1: Rename the file to cover Phase 9**

```bash
git mv docs/benchmarks/final-report-phases-0-8.md docs/benchmarks/final-report-phases-0-9.md
```

- [ ] **Step 2: Add a "Phase 9" section**

Insert a new `## Phase 9 — SBE (Simple Binary Encoding) Comparison` section after the existing `## Phase 8` section (before `## Cross-cutting takeaways`), summarizing the headline numbers from Task 9's analysis doc (2-4 sentences plus the Phase-1-equivalent table), and linking to `docs/benchmarks/phase9-sbe-comparison-analysis.md`.

- [ ] **Step 3: Add an 8th cross-cutting takeaway**

Append to the numbered "Cross-cutting takeaways" list: a takeaway summarizing the real measured Protobuf-vs-SBE factor, referencing the specific numbers from Task 9 (not the pre-implementation estimate).

- [ ] **Step 4: Update the file index table**

Add a Phase 9 row to the `## File index` table, pointing at `docs/superpowers/specs/2026-06-19-benchmark-phase9-sbe-design.md`, this plan file, `results/phase9-2026-06-19.json`, and `docs/benchmarks/phase9-sbe-comparison-analysis.md`.

- [ ] **Step 5: Commit**

```bash
git add docs/benchmarks/final-report-phases-0-9.md docs/benchmarks/final-report-phases-0-8.md
git commit -m "Fold Phase 9 SBE comparison into the final report"
```

(Git records the rename automatically when both the old-path deletion and new-path addition are staged together; `git mv` already staged it, but explicitly `git add` both paths in case the rename detection needs the deletion staged too.)
