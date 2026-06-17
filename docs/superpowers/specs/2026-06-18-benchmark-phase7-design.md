# Protobuf Benchmark Phase 7 — Concurrent Throughput Scaling Design

日期：2026-06-18

> 本 spec 由 Claude 在用户授权自主执行的情况下编写（用户已批准整体路线图并已入睡，本阶段不再征求确认，设计决策自行判断并记录于此）。

## 背景

之前所有 phase 都是单线程耗时/体积测量。真实 IM 服务端通常是多个 worker 线程各自独立处理不同连接的消息序列化/反序列化，互不共享可变状态。Phase 7 测的是：当 N 个线程各自独立、重复执行同一条 `text` 消息的 serialize/parse 时，聚合吞吐量（每秒处理的消息数，跨所有线程合计）随线程数增长的扩展性——是接近线性扩展（CPU-bound，几乎无竞争），还是在某个线程数之后出现瓶颈（内存带宽、分配器锁、cache 一致性流量等）。

Google Benchmark 原生支持这种场景：`BENCHMARK(fn)->Threads(N)` 会注册一个用 N 个线程并发跑同一个 benchmark 函数的配置；每个线程独立调用一次该函数体，循环外的代码（构造消息、序列化得到 bytes）在每个线程里各自执行一次，不跨线程共享——这正好对应"每个线程有自己独立的消息/buffer，互不竞争"的设计意图，不需要额外加锁或共享数据结构。

## 目标

测出：
1. `text` 消息序列化在 1/2/4/8/16/20 个并发线程下的耗时分布（每线程 `real_time`/`cpu_time`），换算出跨所有线程的聚合吞吐量（ops/sec）。
2. 同样条件下 parse 侧的聚合吞吐量。
3. 聚合吞吐量是否随线程数大致线性增长（在本机 20 核条件下），还是提前饱和；如果饱和，发生在哪个线程数附近。

## 范围

**包含：**
- 2 个新 benchmark：`BM_ConcurrentSerializeText`、`BM_ConcurrentParseText`，复用 `BuildTextMessage()`，每个用 `->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20)` 注册 6 个并发度。
- 新增 `docs/benchmarks/phase7-concurrency-analysis.md` + `results/phase7-2026-06-18.json`。
- 分析阶段需要用 `threads * (1e9 / real_time_ns)` 计算聚合吞吐量（`real_time` 是多线程场景下"一次迭代的 wall-clock 耗时"，所有线程并发跑完一次迭代算一次，所以乘以线程数得到聚合 ops/sec）。

**不包含：**
- 不引入任何跨线程共享可变状态或锁——每个线程的消息/buffer 都是线程局部的，故意设计成"无竞争"的理想情况，用来看纯 CPU 并行扩展性，而不是锁竞争。
- 不测 `merged_forward` 类型——复用 `text` fixture，与其它 phase 一致。
- 不需要新文件/CMake 改动——纯 C++ 函数加进 `src/bench.cpp`，Google Benchmark 的 `->Threads()` 是已链接库的现有 API，不需要新增依赖或源文件。

## 组件设计

### `bench.cpp` 新增用例

```cpp
void BM_ConcurrentSerializeText(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentSerializeText)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ConcurrentParseText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentParseText)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);
```

这两个函数体跟 Phase 1 的 `BM_SerializeText`/`BM_ParseText` 完全一样（同样的代码，同样复用 `BuildTextMessage()`）——区别只在于用 `->Threads(N)` 注册多个并发度。`ChatMessage msg`/`original`、`std::string bytes` 都是函数局部变量，在每个线程里各自构造一份，线程间不共享，不需要额外同步。

### 运行与数据产出

```
cmake --build build -j20   # 纯 C++ 新增，不改 CMakeLists.txt，不需要重新 configure
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase7-2026-06-18.json --benchmark_filter="Concurrent"
```

写 `docs/benchmarks/phase7-concurrency-analysis.md`：表格列出 1/2/4/8/16/20 线程下 serialize 和 parse 各自的 `real_time`（单次迭代耗时）、聚合吞吐量（`threads * 1e9/real_time`）、以及相对 1 线程吞吐量的扩展比例（理想线性扩展比例应该等于线程数）。结论包括：是否接近线性扩展，在哪个线程数开始偏离线性（如果有），可能的原因（本机是 20 核单 NUMA 节点，预期在 ≤20 线程范围内大致线性，因为每个线程的工作集很小、彼此独立，几乎没有共享数据的 cache 一致性流量；如果偏离，可能是 CPU 频率随核心占用率下降的"涡轮加速"效应,或操作系统调度开销)。

## 验证标准

- `cmake --build build -j20`：编译通过，无新增警告。
- `./build/proto_test`：现有正确性测试全部通过，不受影响。
- 12 个新 benchmark 配置（2 函数 × 6 个线程数）全部跑出结果，`bytes` counter 仍为 117。
- 分析文档写入实际数据和结论，不留占位符。

## 后续

Phase 8（畸形输入解析失败开销）在本 phase 落地后走相同流程。
