# Protobuf Benchmark Phase 2 — Field-Fill-Rate + Numeric-Encoding-Efficiency Design

日期：2026-06-18

## 背景

Phase 0+1（见 `docs/superpowers/specs/2026-06-17-benchmark-phase0-1-design.md`）搭好了 `proto_bench` 基础设施（Google Benchmark + 共享的 `chat_proto` 静态库 + `message_fixtures.h/.cpp` 共享测试数据），并测出了 `text`/`merged_forward` 两种消息类型的序列化/反序列化吞吐延迟和体积，结论已写入 `docs/benchmarks/phase1-throughput-size-analysis.md`。

完整的 8 阶段 benchmark 路线图里，本 spec 覆盖 **Phase 2：字段填充率 + 数值编码效率**。这两个维度本质都是"固定消息类型，只变输入数据的某个特征，看 size/速度差异"，适合放在一起做：

- **字段填充率**：proto3 不序列化零值/未设置字段，但 Phase 1 的 `BuildTextMessage()` 几乎所有可选字段都填满了（quote、mentioned_user_ids、forward_info）。真实 IM 场景里，多数普通文本消息不会带这些可选字段。需要一个"稀疏"样本和现有"全填充"样本做对比。
- **数值编码效率**：protobuf 的 int64 用 varint 编码，编码后字节数随数值大小变化（1 字节 vs 最多 10 字节）。`message_id` 这种字段在真实场景下是 snowflake 量级的大数，但 Phase 1 fixture 里的 `message_id=1001` 只是个小数，没有体现这个效应。

## 目标

1. 测出"稀疏填充"和"全填充"两种 `text` 消息在序列化/反序列化耗时和体积上的差异，验证 proto3 零值不序列化这个特性在真实场景下的收益有多大。
2. 测出 `message_id` 取小值（如 `1`）和取 snowflake 量级大值（如 `1950123456789012345`）时，varint 编码对序列化/反序列化耗时和体积的影响。

## 范围

**包含：**
- 新增两个共享 fixture 函数：`BuildSparseTextMessage()`、`BuildTextMessageWithId(int64_t message_id)`。
- 在现有 `proto_bench`（`src/bench.cpp`）里新增 6 个 benchmark 函数，复用 Phase 1 已验证的"计时循环外构造，循环内只测目标操作，`bytes` counter 记录体积"模式。
- 字段填充率对比直接复用 Phase 1 已有的 `BM_SerializeText`/`BM_ParseText`（全填充基线），不重新实现。
- 新增 `results/phase2-2026-06-18.json` + `docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md`。

**不包含：**
- 不新增 CMake target、不引入新依赖——完全复用 Phase 0+1 的 `proto_bench` 基础设施。
- 不涉及 Phase 3-8（可扩展性、内存/Arena、API 开销、CPU 微架构、并发、解析失败路径）。
- 不测试 `merged_forward` 类型的填充率/数值编码——本阶段只用 `text` 类型，保持单一变量对比的干净性。

## 组件设计

### 1. `message_fixtures.h` / `message_fixtures.cpp` 新增内容

在现有声明基础上追加两个函数：

```cpp
im::chat::v1::ChatMessage BuildSparseTextMessage();
im::chat::v1::ChatMessage BuildTextMessageWithId(int64_t message_id);
```

- **`BuildSparseTextMessage()`**：只填信封必需字段（`message_id`、`client_msg_id`、`conversation_id`、`conversation_type`、`sender_id`、`seq`、`client_timestamp_ms`、`server_timestamp_ms`、`status`）+ `text.body`，不设置 `quote`、`mentioned_user_ids`、`edited`、`forward_info`、`extra`（保持 proto3 默认值，不显式调用对应的 setter）。除字段填充程度外，取值跟 `BuildTextMessage()` 尽量保持一致（同样的 `message_id=1001`、`conversation_id=555` 等），确保跟"全填充"基线之间只有"是否填充可选字段"这一个变量不同。
- **`BuildTextMessageWithId(int64_t message_id)`**：内部逻辑等同于 `BuildTextMessage()`（同样的 `client_msg_id`/`conversation_id`/quote/mentioned_user_ids/text body 等），唯一区别是 `message_id` 由参数传入而不是硬编码 `1001`。`bench.cpp` 会用 `1`（小值）和 `1950123456789012345`（snowflake 量级，19 位、接近 `int64` 上限）分别调用它，只变这一个变量。为避免和 `BuildTextMessage()` 产生重复代码，`BuildTextMessage()` 内部改为调用 `BuildTextMessageWithId(1001)`。

### 2. `bench.cpp` 新增用例

6 个新 benchmark 函数，跟 Phase 1 的 4 个函数风格一致（fixture 构造放在计时循环外，循环内只测目标操作，`state.counters["bytes"]` 记录体积）：

- `BM_SerializeSparseText` / `BM_ParseSparseText`：用 `BuildSparseTextMessage()`，跟现有的 `BM_SerializeText`/`BM_ParseText`（全填充）对比。
- `BM_SerializeSmallId` / `BM_ParseSmallId`：用 `BuildTextMessageWithId(1)`。
- `BM_SerializeLargeId` / `BM_ParseLargeId`：用 `BuildTextMessageWithId(1950123456789012345)`。

### 3. 运行与数据产出

```
cmake --build build -j20
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase2-2026-06-18.json
```

不需要重新跑 `cmake -S . -B build`（CMake 结构不变，只加了源码里的函数）。

跑完后把 JSON 结果整理进 `docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md`，至少包含：
- 稀疏 vs 全填充 `text` 消息的序列化/反序列化耗时（ns/iter）、吞吐（ops/sec）、体积（bytes）对比表
- 小 ID vs 大 ID 的同类对比表
- 简单结论：稀疏填充节省了多少体积/耗时；大数值 ID 的 varint 编码额外开销有多大，是否符合预期（int64 varint 最多 10 字节，`1950123456789012345` 编码后预期占用接近上限）

## 验证标准

- `cmake --build build -j20`：`proto_test`、`proto_bench` 都编译通过，无新增编译警告。
- `./build/proto_test`：现有正确性测试全部通过（证明新增 fixture 函数没有破坏 `chat.proto` 之外的任何东西；`BuildTextMessage()` 改为委托给 `BuildTextMessageWithId(1001)` 后行为不变）。
- `./build/proto_bench`：除 Phase 1 的 4 个 benchmark 外，新增的 6 个 benchmark 全部跑完，产出非零的耗时和体积数据，且体积数据符合预期方向（稀疏 < 全填充；大 ID 的序列化体积 > 小 ID）。
- `results/phase2-2026-06-18.json` 文件生成，`docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md` 写入实际数据和结论。

## 后续

Phase 3-8 在本 phase 落地后各自单独走 brainstorming 流程产出独立 spec，不在本文档预先设计。
