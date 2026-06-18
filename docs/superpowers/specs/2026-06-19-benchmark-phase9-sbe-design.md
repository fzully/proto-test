# Protobuf Benchmark Phase 9 — SBE (Simple Binary Encoding) Comparison Design

日期：2026-06-19

> 本 spec 在用户明确要求"在本工程内做 SBE 对比测试"并批准整体方向后编写。用户已确认按 spec → plan → implement → verify → commit 的既有流程推进。

## 背景

Phase 1-8 测的都是 Protobuf 的各项开销。用户想知道：如果把同样的 IM 消息内容换成金融行业常用的 SBE（Simple Binary Encoding，FIX/FPL 标准，LMAX/Aeron 等低延迟系统常用）编码，耗时会有多大差异。此前已经做过一轮纯估算（基于 SBE 的已知设计特性：定长字段、无 varint、无 tag 扫描、flyweight 解码零堆分配），现在要把估算换成实测。

SBE 用 XML schema 定义消息，用官方 `sbe-tool`（`uk.co.real-logic:sbe-all`，Java 实现，输出可选 Java/C++/C/C#/Golang/Rust）把 schema 编译成目标语言的 encoder/decoder 代码。本机已确认 `java -version` → OpenJDK 21 可用，Maven Central 可访问，`sbe-all` 最新发布版本是 **1.38.1**（已通过 `maven-metadata.xml` 确认是真实存在的版本号，不是猜测），其 jar 的官方 SHA1 校验值为 `ef7dd43a54a0269854ac2a296c2f6ba25edbaeff`（已通过 Maven Central 的 `.sha1` 文件确认）。

## 目标

测出与 Phase 1/2/3/4/7 完全对应的 SBE 数字（同一份逻辑消息内容、同一台机器、同一个 `proto_bench` 二进制），使报告里能逐行对照 Protobuf vs SBE：

1. **Phase 1 对应**：text / merged_forward 的 encode、decode 耗时和体积。
2. **Phase 2 对应**：sparse vs full text 的耗时/体积差异；small ID vs large ID 的耗时差异（验证"SBE 定长编码，ID 数值大小不影响耗时/体积"这个此前估算）。
3. **Phase 3 对应**：`mentionedUserIds`（repeating group of int64）和 `merged_forward.items`（repeating group of sub-record）在 n=1/10/100/1000 的耗时/体积扫描。
4. **Phase 4 对应**：decode 路径的堆分配次数（复用现有 `alloc_counter.h`），验证"SBE flyweight decode 零堆分配"这个估算。
5. **Phase 7 对应**：text 的 encode/decode 在 1/2/4/8/16/20 线程下的吞吐扫描。

## 范围

