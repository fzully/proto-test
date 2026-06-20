# Protobuf Benchmark Phase 10 — JSON 库横向对比 Design

日期：2026-06-20

> 本 spec 在用户明确要求"加入 JSON 编解码性能对比，先做主流 JSON 库横向对比再选最强者跟 Protobuf PK"并经过逐项澄清后编写。用户已确认按 spec → plan → implement → verify → commit 的既有流程推进，且明确把"横向对比"和"胜者 vs Protobuf PK"拆成两个独立 phase（Phase 10 / Phase 11），因为 PK 的对手要等本阶段结果出来才能确定。

## 背景

Phase 1-9 测的是 Protobuf 与 SBE 的各项开销。用户想再加入 JSON 作为第三种编码方式的对比对象，但 JSON 本身没有唯一的"标准实现"——C++ 生态里有多个主流库，各自的设计取向差异很大（DOM 易用型、SAX 高性能型、新一代全能型、老派 C 库）。在跟 Protobuf PK 之前，先把"哪个 JSON 库最快"这个问题单独测清楚，避免选错对比对象导致 Phase 11 的结论站不住脚。

`proto/chat.proto` 当前设置了 `option optimize_for = LITE_RUNTIME;`（见 Phase 9 之后的改动），生成的 `ChatMessage` 没有 `Descriptor`/`Reflection`，因此 protobuf 官方的 `google::protobuf::util::MessageToJsonString`/`JsonStringToMessage`（依赖反射）在本仓库不可用。本阶段及 Phase 11 的 JSON 编解码都通过手写映射实现，不依赖任何 protobuf 反射机制。

## 目标

测出 4 个主流 C++ JSON 库——**nlohmann/json**（DOM 易用型）、**RapidJSON**（SAX 高性能型）、**yyjson**（现代 C，号称编解码对称地快）、**cJSON**（老派 C 风格 DOM 库，作为技术演进的基线参照）——在两种现有逻辑消息内容（`BuildTextMessage`、`BuildMergedForwardMessage`）下的：

1. encode（构造 JSON 文本）耗时
2. decode（解析并读出全部字段）耗时
3. 编码后的 JSON 文本体积

按 encode+decode 总耗时排序，选出最快的一个库，作为 Phase 11（vs Protobuf PK）的对比对象。

simdjson 经讨论后**不参赛**：它只长于 decode（SIMD 加速解析），没有对称的高性能 writer，硬塞一个 encode 实现（无论是自己写还是借用别的库）都会让"encode+decode 总耗时"这个排名指标失去意义，所以直接排除，不在结果里出现。

## 范围

**包含：**

- CMake 新增：用 `FetchContent` 拉取 nlohmann/json、RapidJSON、yyjson、cJSON 四个库，固定 tag + `GIT_SHALLOW`，关闭各自的测试/示例/fuzzing 构建选项。四个库只挂在 `proto_bench`、`proto_test` 上，不污染 `chat_proto`/`chat_sbe`。
- 新增 4 对 fixture 文件（命名仿照 `sbe_message_fixtures`，见"组件设计"），每对文件用独立 namespace 隔离，避免符号冲突：
  - `src/json_nlohmann_fixtures.{h,cpp}` → `namespace json_nlohmann`
  - `src/json_rapidjson_fixtures.{h,cpp}` → `namespace json_rapidjson`
  - `src/json_yyjson_fixtures.{h,cpp}` → `namespace json_yyjson`
  - `src/json_cjson_fixtures.{h,cpp}` → `namespace json_cjson`
- 新增 `src/json_message_decoded.h`：定义 `DecodedTextMessage`/`DecodedMergedForwardMessage` 两个 plain struct，4 个库的 decode 函数共用，供 `proto_test` 做逐字段断言。
- 统一的 JSON 形状与编码约定（见"组件设计"第 2 节），4 个库都按同一份字段表手写映射，保证横向对比公平。
- `bench.cpp` 新增 16 个显式 benchmark 函数（4 库 × 2 形状 × encode/decode），延续现有风格（不用宏批量生成），体积走 `state.counters["bytes"]` 惯例。
- `proto_test` 新增 round-trip 正确性校验：每个库 × 每种形状都 encode 后立即 decode，逐字段断言与 fixture 输入一致。
- 新增 `results/phase10-2026-06-20.json` + `docs/benchmarks/phase10-json-shootout-analysis.md`（含排名表、胜者结论、simdjson 排除原因说明）。
- 把 `docs/benchmarks/final-report-phases-0-9.md` 重命名为 `final-report-phases-0-10.md`（沿用 Phase 9 时的命名升级模式），补一节 Phase 10 摘要；同步更新 `.zh-CN.md` 镜像。

