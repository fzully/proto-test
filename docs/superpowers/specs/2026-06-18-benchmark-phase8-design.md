# Protobuf Benchmark Phase 8 — Malformed-Input Parse Failure Cost Design

日期：2026-06-18

> 本 spec 由 Claude 在用户授权自主执行的情况下编写（用户已批准整体路线图并已入睡，本阶段不再征求确认，设计决策自行判断并记录于此）。

## 背景

之前所有 parse 相关 benchmark 测的都是"成功解析合法消息"的耗时。但真实服务端会持续收到网络层的截断包、损坏包、甚至恶意构造的畸形输入，必须先判断"这条输入到底能不能解析"——如果失败路径异常昂贵（比如要扫描完整个 buffer 才能判定失败），就可能成为一个 DoS 攻击面。Phase 8 测的是：解析一条**结构上不完整/不合法**的字节串，相对于解析一条合法消息，开销是更贵、更便宜，还是差不多。

## 目标

用同一条 `text` 消息的合法序列化结果（117 字节，复用 `BuildTextMessage()`）为基准，构造 2 种确定性的畸形输入，测出三者的 parse 耗时对比：
1. **合法输入**（基准，对应已有的 `BM_ParseText`，本阶段直接复用其结果，不重复定义函数）。
2. **截断输入**：把合法的 117 字节序列化结果直接砍掉后一半（取前 58 字节），制造一个"字段读到一半数据就没了"的不完整消息——`ParseFromString` 应该返回 `false`。
3. **垃圾输入**：构造一个全部由 `0xFF` 填充的、长度等于合法消息（117 字节）的字节串。`0xFF` 作为 tag 字节，最高位（continuation bit）为 1，意味着 varint 解析器会认为"后面还有更多字节属于这个 varint"，而后续字节全部还是 `0xFF`，导致 varint 一直读到超过 protobuf 允许的最大长度（10 字节）才被判定为格式错误——这是一个确定性的、不依赖随机数的"结构性畸形"输入，能触发 parser 的 malformed-varint 错误路径而不是简单的"数据不够"路径，跟截断输入测的是两种不同的失败模式。`ParseFromString` 应该返回 `false`。

## 范围

**包含：**
- 2 个新 benchmark：`BM_ParseTruncatedText`、`BM_ParseGarbageText`，各自在循环里记录最后一次 `ParseFromString` 的返回值到 `state.counters["parse_ok"]`（1.0=成功，0.0=失败），作为验证用的哨兵——预期两者都应该是 0.0（解析失败），如果不是说明构造的"畸形"输入意外解析成功了，需要先排查再继续。
- 复用已有的 `BM_ParseText`（Phase 1）作为合法输入基准，不重新定义。
- 新增 `docs/benchmarks/phase8-malformed-input-analysis.md` + `results/phase8-2026-06-18.json`。

**不包含：**
- 不引入真正的随机/网络畸形输入生成器——两种畸形输入都是从已知合法字节串确定性派生的（截断、固定字节模式覆盖），保证可重现。
- 不测"部分有效、部分畸形"的混合情况（例如前几个字段合法、后面开始畸形）——只测两种边界情况（截断 vs 结构错误），范围控制在二元对比。
- 不需要新文件/CMake 改动——纯 C++ 函数加进 `src/bench.cpp`。

## 组件设计

### `bench.cpp` 新增用例

```cpp
void BM_ParseTruncatedText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string full_bytes;
  static_cast<void>(original.SerializeToString(&full_bytes));
  const std::string truncated = full_bytes.substr(0, full_bytes.size() / 2);
  bool last_ok = true;
  for (auto _ : state) {
    ChatMessage parsed;
    last_ok = parsed.ParseFromString(truncated);
  }
  state.counters["parse_ok"] = last_ok ? 1.0 : 0.0;
  state.counters["bytes"] = static_cast<double>(truncated.size());
}
BENCHMARK(BM_ParseTruncatedText);

void BM_ParseGarbageText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string full_bytes;
  static_cast<void>(original.SerializeToString(&full_bytes));
  const std::string garbage(full_bytes.size(), '\xFF');
  bool last_ok = true;
  for (auto _ : state) {
    ChatMessage parsed;
    last_ok = parsed.ParseFromString(garbage);
  }
  state.counters["parse_ok"] = last_ok ? 1.0 : 0.0;
  state.counters["bytes"] = static_cast<double>(garbage.size());
}
BENCHMARK(BM_ParseGarbageText);
```

两个函数体都在循环外构造一次目标字节串（截断/全 0xFF），循环体内只反复调用 `ParseFromString`，跟现有 `BM_ParseText` 的结构保持一致，便于直接对比 `real_time`。

### 运行与数据产出

```
cmake --build build -j20   # 纯 C++ 新增，不改 CMakeLists.txt，不需要重新 configure
./build/proto_test
./build/proto_bench --benchmark_format=json --benchmark_out=results/phase8-2026-06-18.json --benchmark_filter="^BM_ParseText$|^BM_ParseTruncatedText$|^BM_ParseGarbageText$"
```

写 `docs/benchmarks/phase8-malformed-input-analysis.md`：三者（合法/截断/垃圾）耗时对比表，确认 `parse_ok` counter 符合预期（合法=1.0，截断/垃圾=0.0）；结论包括失败路径是更快还是更慢、可能的原因（截断输入通常会更快失败，因为一读到"数据不够"就能立刻报错；全 0xFF 的垃圾输入需要先尝试把一个畸形 varint 读到底（最多 10 字节）才能判定出错，可能比截断稍慢，但不太可能比成功解析整条 117 字节消息更慢，因为它实际处理的字节数远少于一整条合法消息）。

## 验证标准

- `cmake --build build -j20`：编译通过，无新增警告。
- `./build/proto_test`：现有正确性测试全部通过，不受影响。
- 2 个新 benchmark 都跑出结果，`parse_ok` counter 均为 0.0（确认构造的畸形输入确实解析失败，不是意外构造出了"看起来畸形但实际能解析"的输入）。
- 分析文档写入实际数据和结论，不留占位符。

## 后续

这是路线图里计划的最后一个 benchmark phase（Phase 6 因环境权限限制已记录为跳过）。本 phase 完成并合并后，进入"整理 Phase 0-8 完整报告"的收尾阶段。
