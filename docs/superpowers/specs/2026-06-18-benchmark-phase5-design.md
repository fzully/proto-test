# Protobuf Benchmark Phase 5 — Serialization API Overhead Comparison Design

日期：2026-06-18

> 本 spec 由 Claude 在用户授权自主执行的情况下编写（用户已批准整体路线图并已入睡，本阶段及之后阶段不再征求确认，设计决策自行判断并记录于此）。

## 背景

Phase 0-1 里的 `BM_SerializeText` 用的是 `std::string bytes; bytes.clear(); msg.SerializeToString(&bytes);` 这种模式——`bytes.clear()` 通常不释放已分配的 capacity，所以热循环里大概率不会触发堆分配，但这是"恰好避免了分配"，不是接口本身保证零分配。protobuf 提供了几种不同抽象层级的序列化 API，理论上分配行为和开销不同：
1. `SerializeToString(std::string*)` ——如果每次循环都重新构造一个新的 `std::string`（而不是复用并 `clear()`），会触发字符串堆缓冲区的重新分配。
2. `SerializeToArray(void* data, int size)` ——直接写入调用者提供的、已经按 `ByteSizeLong()` 精确分配好的裸缓冲区，循环体内部不做任何内存分配。
3. `SerializeToCodedStream(io::CodedOutputStream*)` 搭配 `io::ArrayOutputStream` 包装一块预分配缓冲区——这是 protobuf 文档里说的"zero-copy"输出路径，直接在调用者缓冲区上写，不经过中间 `std::string`。

Phase 5 要量化这几种 API 在同一条消息上的耗时差异，验证"避免不必要分配/拷贝"在实践中能带来多少收益。

## 目标

用同一条 `text` fixture（复用 `BuildTextMessage()`），测出：
1. 每次循环都新建一个 `std::string` 再调用 `SerializeToString` 的耗时（对照：分配开销重的写法）。
2. 用循环外预分配一次、按 `ByteSizeLong()` 精确大小的裸字节缓冲区，每次循环调 `SerializeToArray` 写入同一块缓冲区的耗时（循环体内零分配）。
3. 用循环外构造一次的 `io::ArrayOutputStream` 包装同一块预分配缓冲区，每次循环用一个新的 `io::CodedOutputStream` 包装它再调 `SerializeToCodedStream` 的耗时（zero-copy 路径，`CodedOutputStream` 本身轻量到可以每次重建,因为它不持有堆内存,只持有指向 ArrayOutputStream 的指针)。
4. 与 Phase 1 已有的 `BM_SerializeText`（复用 string + `clear()`）对比，作为"实践中已经够快"的基线。

## 范围

**包含：**
- 3 个新 benchmark：`BM_SerializeToFreshString`、`BM_SerializeToPreallocatedArray`、`BM_SerializeToCodedStream`，全部用 `BuildTextMessage()`，输出字节数一致性用 `state.counters["bytes"]` 记录验证。
- 新增 `docs/benchmarks/phase5-api-overhead-analysis.md` + `results/phase5-2026-06-18.json`。

**不包含：**
- 不测反序列化侧的 API 变体（`ParseFromArray` vs `ParseFromString` 行为基本一致，protobuf 内部走的是同一套 `ZeroCopyInputStream` 路径，预期差异远小于序列化侧，留给以后如果需要再补）。
- 不需要新文件/CMake 改动——这 3 个函数和已有的 14+2 个一样直接加进 `src/bench.cpp`，只需要 `#include <google/protobuf/io/coded_stream.h>` 和 `#include <google/protobuf/io/zero_copy_stream_impl_lite.h>`（`ArrayOutputStream` 所在头）。

## 组件设计

### `bench.cpp` 新增用例

```cpp
void BM_SerializeToFreshString(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::size_t last_size = 0;
  for (auto _ : state) {
    std::string bytes;
    static_cast<void>(msg.SerializeToString(&bytes));
    last_size = bytes.size();
  }
  state.counters["bytes"] = static_cast<double>(last_size);
}
BENCHMARK(BM_SerializeToFreshString);

void BM_SerializeToPreallocatedArray(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  const int size = static_cast<int>(msg.ByteSizeLong());
  std::vector<char> buffer(static_cast<std::size_t>(size));
  for (auto _ : state) {
    bool ok = msg.SerializeToArray(buffer.data(), size);
    static_cast<void>(ok);
  }
  state.counters["bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_SerializeToPreallocatedArray);

void BM_SerializeToCodedStream(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  const int size = static_cast<int>(msg.ByteSizeLong());
  std::vector<char> buffer(static_cast<std::size_t>(size));
  for (auto _ : state) {
    google::protobuf::io::ArrayOutputStream array_stream(buffer.data(), size);
    google::protobuf::io::CodedOutputStream coded_stream(&array_stream);
    msg.SerializeToCodedStream(&coded_stream);
  }
  state.counters["bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_SerializeToCodedStream);
```

`BM_SerializeToPreallocatedArray`/`BM_SerializeToCodedStream` 都把缓冲区分配挪到循环外，循环体内不调用任何堆分配 API（`ArrayOutputStream`/`CodedOutputStream` 本身只持有指针和计数器，构造它们不分配堆内存，可以在循环里重建）。`BM_SerializeToFreshString` 反过来故意在循环内构造新 `std::string`，作为"明显有分配开销"的对照组。

`#include` 新增：`<vector>`、`<google/protobuf/io/coded_stream.h>`、`<google/protobuf/io/zero_copy_stream_impl_lite.h>`。

### 运行与数据产出

```
cmake --build build -j20   # 纯 C++ 新增，不改 CMakeLists.txt，不需要重新 configure
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase5-2026-06-18.json
```

写 `docs/benchmarks/phase5-api-overhead-analysis.md`：4 个 benchmark（含已有的 `BM_SerializeText` 作对照）耗时对比表，结论包括：裸缓冲区/CodedStream 路径是否真的比"复用 string + clear()"更快（预期：差异很小，因为 `clear()` 本身已经接近零分配），以及"新建 string"路径是否明显慢于其它三者（预期：是，量化分配开销的实际成本）。

## 验证标准

- `cmake --build build -j20`：编译通过，无新增警告。
- `./build/proto_test`：现有正确性测试全部通过，不受影响。
- 4 个 benchmark 都跑出结果，`bytes` counter 在 3 个新 benchmark 里都等于 `ByteSizeLong()` 算出的值（与已有 117 字节基线一致）。
- 分析文档写入实际数据和结论，不留占位符。

## 后续

Phase 6（CPU 微架构指标）需要先检查 `perf` 在当前环境下是否可用（`perf stat` 权限、`/proc/sys/kernel/perf_event_paranoid`、容器限制），若不可行则在该阶段说明并跳过/降级，而不是强行设计一个跑不起来的方案。
