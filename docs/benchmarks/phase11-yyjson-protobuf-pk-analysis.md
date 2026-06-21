# Phase 11 — yyjson vs Protobuf PK Analysis

Date: 2026-06-21

## Method

复用 Phase 1 的 4 个 Protobuf benchmark 和 Phase 10 的 4 个 yyjson benchmark（均定义于
`src/bench.cpp`，本阶段未新增/修改任何代码），用
`--benchmark_filter="^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$"` 同时跑出。
yyjson 是 Phase 10 在 nlohmann/json、RapidJSON、yyjson、cJSON 四库横向对比中选出的胜者
（详见 `docs/benchmarks/phase10-json-shootout-analysis.md`）。

## Results — TextMessage 形态

| 格式 | Encode (ns) | Decode (ns) | bytes |
| --- | --- | --- | --- |
| Protobuf | 73.67 | 191.10 | 117 |
| yyjson | 159.98 | 454.22 | 408 |

## Results — MergedForwardMessage 形态

| 格式 | Encode (ns) | Decode (ns) | bytes |
| --- | --- | --- | --- |
| Protobuf | 104.97 | 316.25 | 162 |
| yyjson | 203.16 | 474.80 | 500 |

## 结论

两种形态下 Protobuf 在 encode、decode、体积三项指标上全部胜出，没有出现 yyjson 反超的情况，
但各项倍数并不一致，分开报告如下（倍数 = 较大值 / 较小值）：

**TextMessage 形态：**
- Encode：Protobuf 73.67 ns vs yyjson 159.98 ns，Protobuf 快 **2.17x**。
- Decode：Protobuf 191.10 ns vs yyjson 454.22 ns，Protobuf 快 **2.38x**。
- 体积：Protobuf 117 bytes vs yyjson 408 bytes，Protobuf 小 **3.49x**。

**MergedForwardMessage 形态：**
- Encode：Protobuf 104.97 ns vs yyjson 203.16 ns，Protobuf 快 **1.94x**。
- Decode：Protobuf 316.25 ns vs yyjson 474.80 ns，Protobuf 快 **1.50x**。
- 体积：Protobuf 162 bytes vs yyjson 500 bytes，Protobuf 小 **3.09x**。

两个形态下 encode 的优势（2.17x / 1.94x）都比 decode 的优势更大或接近，但 decode 优势在
MergedForwardMessage 上明显收窄（1.50x，对比 TextMessage 的 2.38x）——repeated 字段更多的
消息形态下，yyjson 的 decode 劣势在缩小，这与 Phase 3 此前观察到的"repeated 字段是最贵的
schema 构造"结论方向一致：repeated 字段对两种格式的 decode 都有额外开销，但对 yyjson 的
相对惩罚小于对 Protobuf 的相对惩罚。体积上 Protobuf 的优势（3.09x–3.49x）在两个形态间相对
稳定，符合预期——二进制 varint/tag 编码本身就比 JSON 文本表示更紧凑，与消息形态无关。

诚实地说，这次对比中二进制格式（Protobuf）确实在每一项指标上都跑赢了目前最快的 JSON 库
（yyjson），但优势幅度因指标和形态而异（1.50x–3.49x），不是一个统一的"二进制天生强 N 倍"
的数字，因此分开报告而不取平均。

## Carried into final report

并入 `docs/benchmarks/final-report-phases-0-11.md`（和 `.zh-CN.md` 镜像）的 Phase 11 一节和
跨阶段结论。
