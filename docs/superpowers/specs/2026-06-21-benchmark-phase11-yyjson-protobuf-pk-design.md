# Protobuf Benchmark Phase 11 — yyjson vs Protobuf PK Design

日期：2026-06-21

> 本 spec 在用户明确要求"开一个 Phase 11 spec，用 yyjson 跟 Protobuf PK"并经过澄清后编写。用户已确认 Phase 11 范围对齐 Phase 1/9（只比 encode/decode 耗时与体积，不做规模扫描/并发/堆分配计数/畸形输入），且确认"零新代码，纯复用 Phase 1（Protobuf）与 Phase 10（yyjson）已有的 benchmark，只出一份对比分析"。

## 背景

Phase 10 横向对比了 4 个 JSON 库（nlohmann/json、RapidJSON、yyjson、cJSON；simdjson 因没有对称的高性能 writer 被排除），结果 yyjson 以 1203.82 ns（两种形态 encode+decode 总耗时）夺冠，领先第二名 RapidJSON（2717.31 ns）2.26 倍，领先末位 nlohmann/json（13008.57 ns）10.8 倍。Phase 11 是 Phase 10 的既定后续：把这个胜者拿来跟 Protobuf 做最终 PK，对齐 Phase 1（Protobuf 的 encode/decode 耗时与体积基线）和 Phase 9（SBE 跟 Protobuf 的同类对比）建立的范围与方法论。

## 目标

测出 Protobuf 与 yyjson 在两种现有逻辑消息内容（`BuildTextMessage`、`BuildMergedForwardMessage`）下的 encode/decode 耗时与体积，逐行对照给出真实倍数，诚实报告谁快、快多少、体积谁更小——不预设"二进制格式一定比文本格式快/小"这类结论，按实测数字说话。

## 范围

**包含：**

- 复用 Phase 1 已有的 4 个 Protobuf benchmark（`BM_SerializeText`、`BM_ParseText`、`BM_SerializeMergedForward`、`BM_ParseMergedForward`，定义于 `src/bench.cpp`）和 Phase 10 已有的 4 个 yyjson benchmark（`BM_EncodeTextJsonYyjson`、`BM_DecodeTextJsonYyjson`、`BM_EncodeMergedForwardJsonYyjson`、`BM_DecodeMergedForwardJsonYyjson`，同样定义于 `src/bench.cpp`）。**这 8 个 benchmark 已经存在且已验证可正确编译运行（Phase 1/Phase 10 都已 commit），本阶段不新增、不修改任何 `.cpp`/`.h` 文件。**
- 用一条精确的 `--benchmark_filter` 正则同时跑这 8 个 benchmark：

  ```
  ./build/proto_bench --benchmark_filter="^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$" --benchmark_format=json --benchmark_out=results/phase11-2026-06-21.json
  ```

  这条正则已经过逐一核对（见下方"组件设计"第 1 节），确认只匹配这 8 个目标 benchmark，不会误命中 Phase 2/3/5 等同前缀的其他 benchmark（如 `BM_SerializeSparseText`、`BM_SerializeMergedItems`、`BM_SerializeToFreshString`）。
- 新增 `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`：两张表（TextMessage、MergedForwardMessage），每张表列 Protobuf 与 yyjson 的 encode/decode 耗时（ns）与体积（bytes），给出耗时倍数和体积倍数，按实测数字下结论。
- 把 `docs/benchmarks/final-report-phases-0-10.md` 重命名为 `final-report-phases-0-11.md`（沿用 Phase 9→10 的命名升级模式），补一节 Phase 11 摘要；同步更新 `.zh-CN.md` 镜像。
- 更新 `CLAUDE.md` 里对 final-report 文件名的引用（如果改名后产生悬空引用——参考 Phase 10 收尾时发现并修复 `CLAUDE.md` 里两处过期 `final-report-phases-0-9.md` 引用的先例，这次实现阶段同样要检查一遍，不要留下悬空文件名）。

**不包含：**

- **不写任何新代码**——8 个 benchmark 全部复用现有的，不新增 fixture、不新增 benchmark 函数、不改 `CMakeLists.txt`。
- **不做规模扫描**（`mentioned_user_ids`/`merged_forward.items` 在 n=1/10/100/1000 下的扫描，对应 Phase 3）、**不做并发**（对应 Phase 7）、**不做堆分配计数**（对应 Phase 4）、**不做畸形输入测试**（对应 Phase 8）——跟 Phase 10 排除 simdjson 同样的范围克制理由：这些问题如果将来需要，应该开一个新的 phase 单独回答，不要在"PK 收尾"这一个 phase 里顺带做大。
- **不重新跑 Phase 10 的 4 库横向对比**——yyjson 已经是 Phase 10 选出的胜者，本阶段不重新验证那个结论，只把它跟 Protobuf 比。
- **不引入新的 JSON 编码约定**——`json_yyjson_fixtures.cpp`（Phase 10 已实现）用的 camelCase + int64/enum 转字符串 + compact 输出的约定原样复用，本阶段不讨论是否要换一种 JSON 表达方式。

## 组件设计

### 1. Benchmark filter 正则验证