**不包含：**

- **不做 simdjson**——原因见上文"目标"一节，没有对称 writer，参赛会让排名指标失真。
- **不做规模扫描**（`mentioned_user_ids`/`merged_forward.items` 在 n=1/10/100/1000 下的扫描，对应 Phase 3）、**不做并发**（对应 Phase 7）、**不做堆分配计数**（对应 Phase 4）、**不做畸形输入测试**（对应 Phase 8）。这些问题留给 Phase 11——到那时只剩 1 个胜者库，工作量可控，且价值更大（是跟 Protobuf 的最终对比，不是 4 库内部排名）。如果 Phase 11 的结果显示需要更深入的规模/并发数据，会在 Phase 11 的 spec 里单独规划，不在本阶段提前做。
- **不做 `sparse_text`/`small_id`/`large_id` 等 Phase 2 对应物**——只用 `BuildTextMessage`/`BuildMergedForwardMessage` 两种形状，足够区分 4 个库的相对快慢。
- **不引入 oneof 判别字段或 union 包装**——两种 fixture 各自固定是 TextContent / MergedForwardContent，直接拍平成对应的 JSON 字段，不需要表达"内容类型"的判别字段。
- **不使用 protobuf 官方 JSON 工具**——`chat.proto` 是 `LITE_RUNTIME`，没有反射，`MessageToJsonString`/`JsonStringToMessage` 不可用；4 个库的映射都是手写代码直接读 `ChatMessage` 的字段或写入对应 plain struct，不经过 protobuf 的任何 JSON 转换设施。

## 组件设计

### 1. JSON 形状与编码约定

字段范围严格对应 `message_fixtures.cpp` 里 `BuildTextMessage`（即 `BuildTextMessageWithId(1001)`）和 `BuildMergedForwardMessage` 实际填充的字段，不覆盖 `chat.proto` 里这两个 builder 从未填充的字段——跟 Phase 9 SBE schema 的取舍一致。

**通用约定：**
- 字段名用 camelCase（如 `messageId` 而不是 `message_id`），对齐 protobuf 官方 JSON mapping 习惯。
- int64 字段序列化为 JSON 字符串（不是 JSON number），避免 JSON number 是 double、超过 2^53 丢精度的问题，对齐官方 JSON mapping。
- enum 字段（`conversationType`/`status`）序列化为字符串名（如 `"CONVERSATION_TYPE_GROUP"`、`"MESSAGE_STATUS_SENT"`，沿用 `chat.proto` 里枚举值的原始名字，不做额外的名字简化）。
- 字符串字段（含 UTF-8 中文文本如 `"原消息预览文本"`）直接作为 JSON 字符串值，按各库默认的 UTF-8/转义规则处理，不做额外的编码层。

**`TextMessage` 形状**（对应 `BuildTextMessage`）字段表：

| JSON 字段 | 来源 | 类型/编码 |
| --- | --- | --- |
| `messageId` | `message_id` | int64 字符串，值 `"1001"` |
| `clientMsgId` | `client_msg_id` | 字符串，`"client-uuid-abc123"` |
| `conversationId` | `conversation_id` | int64 字符串，`"555"` |
| `conversationType` | `conversation_type` | 枚举字符串，`"CONVERSATION_TYPE_GROUP"` |
| `senderId` | `sender_id` | int64 字符串，`"42"` |
| `seq` | `seq` | int64 字符串，`"17"` |
| `clientTimestampMs` | `client_timestamp_ms` | int64 字符串，`"1750000000000"` |
| `serverTimestampMs` | `server_timestamp_ms` | int64 字符串，`"1750000000050"` |
| `status` | `status` | 枚举字符串，`"MESSAGE_STATUS_SENT"` |
| `quotedMessageId` | `quote.quoted_message_id` | int64 字符串，`"998"` |
| `quotedSenderId` | `quote.quoted_sender_id` | int64 字符串，`"7"` |
| `contentPreview` | `quote.content_preview` | 字符串，`"原消息预览文本"` |
| `mentionedUserIds` | `mentioned_user_ids` | JSON 数组，每个元素 int64 字符串，`["7", "9"]` |
| `body` | `text.body` | 字符串，`"Hello, this is a test message."` |

