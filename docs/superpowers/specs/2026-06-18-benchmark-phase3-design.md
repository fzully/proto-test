# Protobuf Benchmark Phase 3 — Scalability (Repeated Field Size Sweep) Design

日期：2026-06-18

> 本 spec 由 Claude 在用户授权自主执行的情况下编写（用户已批准整体 8 阶段路线图与 brainstorming→spec→plan→实现 的标准流程，本阶段起不再逐项征求用户确认，设计决策由 Claude 自行判断并在此记录）。

## 背景

Phase 0-2（见 `docs/superpowers/specs/2026-06-17-benchmark-phase0-1-design.md`、`docs/superpowers/specs/2026-06-18-benchmark-phase2-design.md`）已经测过固定大小消息的吞吐延迟/体积/填充率/数值编码效率。Phase 3 测的是**规模可扩展性**：固定消息结构，只把某个 repeated 字段的长度当作自变量扫描，看序列化/反序列化耗时和体积是否随长度线性增长，还是有额外的非线性开销（比如内存重分配、vector resize）。

schema 里有两个天然的 repeated 字段适合做这个实验：
- `ChatMessage.mentioned_user_ids`（`repeated int64`，扁平的数值数组）
- `MergedForwardContent.items`（`repeated ForwardedItem`，repeated 嵌套 message）

这两个分别代表"repeated 标量"和"repeated message"两种不同的内部表示，扫描两者能看出 protobuf 对这两类 repeated 字段的扩展性是否有差异。

## 目标

测出 `mentioned_user_ids` 和 `MergedForwardContent.items` 在长度为 1/10/100/1000 时，序列化/反序列化耗时（ns/iter）和体积（bytes）的变化曲线，判断增长是否符合线性预期（即 `时间(n) ≈ a + b·n`，`体积(n) ≈ a + b·n`）。

## 范围

**包含：**
- 新增两个参数化 fixture：`BuildTextMessageWithMentionCount(int mention_count)`、`BuildMergedForwardMessageWithItemCount(int item_count)`。
- 用 Google Benchmark 自带的参数化机制（`BENCHMARK(...)->Arg(N)`，而不是手写 8 个独立函数）新增 4 个 benchmark 函数，每个在 1/10/100/1000 四个长度上各跑一组。
- 新增 `results/phase3-2026-06-18.json` + `docs/benchmarks/phase3-scalability-analysis.md`。

**不包含：**
- 不测除 `mentioned_user_ids`/`MergedForwardContent.items` 外的其他 repeated 字段（schema 里没有其他有意义的 repeated 字段）。
- 不涉及 Phase 4-8（内存/Arena、API 开销、CPU 微架构、并发、解析失败路径）。
- 不新增 CMake target/依赖——复用现有 `proto_bench` 基础设施。

## 组件设计

### 1. `message_fixtures.h` / `message_fixtures.cpp` 新增内容

```cpp
im::chat::v1::ChatMessage BuildTextMessageWithMentionCount(int mention_count);
im::chat::v1::ChatMessage BuildMergedForwardMessageWithItemCount(int item_count);
```

- **`BuildTextMessageWithMentionCount(int mention_count)`**：复用 `BuildTextMessageWithId(1001)` 同样的信封字段和 quote，但 `mentioned_user_ids` 替换为 `mention_count` 个合成的 user id（从 `1` 递增到 `mention_count`），而不是固定的两个 `[7, 9]`。`mention_count=0` 时不调用 `add_mentioned_user_ids`。
- **`BuildMergedForwardMessageWithItemCount(int item_count)`**：复用 `BuildMergedForwardMessage()` 同样的信封字段和 `title`，但 `items` 替换为 `item_count` 个合成的 `ForwardedItem`（每个用简单递增的 `message_id`/`sender_id`/`timestamp_ms` 和固定的短文本 body，避免文本长度本身引入额外变量）。

### 2. `bench.cpp` 新增用例

4 个参数化 benchmark 函数，用 `state.range(0)` 读取当前的长度参数：

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
```

同样模式新增 `BM_ParseMentions`、`BM_SerializeMergedItems`、`BM_ParseMergedItems`，分别用 `BuildTextMessageWithMentionCount`/`BuildMergedForwardMessageWithItemCount`，Parse 版本在循环外先序列化好 `bytes`。`->Arg(1)->Arg(10)->Arg(100)->Arg(1000)` 四个参数都加在每个函数上。

Google Benchmark 会自动把每个 `(函数, Arg)` 组合跑成一行，名字形如 `BM_SerializeMentions/1`、`BM_SerializeMentions/1000`，JSON 输出里 `state.range(0)` 的值会出现在 benchmark 名字里，不需要额外记录。

### 3. 运行与数据产出

```
cmake --build build -j20
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase3-2026-06-18.json
```

不需要重新跑 `cmake -S . -B build`。

跑完后整理进 `docs/benchmarks/phase3-scalability-analysis.md`：
- 两张表（mentioned_user_ids、merged_forward.items），每张 4 行（n=1/10/100/1000），列为耗时（ns/iter）、体积（bytes）。
- 简单结论：体积增长是否线性（用 n=1000 vs n=1 的体积差除以 999，跟单个元素的预期编码体积对比）；耗时增长是否线性，还是在大 n 时出现非线性跳变（暗示内存重分配等开销）。

## 验证标准

- `cmake --build build -j20`：编译通过，无新增警告。
- `./build/proto_test`：现有正确性测试全部通过（新增 fixture 不影响 `BuildTextMessage()`/`BuildMergedForwardMessage()` 的行为）。
- `./build/proto_bench`：新增的 4×4=16 个 benchmark 行全部跑完，体积随 n 单调递增。
- `results/phase3-2026-06-18.json` 生成，`docs/benchmarks/phase3-scalability-analysis.md` 写入实际数据和线性度结论。

## 后续

Phase 4-8 在本 phase 落地后各自单独走相同流程，本文档不预先设计。
