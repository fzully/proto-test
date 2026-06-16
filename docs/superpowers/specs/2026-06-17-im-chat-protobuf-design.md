# IM 聊天消息 Protobuf 序列化试验 — 设计文档

日期：2026-06-17

## 目标

验证 protobuf 基础序列化/反序列化，编码使用 C++20。Schema 不随便设计，按贴合生产 IM 场景的方式来定义聊天消息字段，覆盖单聊/群聊/频道、文本+媒体+系统事件、消息引用、批量转发（逐条+合并）等机制。

## 技术栈与依赖

- C++20，CMake 构建
- protobuf 通过 CMake `FetchContent` 拉源码构建（不依赖系统包），需要：
  - `cmake_minimum_required(3.16)`，`CXX_STANDARD 20`
  - `FetchContent_Declare(protobuf ...)`，指向一个稳定 release tag
  - 设置 `protobuf_ABSL_PROVIDER=module`，让 protobuf 自动拉取并构建其依赖的 Abseil（v22+ 起 protobuf 强制依赖 Abseil）
  - `protobuf_BUILD_TESTS=OFF`，只构建 `libprotobuf` 本体，不需要 protoc 插件 / gRPC
  - 用 FetchContent 拉下来的 `protoc` 把 `.proto` 编译为 `.pb.h` / `.pb.cc`

## 目录结构

```
proto-test/
  CMakeLists.txt
  proto/
    chat.proto       # 消息schema
  src/
    main.cpp          # 序列化/反序列化验证程序
```

## .proto Schema

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
  string content_preview = 3;   // 发送时刻的内容摘要快照
}

// ---- Forward ----

message ForwardInfo {
  int64 original_message_id = 1;
  int64 original_sender_id = 2;
  int64 original_conversation_id = 3;
  int32 forward_depth = 4;       // 转发链深度
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
  // identity / idempotency
  int64 message_id = 1;          // 服务端分配的全局唯一id (snowflake)
  string client_msg_id = 2;      // 客户端生成的UUID，ack前用于幂等去重/断线重发

  // routing
  int64 conversation_id = 3;
  ConversationType conversation_type = 4;
  int64 sender_id = 5;

  // ordering / time
  int64 seq = 6;                 // 会话内单调递增序号，排序唯一依据
  int64 client_timestamp_ms = 7;
  int64 server_timestamp_ms = 8;

  // delivery state
  MessageStatus status = 9;

  // rich interaction
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

  map<string, string> extra = 30; // 扩展位
}
```

### 关键设计决策

1. **`message_id` vs `client_msg_id` 分离**：服务端确认后的全局真相 vs 客户端发送前生成的本地UUID，用于断线重连/重试场景下的幂等去重。
2. **`seq` 独立于时间戳**：时钟漂移、网络乱序，排序必须靠服务端单调序号，不依赖timestamp。
3. **`oneof content` 做多态内容**：强类型而不是塞 `bytes`/`Any`，每种内容类型有专属schema，编译期可检查字段。
4. **字段编号分段**：1-19信封字段，20-29内容oneof，30+扩展位，给每段留增长空间。
5. **`QuoteInfo` 带内容快照**：引用展示不依赖回查原消息，原消息被撤回/编辑/未同步都不影响引用展示。
6. **`ForwardInfo` 支持逐条转发**：每条转发消息独立携带原始消息/发送者/会话信息和转发链深度。
7. **`MergedForwardContent` 支持合并转发**：把多条消息打包成一条消息的一种 content 类型，`ForwardedItem` 比完整 `ChatMessage` 更轻量（不含 seq/status 等会话态字段），合并卡片本身也可再次被转发。
8. **enum 的 `0` 值都是 `UNSPECIFIED`**：proto3字段未显式设置时默认为0，需要和"明确设置为某个有效值"区分开。
9. **`extra` map 兜底**：小字段扩展不必每次都改schema重新发版本。

## 测试程序计划 (src/main.cpp)

1. 构造一条单聊文本 `ChatMessage`（含 mentioned_user_ids、quote）
2. `SerializeToString` 序列化到 `std::string`
3. 用新对象 `ParseFromString` 反序列化
4. 对比关键字段，确认 round-trip 正确
5. 额外构造一条 `merged_forward` 类型的消息，验证 oneof + repeated 嵌套message的序列化往返

## 构建与运行

```
cmake -S . -B build
cmake --build build -j
./build/proto_test
```

## 范围说明

本次试验只验证 protobuf 序列化/反序列化本身是否按预期工作；不包含网络传输、持久化存储、实际IM服务端/客户端实现。