**`MergedForwardMessage` 形状**（对应 `BuildMergedForwardMessage`）字段表：

| JSON 字段 | 来源 | 类型/编码 |
| --- | --- | --- |
| `messageId` | `message_id` | int64 字符串，`"2002"` |
| `clientMsgId` | `client_msg_id` | 字符串，`"client-uuid-def456"` |
| `conversationId` | `conversation_id` | int64 字符串，`"777"` |
| `conversationType` | `conversation_type` | 枚举字符串，`"CONVERSATION_TYPE_SINGLE"` |
| `senderId` | `sender_id` | int64 字符串，`"42"` |
| `seq` | `seq` | int64 字符串，`"18"` |
| `clientTimestampMs` | `client_timestamp_ms` | int64 字符串，`"1750000001000"` |
| `serverTimestampMs` | `server_timestamp_ms` | int64 字符串，`"1750000001050"` |
| `status` | `status` | 枚举字符串，`"MESSAGE_STATUS_SENT"` |
| `title` | `merged_forward.title` | 字符串，`"群聊的聊天记录"` |
| `items` | `merged_forward.items` | JSON 数组，每个元素见下表 |

`items` 数组元素（对应每个 `ForwardedItem`）：

| JSON 字段 | 来源 | 类型/编码 |
| --- | --- | --- |
| `messageId` | `item.message_id` | int64 字符串 |
| `senderId` | `item.sender_id` | int64 字符串 |
| `timestampMs` | `item.timestamp_ms` | int64 字符串 |
| `body` | `item.text.body` | 字符串 |

两个 item（`message_id=101/102`、`sender_id=7/9`、`timestamp_ms=1749999999000/1749999999500`、`body="第一条被转发的消息"/"第二条被转发的消息"`）。

### 2. CMake 改动

四个版本号均已通过 `git ls-remote --tags` 逐一确认是真实存在的标签（不是猜测），且都是确认时各仓库的最新稳定标签：nlohmann/json `v3.12.0`、RapidJSON `v1.1.0`（该库自 2016 年起未再发布新 tag，`v1.1.0` 即当前最新）、yyjson `0.12.0`、cJSON `v1.7.19`。

```cmake
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)  # 提供 nlohmann_json::nlohmann_json

FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG v1.1.0
  GIT_SHALLOW TRUE
)
set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidjson)  # header-only，用 include 目录而非 link target

FetchContent_Declare(
  yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(yyjson)  # 提供 yyjson target

FetchContent_Declare(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.19
  GIT_SHALLOW TRUE
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)  # 提供 cjson target
```

具体的目标名字（如 RapidJSON 是否产出一个可链接的 interface target，还是只能手动 `target_include_directories`）以实现阶段第一次跑 `cmake -S . -B build` 后的实际产出为准，本 spec 不假设。`proto_bench`、`proto_test` 都 `target_link_libraries(... PRIVATE nlohmann_json::nlohmann_json yyjson cjson)`（RapidJSON 视产出形式用 link 或 include 目录）。

### 3. `src/json_message_decoded.h`

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

### 4. 4 对 fixture 文件

每对文件提供 4 个函数，签名贴合该库的自然用法：

```cpp
// json_<lib>_fixtures.h（命名空间 json_<lib>）
std::string EncodeTextMessageJson();
DecodedTextMessage DecodeTextMessageJson(const std::string& json_text);
std::string EncodeMergedForwardMessageJson();
DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text);
```