**包含：**
- 新增 `schema/chat.sbe.xml`，定义两个顶层 SBE message：`TextChatMessage`、`MergedForwardChatMessage`，字段范围严格对应 `message_fixtures.cpp` 里 6 个 builder 函数**实际写到的字段**（见下方"组件设计"），不覆盖 `chat.proto` 里 builder 从未填充的字段（`forward_info`、`edited`、`extra` map、image/audio/video/file/recall/system_event 等 oneof 分支）——保持"同样的逻辑内容"这个可比性前提，不为了"schema 全覆盖"而引入 fixture 根本没有的数据。
- CMake 新增：用 `file(DOWNLOAD ...)` 拉取 `sbe-all-1.38.1.jar`（带 `EXPECTED_HASH SHA1=ef7dd43a54a0269854ac2a296c2f6ba25edbaeff`，防止下载内容被篡改/损坏而不自知），用 `find_package(Java)` 找 `java` 可执行文件，用 `add_custom_command` 在配置/构建期调用它对 `schema/chat.sbe.xml` 跑 codegen，产出 C++ header 到 build 目录，新增 `chat_sbe` library 目标（具体是 STATIC 还是 INTERFACE，取决于 SBE C++ 生成器是否产出 `.cpp` 编译单元——按以往经验大概率是纯 header-only inline 实现，但本 spec 不假设，留给实现阶段第一次跑 codegen 后用 `ls` 确认再定）。
- 新增 `src/sbe_message_fixtures.{h,cpp}`：6 个 encode 函数，逐字段对应现有 `message_fixtures.cpp` 的 6 个 builder，写入调用方提供的 buffer。
- 新增 benchmark（`src/bench.cpp`）：对应上面目标 1-5 的所有用例，命名规则 `BM_<Encode|Decode><Shape>Sbe`，与现有 Protobuf 命名一一对应，方便报告里直接列对照表。
- `proto_test` 增加 SBE round-trip 正确性校验：encode 后 decode，逐字段断言数值/字符串与 fixture 输入一致。**这一步是必须的**——SBE decode 没有"解析失败"的概念，schema/offset 写错了不会报错，只会读出错误的字节,所以这是唯一能保证"我们测的是正确实现"的安全网。
- decode 类 benchmark 必须把每个字段都实际读出来（标量字段读入局部变量、repeating group 整个迭代一遍、每个 var-length 字符串拷进一个栈 buffer），并用 `benchmark::DoNotOptimize` 防止编译器优化掉这些读取——否则 SBE 的 decode benchmark 测的是"什么都没干的 wrap"，跟 Protobuf 的"构造完整可访问对象图"不是同一种工作量，对比就不成立。
- 新增 `results/phase9-2026-06-19.json` + `docs/benchmarks/phase9-sbe-comparison-analysis.md`。
- 更新 `docs/benchmarks/final-report-phases-0-8.md`（或新增一份覆盖 Phase 0-9 的报告）补充 Phase 9 一节和跨阶段结论。

**不包含：**
- 不覆盖 `oneof content` 的全部 8 个分支（image/audio/video/file/recall/system_event/merged_forward 之外的类型）——SBE 没有原生的 union/oneof 构造,真实场景需要按 message id 分发到不同 message 类型,但本次 fixture 从未用到这些分支,引入它们只会增加 schema 复杂度而不提升对比的公平性。
- **不做 Phase 8（畸形输入）的 SBE 对应物**。原因：Protobuf 的 parse 失败是"tag/length 自描述格式主动检测出错误"；SBE 的 decode 没有这种自校验机制——给一个声明了字段长度的 decoder 喂截断 buffer，它默认会读到声明长度之外的内存(未定义行为)，而不是返回"parse_ok=false"。要做一个"诚实"的对比,必须先自己写一层边界检查包装(每次访问前验证 offset+size ≤ buffer 实际长度),这本质上是在评测"我们自己加的防御代码"而不是"SBE 本身",超出了本次基准测试(评测现有实现)的范围,容易把工程引入未定义行为风险。Phase 9 分析文档里会明确写这一条结论(协议自描述性 vs 性能的取舍),但不会跑一个有 UB 风险的 benchmark 来"证明"它。
- **不做 Phase 5（序列化 API 开销）的 SBE 对应物**。原因：Protobuf 测的是"同一份数据,4 种不同的序列化 API(string/array/CodedStream)"之间的取舍;SBE 的 C++ 生成代码只有一种自然的用法(flyweight wrap 一个调用方提供的 buffer),没有等价的多 API 选择题,硬造一个"对比"会是无意义的稻草人。
- 不引入 schema 版本演进(`sinceVersion`)、不测多 schema 版本兼容性——这是 SBE 的一个重要特性,但跟"耗时对比"这个本次目标无关。

## 组件设计

### 1. `schema/chat.sbe.xml`

字段范围来自逐一核对 `src/message_fixtures.cpp`：