对 `src/bench.cpp` 里全部 `void BM_*` 函数名跑了一遍 `grep -P '^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$'`，确认只命中这 8 个：

```
BM_SerializeText
BM_ParseText
BM_SerializeMergedForward
BM_ParseMergedForward
BM_EncodeTextJsonYyjson
BM_DecodeTextJsonYyjson
BM_EncodeMergedForwardJsonYyjson
BM_DecodeMergedForwardJsonYyjson
```

`^...$` 锚定排除了 `BM_SerializeSparseText`（"Serialize" 后面是 "SparseText" 不是 "Text"）、`BM_SerializeMergedItems`（"MergedItems" 不是 "MergedForward"）、`BM_SerializeToFreshString`/`BM_SerializeToPreallocatedArray`/`BM_SerializeToCodedStream`（"Serialize" 后面是 "To..." 不是 "Text"/"MergedForward"）。`JsonYyjson$` 这一段只命中 yyjson 自己的 4 个（不会命中 nlohmann/RapidJSON/cJSON 的同形态 benchmark，因为它们的函数名后缀是 `JsonNlohmann`/`JsonRapidjson`/`JsonCJson`，不是 `JsonYyjson`）。

### 2. 运行与产出物

```sh
cmake --build build -j$(nproc)   # 确认现有二进制是最新的，本阶段预期不会有任何源码改动触发重新编译
./build/proto_bench --benchmark_filter="^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$" --benchmark_format=json --benchmark_out=results/phase11-2026-06-21.json
```

预期产出恰好 8 条 benchmark 结果，每条都有 `real_time`（ns/op）和 `bytes`（counter）字段（`bytes` counter 是 Phase 1/Phase 10 两边的 benchmark 都已经在用的既有惯例，不需要新增）。

### 3. `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`

用以下骨架，<...> 占位符在实现阶段用 `results/phase11-2026-06-21.json` 里的真实 `real_time`/`bytes` 值填入（不能留空，不能估算，必须是该次实际跑出来的数字）：

```markdown
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
| Protobuf | <BM_SerializeText real_time> | <BM_ParseText real_time> | <bytes> |
| yyjson | <BM_EncodeTextJsonYyjson real_time> | <BM_DecodeTextJsonYyjson real_time> | <bytes> |

## Results — MergedForwardMessage 形态

| 格式 | Encode (ns) | Decode (ns) | bytes |
| --- | --- | --- | --- |
| Protobuf | <BM_SerializeMergedForward real_time> | <BM_ParseMergedForward real_time> | <bytes> |
| yyjson | <BM_EncodeMergedForwardJsonYyjson real_time> | <BM_DecodeMergedForwardJsonYyjson real_time> | <bytes> |

## 结论

<按实测数字给出耗时倍数（Protobuf 比 yyjson 快/慢多少倍，encode 和 decode 分开说，因为
两者的倍数很可能不一样）和体积倍数，诚实报告——如果某一项 yyjson 反而更快或体积更小，
直接写出来，不要为了"二进制理应更优"这个先验印象去美化或回避。>

## Carried into final report

并入 `docs/benchmarks/final-report-phases-0-11.md`（和 `.zh-CN.md` 镜像）的 Phase 11 一节和
跨阶段结论。
```

### 4. Final report 收尾

- `git mv docs/benchmarks/final-report-phases-0-10.md docs/benchmarks/final-report-phases-0-11.md`
- `git mv docs/benchmarks/final-report-phases-0-10.zh-CN.md docs/benchmarks/final-report-phases-0-11.zh-CN.md`
- 两个文件里补 Phase 11 一节（沿用 Phase 10 那一节的格式）+ 一条跨阶段结论 bullet（yyjson vs Protobuf 的真实倍数）。
- 检查并修复 `CLAUDE.md` 里对 final-report 文件名的引用（Phase 10 收尾时已经发现并修过类似的悬空引用，这次实现阶段要重复同样的检查，不能假设这次不会有）。

## 验证标准

- `results/phase11-2026-06-21.json` 恰好包含 8 条 benchmark 记录，且都是上面列出的 8 个目标名字（用 `grep -c '"name"'` 之类的方式核对条数，再核对每个 name 字符串）。
- `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md` 里的每一个数字都能在 `results/phase11-2026-06-21.json` 里找到对应的原始值（不是估算、不是从 Phase 10 的旧数字里搬运——本阶段必须重新跑一次，因为 Phase 1 的 Protobuf 数字是很早之前跑的，机器状态/负载可能已经变化，PK 对比必须是同一次运行里测出来的才公平）。
- `./build/proto_test` 仍然全部通过（本阶段不改代码，但收尾前要确认没有意外破坏现有正确性）。
- `final-report-phases-0-11.md`（含 `.zh-CN.md`）和 `CLAUDE.md` 里不再有任何指向旧文件名 `final-report-phases-0-10.md` 的悬空引用。

## 后续

完成并验证后，本阶段是当前 final-report 索引的最新一节。如果后续还想测 Phase 3/4/7/8 对应的 yyjson 版本（规模扫描、并发、堆分配、畸形输入），需要单独开新 phase——本 spec 明确不包含这些。