- `json_nlohmann`：encode 用 `nlohmann::json` 对象逐字段赋值后 `.dump()`；decode 用 `nlohmann::json::parse()` 后逐字段 `.get<...>()`／字符串转 int64（`std::stoll`）。
- `json_rapidjson`：encode 用 `rapidjson::Writer<rapidjson::StringBuffer>` 顺序写 key/value；decode 用 `rapidjson::Document::Parse()` 后逐字段读取（int64 字符串同样手动 `std::stoll`）。
- `json_yyjson`：encode 用 `yyjson_mut_doc`/`yyjson_mut_obj_add_*` 系列 API 后 `yyjson_mut_write`；decode 用 `yyjson_read()` 后 `yyjson_obj_get`/`yyjson_get_str` 系列 API。
- `json_cjson`：encode 用 `cJSON_CreateObject`/`cJSON_AddStringToObject`/`cJSON_AddItemToArray` 系列 API 后 `cJSON_PrintUnformatted`（必须是无格式化的 compact 输出，不能用 `cJSON_Print` 的带缩进版本——否则体积和耗时都不可比）；decode 用 `cJSON_Parse()` 后 `cJSON_GetObjectItem`/`cJSON_GetStringValue` 系列 API，最后 `cJSON_Delete()` 释放。

4 个库的 encode 函数都内部硬编码"组件设计第 1 节"字段表里的固定值（跟 `message_fixtures.cpp` 一一对应，不接收参数），decode 函数把所有字段都实际读出并填入 `DecodedTextMessage`/`DecodedMergedForwardMessage`，避免编译器把"什么都没读"的 decode 优化掉。所有 4 个库的输出都必须是 compact（无多余空白/缩进）格式，因为线上传输只会用 minified JSON，pretty-print 只是徒增字符串拼接开销，没有现实意义。

### 5. `bench.cpp` 新增用例

16 个显式函数，命名 `BM_Encode{Text,MergedForward}Json{Nlohmann,RapidJson,Yyjson,CJson}` / `BM_Decode{Text,MergedForward}Json{Nlohmann,RapidJson,Yyjson,CJson}`，体积走 `state.counters["bytes"]` 惯例：

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
```

其余 14 个函数按此模式展开（4 库 × {Text, MergedForward} × {Encode, Decode}）。

### 6. `proto_test` 新增 round-trip 校验

新增一个函数（如 `RunJsonRoundTripChecks()`，在 `main.cpp` 里调用），对 4 个库 × 2 种形状各自 encode 后立即 decode，逐字段断言与 fixture 输入一致（保持与现有 `proto_test`/Phase 9 SBE round-trip 校验一致的断言风格）。重点覆盖：int64 字符串往返（含 `quotedMessageId`/`quotedSenderId` 这类可能跨字段拼错的值）、`mentionedUserIds`/`items` 数组长度和元素顺序、UTF-8 中文字符串（`"原消息预览文本"`、`"群聊的聊天记录"`等）往返不丢字节。

## 验证标准

- `cmake -S . -B build && cmake --build build -j$(nproc)`：编译通过，4 个新 FetchContent 依赖成功拉取并链接。
- `./build/proto_test`：现有正确性测试 + 新增 16 组（4 库 × 2 形状）JSON round-trip 校验全部通过。
- `./build/proto_bench --benchmark_filter="Json"`：16 个新 benchmark 全部跑出结果，每个的 `bytes` counter 数值合理（同一形状下 4 个库体积应接近，只有 key 顺序/转义细节差异，不应有数量级差异——如果出现数量级差异，说明某个库的映射写漏了字段，需要先排查再继续）。
- 分析文档按 encode+decode 总耗时排序给出 4 库排名表，明确写出胜者，并说明 simdjson 被排除的原因。

## 后续

完成并验证后，更新 `docs/benchmarks/final-report-phases-0-9.md`（重命名为覆盖 Phase 0-10）并同步 `.zh-CN.md` 镜像，把 Phase 10 的排名结论并入"Cross-cutting takeaways"。随后基于本阶段选出的胜者库，开 Phase 11 spec：胜者 vs Protobuf 的 encode/decode 耗时与体积 PK（对齐 Phase 1/9 的范围）。