**`TextChatMessage`**(对应 `BuildTextMessage` / `BuildTextMessageWithId` / `BuildSparseTextMessage` / `BuildTextMessageWithMentionCount`)：
- 固定块字段(顺序固定,在 group/data 之前)：`messageId`(int64)、`conversationId`(int64)、`conversationType`(enum uint8，4 个值)、`senderId`(int64)、`seq`(int64)、`clientTimestampMs`(int64)、`serverTimestampMs`(int64)、`status`(enum uint8，7 个值)、`quotedMessageId`(int64, `presence="optional"`)、`quotedSenderId`(int64, `presence="optional"`)。
  - `quote` 在 protobuf 里是一个可选子消息(`BuildSparseTextMessage` 不设置它),这里没有用 SBE 的 composite presence(避免不必要的复杂度),而是把 `QuoteInfo` 的两个 int64 字段直接拍平进根字段,用 SBE 标准的 `presence="optional"` + null-value 哨兵表达"无 quote"；`content_preview` 拍平进下面的 var-length data,无 quote 时写空字符串。
- repeating group：`mentionedUserIds`(对应 `mentioned_user_ids`)，组内单一字段 `userId: int64`。
- var-length data(必须排在 group 之后，顺序固定)：`clientMsgId`(varStringEncoding)、`contentPreview`(varStringEncoding，quote 缺失时长度为 0)、`body`(varStringEncoding，对应 `text.body`)。

**`MergedForwardChatMessage`**(对应 `BuildMergedForwardMessage` / `BuildMergedForwardMessageWithItemCount`)：
- 固定块字段：`messageId`、`conversationId`、`conversationType`、`senderId`、`seq`、`clientTimestampMs`、`serverTimestampMs`、`status`（类型同上）。
- repeating group：`items`(对应 `merged_forward.items`)，组内固定字段 `messageId: int64`、`senderId: int64`、`timestampMs: int64`，组内 var-length data `body`(varStringEncoding，对应每个 `ForwardedItem.text.body`——fixture 里每个 item 都只用 `TextContent`,不需要 union)。
- 顶层 var-length data：`clientMsgId`(varStringEncoding)、`title`(varStringEncoding，对应 `merged_forward.title`)。

两个枚举类型：
```xml
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
```

`messageSchema` 用标准 `messageHeader` composite(`blockLength`/`templateId`/`schemaId`/`version`，均 `uint16`)、`groupSizeEncoding` composite(`blockLength: uint16` + `numInGroup: uint8`)、`varStringEncoding` composite(`length: uint32` + `varData: uint8[], characterEncoding="UTF-8"`)。`package` 设为单段标识符(如 `im_chat_sbe`)，避免 C++ namespace 嵌套的额外复杂度。`byteOrder="littleEndian"`。

### 2. CMake 改动

```cmake
find_package(Java REQUIRED)

set(SBE_TOOL_VERSION 1.38.1)
set(SBE_JAR ${CMAKE_CURRENT_BINARY_DIR}/sbe-all-${SBE_TOOL_VERSION}.jar)
file(DOWNLOAD
  https://repo1.maven.org/maven2/uk/co/real-logic/sbe-all/${SBE_TOOL_VERSION}/sbe-all-${SBE_TOOL_VERSION}.jar
  ${SBE_JAR}
  EXPECTED_HASH SHA1=ef7dd43a54a0269854ac2a296c2f6ba25edbaeff
)

set(SBE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated-sbe)
file(MAKE_DIRECTORY ${SBE_GENERATED_DIR})

add_custom_command(
  OUTPUT <实现阶段第一次手动跑一遍 codegen 后，把实际产出的文件列表填进来>
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
add_custom_target(generate_chat_sbe DEPENDS <同上>)

add_library(chat_sbe INTERFACE)  # 或 STATIC，取决于是否有 .cpp 产物，实现阶段确认
target_include_directories(chat_sbe INTERFACE ${SBE_GENERATED_DIR})
add_dependencies(chat_sbe generate_chat_sbe)
```

`proto_test`、`proto_bench` 都 `target_link_libraries(... PRIVATE chat_sbe)`。

### 3. `src/sbe_message_fixtures.h` / `.cpp`

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

