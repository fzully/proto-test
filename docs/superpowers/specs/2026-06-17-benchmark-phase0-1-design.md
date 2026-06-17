# Protobuf Benchmark Phase 0 + Phase 1 — 设计文档

日期：2026-06-17

## 背景

当前工程（见 `docs/superpowers/specs/2026-06-17-im-chat-protobuf-design.md`）只验证了 IM 聊天消息 schema 的 protobuf 序列化/反序列化*正确性*（`src/main.cpp` 里的 round-trip 断言测试），尚未收集任何性能数据。

讨论确定了一个完整的 benchmark 路线图，共 8 个测试目标，按依赖关系分阶段推进：

1. Phase 0 — Benchmark 基础设施
2. Phase 1 — 序列化/反序列化吞吐延迟 + 体积（本 spec 覆盖）
3. Phase 2 — 字段填充率 + 数值编码效率
4. Phase 3 — 规模可扩展性（repeated 字段长度参数化）
5. Phase 4 — 内存分配次数 / Arena 对比
6. Phase 5 — 序列化 API 选择开销（`SerializeToString` vs 预分配 buffer/`CodedStream`）
7. Phase 6 — CPU 微架构指标（`perf stat`，依赖宿主环境权限，需先确认可行性）
8. Phase 7 — 并发吞吐扩展性
9. Phase 8 — 损坏/截断输入的解析失败开销

横向对比 protobuf vs JSON 被明确排除：需要引入新依赖，且与现有设计文档"本次试验只验证 protobuf 序列化本身"的范围声明冲突。

本 spec 只覆盖 **Phase 0（基础设施）+ Phase 1（吞吐延迟 + 体积）**。Phase 2-8 各自单独走 brainstorming → spec → plan → 实现的流程，不在本文档展开。

## 目标

1. 搭建可复用的 C++ benchmark 基础设施，后续 7 个 phase 都能直接在此基础上加用例，不用重新搭框架。
2. 测出现有两类消息（`text`、`merged_forward`）的序列化/反序列化吞吐延迟和序列化后体积，产出结构化数据文件和一份人读的分析结论。

## 范围

**包含：**
- 引入 Google Benchmark 作为 benchmark 框架。
- 抽取现有消息构造逻辑为共享 fixture，供正确性测试和 benchmark 复用。
- 新增独立的 `proto_bench` 可执行文件，覆盖 `text` / `merged_forward` 两种消息类型的序列化和反序列化共 4 个基准。
- 序列化体积作为自定义 counter 一并输出。
- 结构化（JSON）结果文件 + 人读分析文档。

**不包含：**
- Phase 2-8 涉及的字段填充率、数值编码、可扩展性、内存/Arena、API 开销、CPU 微架构、并发、解析失败路径——均留给后续 phase。
- protobuf vs JSON 等跨格式横向对比。
- 网络传输、持久化存储、真实 IM 服务端/客户端实现（继承自原设计文档的范围声明）。

## 目录结构

```
proto-test/
  CMakeLists.txt
  proto/
    chat.proto
  src/
    main.cpp                # 现有正确性测试，不变
    message_fixtures.h      # 新增：共享消息构造函数声明
    message_fixtures.cpp    # 新增：从 main.cpp 抽取的 BuildTextMessage/BuildMergedForwardMessage
    bench.cpp                # 新增：Google Benchmark 用例
  results/
    phase1-2026-06-17.json   # 新增：本次 benchmark 运行的结构化输出
  docs/
    superpowers/specs/...
    benchmarks/
      phase1-throughput-size-analysis.md   # 新增：分析文档
```

## 组件设计

### 1. `message_fixtures.h` / `message_fixtures.cpp`

把 `src/main.cpp` 里的 `BuildTextMessage()` 和 `BuildMergedForwardMessage()` 抽取为自由函数，签名不变，移到独立的头文件/源文件中。`main.cpp` 和 `bench.cpp` 都 `#include "message_fixtures.h"`，保证正确性测试和性能测试使用同一份测试数据，不会出现两边 fixture 不一致的问题。`main.cpp` 中原有的 `BuildTextMessage`/`BuildMergedForwardMessage` 定义删除，改为调用抽取后的版本，其余测试逻辑不变。

### 2. CMake 改动

- 新增 `FetchContent_Declare(benchmark, GIT_REPOSITORY https://github.com/google/benchmark.git, GIT_TAG v1.9.1)`。
- 设置 `BENCHMARK_ENABLE_TESTING OFF`（不需要 Google Benchmark 自带的测试，避免拉 googletest）、`BENCHMARK_ENABLE_GTEST_TESTS OFF`。
- `message_fixtures.cpp` 直接加进 `proto_test` 和 `proto_bench` 两个 target 各自的 source list（与现有 `CMakeLists.txt` 的极简风格一致，不引入 OBJECT library 这层抽象）。
- 新增 `proto_bench` executable target：
  - source: `src/bench.cpp` + `message_fixtures.cpp`
  - link: `protobuf::libprotobuf`、`benchmark::benchmark`
  - include 目录同 `proto_test`（生成的 `chat.pb.h` 所在目录）。

### 3. `bench.cpp` 用例

4 个基准函数：

- `BM_SerializeText` / `BM_SerializeMergedForward`：在计时循环外用 fixture 构造好 `ChatMessage`，循环内只执行 `SerializeToString`，避免把对象构造开销计入序列化耗时。每次迭代后通过 `state.counters["bytes"]` 记录序列化后字节数（体积在所有迭代中应恒定，取最后一次即可）。
- `BM_ParseText` / `BM_ParseMergedForward`：循环外先用 fixture 构造并序列化好 `bytes`，循环内只执行 `ParseFromString`。

Google Benchmark 默认会自动选择迭代次数、做统计上的多轮采样，不需要手动控制 warmup/iteration count。

### 4. 运行与数据产出

```
cmake -S . -B build
cmake --build build -j
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase1-2026-06-17.json
```

终端同时保留 Google Benchmark 默认的可读表格输出，方便跑的时候直接看一眼。

跑完后，把 JSON 结果人工整理成 `docs/benchmarks/phase1-throughput-size-analysis.md`，至少包含：
- 每种消息类型 × 操作（序列化/反序列化）的耗时（ns/iter）和吞吐（ops/sec）
- 每种消息类型的序列化体积（bytes）
- 简单结论：例如 `merged_forward`（嵌套 repeated message）相对 `text`（扁平结构）的耗时/体积倍数是否符合预期

## 验证标准

- `cmake --build build -j`：`proto_test` 和 `proto_bench` 两个 target 都编译通过。
- `./build/proto_test`：现有正确性测试全部通过（证明抽取 fixture 没有改变行为）。
- `./build/proto_bench`：4 个基准全部跑完，产出非零的耗时和体积数据，`results/phase1-2026-06-17.json` 文件生成。
- `docs/benchmarks/phase1-throughput-size-analysis.md` 写入实际跑出的数据和结论。

## 后续

Phase 2-8 在本 phase 落地、`proto_bench` 基础设施验证可用后，各自单独走 brainstorming 流程产出独立 spec，不在本文档预先设计。
