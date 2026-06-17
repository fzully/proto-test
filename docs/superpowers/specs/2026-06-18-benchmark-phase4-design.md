# Protobuf Benchmark Phase 4 — Memory Allocations / Arena Comparison Design

日期：2026-06-18

> 本 spec 由 Claude 在用户授权自主执行的情况下编写（用户已批准整体路线图，本阶段起不再逐项征求确认，设计决策自行判断并记录于此）。

## 背景

Phase 0-3 测的都是耗时和体积。Phase 4 测**堆分配次数和字节数**，并对比开启 `google::protobuf::Arena` 后的差异——这是 protobuf 官方文档里推荐的高频场景优化手段：用一个长期存活的 Arena，反复在上面构造/解析消息，避免每条消息的每个子字段都单独调用一次堆分配。

protobuf 的 Arena 收益主要体现在**构造/解析**侧：解析时每多一个子 message 字段（这里是 `QuoteInfo` quote）、每多一个字符串字段，默认堆分配路径下都要单独 `new` 一次；Arena 路径下这些子对象从 arena 的大块内存里"bump 分配"，只有 arena 自身需要扩容时才会真正调用一次系统分配器。序列化侧（写入 `std::string` buffer）不受消息本身是否 arena 分配影响，所以本阶段聚焦反序列化（parse）侧的对比。

## 目标

测出反序列化同一条 `text` 消息（复用 Phase 1 的 `BuildTextMessage()`）时：
1. 用普通堆分配的 `ChatMessage parsed;` 反复 parse，每次 parse 平均消耗多少次堆分配、多少字节。
2. 用一个长期存活、复用的 `Arena` 反复 `Arena::Create<ChatMessage>(&arena)` 再 parse，每次 parse 平均消耗多少次堆分配、多少字节。
3. 对比两者的耗时和分配次数差异。

## 范围

**包含：**
- 新增一个仅供 `proto_bench` 使用的全局分配计数设施（覆写 `operator new`/`operator delete`，统计调用次数和字节数），放在新文件 `src/alloc_counter.h`/`.cpp`，只编进 `proto_bench` target（不影响 `proto_test`/`chat_proto`，因为是两个独立可执行文件，互不干扰）。
- CMake 改动：把 `src/alloc_counter.cpp` 加进 `proto_bench` 的 source list（其他 target 不变）。这是本阶段唯一允许的 CMake 改动。
- 新增 2 个 benchmark：`BM_ParseTextHeapAllocs`（默认堆分配）、`BM_ParseTextArenaAllocs`（Arena 分配），每个同时报告耗时（Google Benchmark 自动列）和自定义 counter `allocs_per_iter`/`bytes_per_iter`。
- 新增 `results/phase4-2026-06-18.json` + `docs/benchmarks/phase4-memory-arena-analysis.md`。

**不包含：**
- 不测序列化侧的分配次数（理由见背景：Arena 收益主要在解析/构造侧，序列化写 `std::string` 不受消息分配方式影响）。
- 不测 `merged_forward` 类型——复用 Phase 1 的 `text` fixture，保持跟其他 phase 一致的基线。
- 不涉及 Phase 5-8（API 开销、CPU 微架构、并发、解析失败路径）。

## 组件设计

### 1. `src/alloc_counter.h` / `.cpp`（新文件，只编进 `proto_bench`）

```cpp
// alloc_counter.h
#ifndef PROTO_TEST_ALLOC_COUNTER_H_
#define PROTO_TEST_ALLOC_COUNTER_H_

#include <cstdint>

void ResetAllocCounters();
int64_t GetAllocCount();
int64_t GetAllocBytes();

#endif  // PROTO_TEST_ALLOC_COUNTER_H_
```

`.cpp` 覆写全局 `operator new`/`operator new[]`/`operator delete`/`operator delete[]`，内部用 `std::atomic<int64_t>` 计数，分配实现直接调用 `std::malloc`/`std::free`（不经过任何可能再分配内存的标准库设施，避免递归）。这个覆写对整个 `proto_bench` 进程生效，但因为 `proto_test` 是完全独立的可执行文件，不会被影响。

### 2. CMake 改动

`CMakeLists.txt` 里 `proto_bench` 的 source list 从 `src/bench.cpp src/message_fixtures.cpp` 改成 `src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp`。`proto_test`/`chat_proto` 不变。

### 3. `bench.cpp` 新增用例

```cpp
void BM_ParseTextHeapAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextHeapAllocs);

void BM_ParseTextArenaAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));

  google::protobuf::Arena arena;
  {
    // Warm up the arena's first block before measuring, so the
    // steady-state count reflects reuse, not one-time block setup.
    ChatMessage* warm = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(warm->ParseFromString(bytes));
  }
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage* parsed = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(parsed->ParseFromString(bytes));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextArenaAllocs);
```

`BM_ParseTextArenaAllocs` 用同一个 `arena` 对象贯穿整个计时循环（不在循环里 `Reset()`），模拟真实高频场景里"一个长期存活的 arena 反复处理消息"的用法；随着迭代次数增多，arena 会按需向系统分配器申请新的大块内存，但申请频率远低于"每条消息每个子字段都堆分配一次"，所以 `allocs_per_iter` 预期远小于 1（远小于 `BM_ParseTextHeapAllocs` 的值）。

`#include <string>` 已存在，需要额外 `#include "alloc_counter.h"` 和 `#include <google/protobuf/arena.h>`。

### 4. 运行与数据产出

```
cmake -S . -B build   # CMakeLists.txt 改了 proto_bench 的 source list，需要重新 configure
cmake --build build -j20
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase4-2026-06-18.json
```

跑完后写 `docs/benchmarks/phase4-memory-arena-analysis.md`：耗时、`allocs_per_iter`、`bytes_per_iter` 对比表，结论包括分配次数降低的倍数、是否伴随耗时变化（Arena 减少分配次数通常也会降低耗时，因为系统分配器调用本身有开销）。

## 验证标准

- `cmake -S . -B build && cmake --build build -j20`：编译通过，无新增警告（这次确实需要重新 configure，因为改了 `proto_bench` 的 source list，但不涉及 `FetchContent`，不会重新拉取任何依赖，应该很快）。
- `./build/proto_test`：现有正确性测试全部通过，不受影响。
- `./build/proto_bench`：两个新 benchmark 跑完，`allocs_per_iter`：`BM_ParseTextArenaAllocs` 明显小于 `BM_ParseTextHeapAllocs`（符合 Arena 的设计预期）。
- `results/phase4-2026-06-18.json` 生成，分析文档写入实际数据和结论。

## 后续

Phase 5-8 在本 phase 落地后各自单独走相同流程，本文档不预先设计。