每个函数内部：写 `messageHeader`，再 `wrapForEncode` 对应的 message，逐字段填入跟 `message_fixtures.cpp` 完全相同的值(同样的 `client-uuid-abc123`、`555`、`42`、`17`、`1750000000000` 等),返回 header+body 的总编码长度。`EncodeSparseTextMessageSbe` 不写 `quotedMessageId`/`quotedSenderId`(留给 SBE 自动填 null 哨兵)、`contentPreview` 写空字符串、不追加任何 `mentionedUserIds` group 条目(count=0)——对应 protobuf 那边"完全不设置这些字段"。

### 4. `bench.cpp` 新增用例(示意，完整列表在实现阶段按此模式展开)

```cpp
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
  for (auto _ : state) {
    // wrap header + body，逐字段读出（标量读入局部变量、group 整个迭代、
    // var-length data 拷进栈 buffer），全部包 DoNotOptimize。
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeTextSbe);
```

完整列表(均按此模式实现)：
`BM_EncodeTextSbe`/`BM_DecodeTextSbe`、`BM_EncodeSparseTextSbe`/`BM_DecodeSparseTextSbe`、`BM_EncodeSmallIdSbe`/`BM_DecodeSmallIdSbe`、`BM_EncodeLargeIdSbe`/`BM_DecodeLargeIdSbe`、`BM_EncodeMentionsSbe`/`BM_DecodeMentionsSbe`(`->Arg(1)->Arg(10)->Arg(100)->Arg(1000)`)、`BM_EncodeMergedForwardSbe`/`BM_DecodeMergedForwardSbe`、`BM_EncodeMergedItemsSbe`/`BM_DecodeMergedItemsSbe`(同样 4 档 Arg)、`BM_DecodeTextHeapAllocsSbe`(复用 `alloc_counter.h`，验证零分配)、`BM_ConcurrentEncodeTextSbe`/`BM_ConcurrentDecodeTextSbe`(`->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20)`)。

### 5. `proto_test` 新增 round-trip 校验

新增一个函数(例如 `RunSbeRoundTripChecks()`，在 `main.cpp` 里调用)，对 6 个 fixture 各自 encode 后立即 decode，用 `assert`(或现有 `proto_test` 用的校验方式，需先看 `main.cpp` 现有写法保持风格一致)逐字段比对，尤其要覆盖：sparse 场景下 `quotedMessageId`/`quotedSenderId` 读出的是否确实是 schema 定义的 null 值、`mentionedUserIds` 在 n=0/1/1000 时 group 迭代次数是否正确、`merged_forward.items` 在 n=1000 时每个 item 的 `body` 内容是否正确还原。

## 验证标准

- `cmake -S . -B build && cmake --build build -j$(nproc)`：编译通过(包括新增的 `java -jar` codegen 步骤成功跑通、生成的头文件被正确找到)。
- `./build/proto_test`：现有 Protobuf 正确性测试 + 新增 SBE round-trip 校验全部通过。
- `./build/proto_bench`：所有新增 SBE benchmark 跑出结果；`BM_DecodeTextHeapAllocsSbe` 的 `allocs_per_iter` 应接近 0(验证"flyweight decode 零堆分配"这个估算，如果不是,说明哪里意外触发了堆分配,需要先排查再继续，不能直接报告一个"不为零"的数字当作"基本正确"）。
- `BM_EncodeSmallIdSbe`/`BM_EncodeLargeIdSbe` 耗时和体积应几乎完全相同(验证"SBE 定长编码不受数值大小影响"这个估算)。
- 分析文档逐行对照 Phase 1/2/3/4/7 的 Protobuf 数字与本阶段 SBE 数字，给出真实倍数(不是停留在此前估算的"5-30x"区间，要写出这次实测的具体数字)，并明确指出实测结果跟此前纯估算的吻合/偏离程度。

## 后续

完成并验证后，更新 `docs/benchmarks/final-report-phases-0-8.md`(改名为覆盖 Phase 0-9，或新增一节，实现阶段视篇幅决定),把 Phase 9 的结论并入"Cross-cutting takeaways"。
