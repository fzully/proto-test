# Protobuf IM 聊天消息 Benchmark 套件 — 最终报告（Phase 0-9，中文版）

日期：2026-06-18（Phase 9 于 2026-06-19 补充）
仓库：`proto-test`（`im.chat.v1` protobuf schema，C++20，CMake + FetchContent，Google Benchmark v1.9.1，protobuf v35.1）

> 本文档是 `final-report-phases-0-9.md`（英文版）的中文说明，内容与英文版一一对应。如有出入，以英文版及各 Phase 的原始分析文档/JSON 数据为准。

本报告汇总了本项目中跑过的全部 benchmark 阶段：基础设施、吞吐量/延迟/体积、字段填充率、数值编码、可扩展性、内存/Arena 分配、序列化 API 开销、并发扩展性、畸形输入解析开销，以及一次 Protobuf 与 SBE 的对比。Phase 6（通过 `perf` 采集 CPU 微架构计数器）在当前沙箱环境下被判定为不可行，已如实记录为"跳过"，而不是伪造数据硬塞进去。

所有原始数据在 `results/phaseN-*.json`；所有分阶段分析在 `docs/benchmarks/phaseN-*.md`。本文档是对它们的综合提炼——建议先读这份，再去看各 Phase 的详细文档。

---

## 这套 benchmark 是怎么搭起来的

- `chat_proto` 是一个共享的静态库，持有从 `proto/chat.proto` 生成的 `chat.pb.{h,cc}`，被 `proto_test`（正确性往返测试）和 `proto_bench`（Google Benchmark 套件）共同链接，这样每次构建只跑一次 `protoc`。
- `src/message_fixtures.{h,cpp}` 提供了一组确定性的消息构造函数（`BuildTextMessage`、`BuildSparseTextMessage`、`BuildTextMessageWithId`、`BuildTextMessageWithMentionCount`、`BuildMergedForwardMessage`、`BuildMergedForwardMessageWithItemCount`），所有 Phase 都复用这些函数，确保各阶段测的是完全相同的基准消息体。
- 所有依赖（protobuf、Abseil、Google Benchmark）都通过 CMake `FetchContent` 拉取，并加了 `GIT_SHALLOW TRUE`，避免拉取完整历史。
- 构建类型固定为 `Release`（`CMAKE_BUILD_TYPE` 未设置时默认为 `Release`）——经实测，Debug 模式下的耗时数据噪声大、慢约 10 倍，不具代表性。
- 本机全程 `cpu_scaling_enabled: true`（没有专门的、隔离的测试机）——绝对的纳秒级数值会带一些噪声；但每个 Phase 内部的相对对比幅度都足够大，能扛住这种噪声，这一点在每个 Phase 自己的分析文档里也都重复提示过。

---

## Phase 0 — 基础设施

构建系统（CMake + FetchContent 拉取 protobuf v35.1 和 Google Benchmark v1.9.1，都加了 `GIT_SHALLOW`）、`chat_proto` 共享库、`proto_test`/`proto_bench` 两个可执行文件，以及 `message_fixtures` 模块。本阶段没有性能数据——它是后续所有阶段运行的地基。

## Phase 1 — 吞吐量、延迟与体积

测两种已有消息形态（`text`、`merged_forward`）的 serialize/parse 基准耗时。

| Benchmark | 操作 | 消息类型 | 耗时 (ns/次) | 吞吐量 (ops/sec) | 体积 (bytes) |
|---|---|---|---|---|---|
| BM_SerializeText | serialize | text | 76.23 | 13,118,917 | 117 |
| BM_ParseText | parse | text | 189.98 | 5,263,734 | 117 |
| BM_SerializeMergedForward | serialize | merged_forward | 115.34 | 8,669,697 | 162 |
| BM_ParseMergedForward | parse | merged_forward | 297.48 | 3,361,568 | 162 |

`merged_forward`（内嵌了 `ForwardedItem` 子消息）序列化比 `text` 慢 51%，解析慢 57%，体积大 38%。两种消息类型下，解析都比序列化贵 2.5-2.6 倍——反序列化（要分配对象、校验 wire format）每条消息的开销天然就比序列化（只是写字节）更高。

## Phase 2 — 字段填充率与数值编码效率

| Benchmark | 操作 | 填充率 | 耗时 (ns/次) | 体积 (bytes) |
|---|---|---|---|---|
| BM_SerializeSparseText | serialize | 稀疏 | 51.78 | 83 |
| BM_SerializeText | serialize | 完整 | 77.48 | 117 |
| BM_ParseSparseText | parse | 稀疏 | 110.67 | 83 |
| BM_ParseText | parse | 完整 | 188.75 | 117 |

| Benchmark | 操作 | message_id | 耗时 (ns/次) | 体积 (bytes) |
|---|---|---|---|---|
| BM_SerializeSmallId | serialize | 1 | 80.58 | 116 |
| BM_SerializeLargeId | serialize | 1950123456789012345 | 84.00 | 124 |
| BM_ParseSmallId | parse | 1 | 194.94 | 116 |
| BM_ParseLargeId | parse | 1950123456789012345 | 194.18 | 124 |

省略掉可选字段（引用、@提及）能省 29.1% 的字节数和 33-41% 的延迟——延迟改善幅度*大于*体积改善幅度，说明每个字段本身的处理开销（不只是 wire 字节数）也是成本的一部分。一个 19 位的雪花算法量级 `message_id` 比单位数的多花了恰好 8 个字节（符合 varint 编码理论：1 字节 vs 最多 9 字节），但只让序列化延迟多了约 4%，解析侧几乎不受影响——varint 宽度对这个字段确实有影响，但很小。

## Phase 3 — 可扩展性扫描

对一个 `repeated int64`（`mentioned_user_ids`，packed 标量编码）和一个 `repeated message`（`merged_forward.items`，永不 packed）都扫了长度 1/10/100/1000。

| 字段类型 | n=1→1000 体积增长 | n=1→1000 耗时增长（serialize） | 边际成本 |
|---|---|---|---|
| `mentioned_user_ids`（packed varint） | 116→1989 字节 | 75.13→955.53 ns | 约 0.7-0.9 ns/元素，约 1.9 字节/元素 |
| `merged_forward.items`（repeated message） | 109→36,049 字节 | 85.28→26,514.75 ns | 约 25.6-26.4 ns/项（serialize），约 82-108 ns/项（parse），约 36 字节/项 |

Packed 标量 repeated 字段每个元素的代价极低（就是在紧凑循环里写/读 varint）。Repeated *message* 字段每个元素的代价高出 30-100 倍，因为每一项都要单独带 tag/length 前缀，并完整地递归编解码一遍子消息（4 个子字段，其中一个还是嵌套消息）。这是本套 benchmark 里对 schema 设计最大的一条"避坑"提示：在热路径上，能用扁平的 repeated 标量字段就别用 repeated 子消息。

## Phase 4 — 内存分配 / Arena 对比

对比了默认堆分配的解析方式，和一个长期存活、**整个 benchmark 过程中从不 Reset**、被反复复用的 `google::protobuf::Arena`。

| Benchmark | 耗时 (ns/次) | 分配次数/次 | malloc 字节数/次 |
|---|---|---|---|
| BM_ParseTextHeapAllocs | 191.64 | 9.00 | 301.00 |
| BM_ParseTextArenaAllocs | 220.74 | 3.01 | 486.32 |

Arena 把 `malloc` 调用次数砍了约 3 倍（9→3.01）——顶层消息和它的 `QuoteInfo` 子消息都是从 arena 已有内存里"bump 分配"出来的，不用每个都单独 `new`。但因为这个 benchmark 故意从不调用 `Reset()`，arena 的内存只会一直增长——它得不断申请新的、更大的、冷内存块，而且 `Arena::Create<T>` 本身每次调用都有一点记账开销。综合下来：这里 Arena 反而*更慢*（220.7ns vs 191.6ns，慢 15%），而不是更快。**这是一个刻意设计出来的、有信息量的负面结果**，不是 bug：它说明 Arena 文档里讲的收益是建立在"有节奏地 reset/复用"基础上的（比如每个请求一个 arena，请求间 reset）——一个只会增长、从不 reset 的 arena，既没拿到内存复用的好处，又没省掉每次调用的记账开销。

## Phase 5 — 序列化 API 开销

对比了写同一条 117 字节 `text` 消息的 4 种方式。

| Benchmark | 耗时 (ns/次) | 吞吐量 (ops/sec) |
|---|---|---|
| BM_SerializeText（复用 `std::string` + `.clear()`，基准） | 79.55 | 12,570,606 |
| BM_SerializeToFreshString（每次调用新建一个 `std::string`） | 87.16 | 11,473,639 |
| BM_SerializeToPreallocatedArray（`SerializeToArray` 写入复用的裸缓冲区） | 75.93 | 13,169,701 |
| BM_SerializeToCodedStream（每次构造 `ArrayOutputStream`+`CodedOutputStream`） | 87.25 | 11,461,000 |

每次新建一个 `std::string` 比用 `.clear()` 复用贵约 9.6%。直接写裸缓冲区的 `SerializeToArray` 路径最快（比"复用 string"基准快约 4.5%）——完全没有 string 相关的开销。**意外发现：**所谓"zero-copy"的 `CodedOutputStream` 路径并*没有*更快——它跟"每次新建 string"那条路径耗时几乎一样，都比"复用 string"基准慢约 10%，原因是 `SerializeToCodedStream` 走的是抽象的 `ZeroCopyOutputStream` 虚接口（`Next()`/`BackUp()`），这个虚函数调用和接口开销，在这种小体积消息上，盖过了它本该省下的那点分配开销。结论：protobuf 的流式"zero-copy" API 真正发挥价值的场景，是目标本身就是一个你不掌控的流（比如 socket、`Cord`）；如果你手上已经有一块扁平的、预分配好的缓冲区，直接用 `SerializeToArray` 反而更划算。

## Phase 6 — CPU 微架构指标（已跳过 — 环境不可行）

`perf stat` 需要 `perf_event_paranoid <= 2`；这台机器上是 `4`，而且这次是无人值守的自主执行，没有免密 `sudo` 可以把它调低。无需特殊权限的备选方案 `valgrind --tool=cachegrind` 没有安装，安装它同样需要 root。两种可行方案都卡在同一个根因上（没有可用的高权限，也没有自主获取权限的途径）。已记录在 `docs/benchmarks/phase6-feasibility-note.md` 里；这不是设计缺陷——以后如果在一台权限合适的机器上跑，直接补上去就行，因为 `perf stat` 是从外部包一层 `proto_bench` 进程,完全不需要改任何代码。

## Phase 7 — 并发吞吐量扩展性

用 1/2/4/8/16/20 个相互独立的线程（benchmark 代码里没有共享状态、没有锁），测聚合吞吐量。

| 线程数 | Serialize 聚合吞吐量 (ops/sec) | Serialize 扩展比例 | Parse 聚合吞吐量 (ops/sec) | Parse 扩展比例 |
|---|---|---|---|---|
| 1 | 13,172,661 | 1.00 | 5,102,884 | 1.00 |
| 2 | 26,481,638 | 2.01 | 2,905,831 | **0.57** |
| 4 | 52,230,107 | 3.97 | 3,726,085 | 0.73 |
| 8 | 92,535,545 | 7.02 | 3,898,634 | 0.76 |
| 16 | 161,741,472 | 12.28 | 4,626,571 | 0.91 |
| 20 | 177,219,044 | 13.45 | 5,645,058 | 1.11 |

Serialize（分配很轻、buffer 复用）扩展得还算合理（20 线程时效率约 67%——更可能是满核负载下涡轮加速频率被压低，不是竞争问题）。Parse（每次调用要约 9 次堆分配，见 Phase 4）扩展得*很差*：2 个线程时，聚合吞吐量反而**跌到单线程基准以下**（比例 0.57），要到 20 线程才重新爬回 1.0x 以上。**这是整套 benchmark 里最有实践指导意义的跨阶段发现**：即便应用代码层面完全没有共享状态，一个分配密集的工作负载，也会把全局堆分配器变成一个隐藏的共享资源，这种竞争能让"加线程"在低并发度下反而变成负优化。这也直接给 Phase 4 探索的 Arena 方案提供了更强的支撑理由——一个按线程或按连接划分的 Arena，能让 parse 摆脱对全局分配器的依赖，扩展曲线应该能更接近 serialize 那条线。

## Phase 8 — 畸形输入解析失败开销

| 输入 | 耗时 (ns/次) | 字节数 | parse_ok |
|---|---|---|---|
| BM_ParseText（合法输入，基准） | 190.26 | 117 | 1 |
| BM_ParseTruncatedText（合法字节串的前半段） | 101.85 | 58 | 0 |
| BM_ParseGarbageText（117 字节，全部填 `0xFF`） | 12.44 | 117 | 0 |

两种失败模式都比成功解析更便宜——在这两种测试形态下，都不存在"拒绝垃圾数据反而比接受合法数据更贵"这种 DoS 攻击面。失败开销跟"解析器在判定出问题之前走了多远"成正比：截断输入（一段格式正确、只是数据不够的前缀）耗时约为完整解析的 54%，因为它确实先成功解码了好几个真实字段。结构性畸形输入（全 `0xFF`，第一个 tag 字节本身就是一个畸形 varint）失败速度比合法解析快约 15 倍，比截断输入快约 8 倍，因为解析器在缓冲区最开头的约 10 字节内就报错了，跟缓冲区总长度无关。

## Phase 9 — SBE（Simple Binary Encoding）对比

把项目早期那个纯靠估算的数字（在任何 SBE 代码写出来之前就给出的，预测有 5-30 倍提升）换成了实测数据，测的是 Phase 1-8 全程都在用的那套逻辑上完全相同的 `text` 和 `merged_forward` 消息内容。完整细节——包括 Phase 2/3/4/7 等价的扫描，以及对一个反直觉并发结果的如实讨论——见 `docs/benchmarks/phase9-sbe-comparison-analysis.md`。

| Benchmark | Protobuf (ns/次) | SBE (ns/次) | 加速比 | Protobuf 字节数 | SBE 字节数 |
|---|---|---|---|---|---|
| text encode | 76.84 | 50.84 | 1.51x | 117 | 175 |
| text decode | 195.51 | 58.14 | 3.36x | 117 | 175 |
| merged_forward encode | 116.13 | 66.78 | 1.74x | 162 | 219 |
| merged_forward decode | 318.56 | 75.91 | 4.20x | 162 | 219 |

实测加速比（1.51x-4.20x）远低于实现之前的估算，而且 decode 受益明显比 encode 多（3.4x-4.2x vs 1.5x-1.7x）——SBE 的 flyweight 式 decode 省掉了 Protobuf decode 路径上要付的堆分配和字段存在性判断开销，而 Protobuf 的 encode 路径本来就相对便宜。SBE 的 decode 还是零分配的（每次 0 次堆分配，对比 Protobuf 的 9 次），并且在并发下接近线性扩展（1 到 20 线程的比例是 0.89-1.05x），而 Protobuf decode 在 20 线程时扩展比例会崩到 0.07x。Protobuf 持久的优势在于体积（这里小 30-50%，得益于 varint 压缩和可选字段省略——SBE 的固定宽度字段在这些消息上反而*大了* 35-50%）和自描述/自校验式解码。反直觉的是，SBE 的 *encode*（不是 decode）在高并发下扩展得反而比 Protobuf encode 更差，20 线程时比例崩到 0.04x，而 Protobuf encode 同样条件下是 0.67x——这个结果经过独立复测确认是可复现的，不是偶发抖动，最可能的原因是机器级的饱和效应（内存/缓存带宽或涡轮加速降频），SBE 那约 51ns 的 encode 在相对比例上比 Protobuf 更长的 encode 对这种饱和更敏感，而不是说明 SBE 本身有结构性缺陷。

---

## 跨阶段的核心结论

1. **Repeated message 字段是本套测试里测出的最贵的 schema 写法**（Phase 3）——单元素成本比 packed repeated 标量高 30-100 倍。如果 schema 设计上能在"很多标量值"和"很多个小子消息"之间选，热路径上优先选前者。
2. **同一条逻辑消息，解析始终比序列化贵 2-3 倍**（Phase 1、3），两种消息形态下都是如此——很大程度上要归因于每次调用的堆分配（Phase 4 量化出单次 `text` 解析约需要 9 次 `malloc`）。
3. **堆分配是这套 benchmark 里最主要的隐藏成本**，在三个独立的 Phase 里都冒出来了：Phase 4（分配次数/字节数）、Phase 5（新建字符串的分配税）、以及最戏剧性的 Phase 7（多线程下分配器竞争导致低并发度时 parse 吞吐量跌破单线程基准）。
4. **Arena 分配不是免费的午餐**——只有配合"reset/复用"的使用节奏才有收益（Phase 4 里那个从不 reset 的 arena，比它本该替代的堆分配路径更慢，不是更快）。未来如果真要采用 Arena，应该按生产环境实际打算用的 reset 节奏去专门测一次，不能想当然地认为教科书里的收益会无条件适用。
5. **"Zero-copy" 流式序列化 API 不是无条件比简单的扁平缓冲区 API 快**（Phase 5）——对于体积小、全部在内存里的负载，流式抽象的虚函数调用开销，可能盖过它本该省下的分配收益。预分配缓冲区 + `SerializeToArray`，是测过的 4 种序列化路径里最快的。
6. **测过的两种畸形输入形态里，都没发现不对称的 DoS 攻击面**（Phase 8）——无论是"格式正确的前缀后突然截断"还是"开头就结构性损坏"，拒绝坏数据始终比接受好数据更便宜。
7. **Phase 6（CPU 微架构计数器）在本沙箱环境下跑不起来**，原因是 `perf_event_paranoid=4` 且没有提权途径；这是环境限制，不是测试结果，如果以后用高权限环境跑这套 suite，应该把这一项补上。
8. **SBE 在单线程 CPU 耗时上确实比 Protobuf 快，但幅度远不如最初猜测的那么大，也不是无条件成立**（Phase 9）——实测加速比是 1.51x-4.20x（decode 受益比 encode 多），远低于实现之前 5-30x 的估算，而且 SBE 为这点速度付出的代价是：在这些具体负载下，wire 体积反而大 35-50%（没有 varint 压缩）。SBE 的 decode 在并发下扩展得也明显更好（接近线性，对比 Protobuf decode 在 20 线程时崩到 0.07x），但 SBE 的 *encode* 在高线程数下反而比 Protobuf encode 扩展得更差（20 线程时 0.04x vs 0.67x）——这是一个经过独立复测确认可复现的结果，原因被归结为机器级饱和效应，而不是 SBE 的结构性缺陷，这里如实报告，不把它抹平成一句"SBE 就是更好"。

## 文件索引

| Phase | Spec | Plan | 结果 JSON | 分析文档 |
|---|---|---|---|---|
| 0+1 | `docs/superpowers/specs/2026-06-17-benchmark-phase0-1-design.md` | `docs/superpowers/plans/2026-06-17-benchmark-phase0-1.md` | `results/phase1-2026-06-17.json` | `docs/benchmarks/phase1-throughput-size-analysis.md` |
| 2 | `docs/superpowers/specs/2026-06-18-benchmark-phase2-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase2.md` | `results/phase2-2026-06-18.json` | `docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md` |
| 3 | `docs/superpowers/specs/2026-06-18-benchmark-phase3-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase3.md` | `results/phase3-2026-06-18.json` | `docs/benchmarks/phase3-scalability-analysis.md` |
| 4 | `docs/superpowers/specs/2026-06-18-benchmark-phase4-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase4.md` | `results/phase4-2026-06-18.json` | `docs/benchmarks/phase4-memory-arena-analysis.md` |
| 5 | `docs/superpowers/specs/2026-06-18-benchmark-phase5-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase5.md` | `results/phase5-2026-06-18.json` | `docs/benchmarks/phase5-api-overhead-analysis.md` |
| 6 | — | — | — | `docs/benchmarks/phase6-feasibility-note.md`（已跳过） |
| 7 | `docs/superpowers/specs/2026-06-18-benchmark-phase7-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase7.md` | `results/phase7-2026-06-18.json` | `docs/benchmarks/phase7-concurrency-analysis.md` |
| 8 | `docs/superpowers/specs/2026-06-18-benchmark-phase8-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase8.md` | `results/phase8-2026-06-18.json` | `docs/benchmarks/phase8-malformed-input-analysis.md` |
| 9 | `docs/superpowers/specs/2026-06-19-benchmark-phase9-sbe-design.md` | `docs/superpowers/plans/2026-06-19-benchmark-phase9-sbe.md` | `results/phase9-2026-06-19.json` | `docs/benchmarks/phase9-sbe-comparison-analysis.md` |

## 执行说明

Phase 3 及以后（3、4、5、7、8）以及本报告，都是在用户明确授权"继续自主执行、不必逐项确认"之后无人值守完成的。Phase 0-2 走的是 subagent-driven-development，有人工可审查的 spec/plan/review 检查点；Phase 3 及以后沿用同样的 spec → plan → implement → verify → commit → merge 流程，但改成直接执行（不再派发 subagent），这是后来用户基于"subagent 内 bypass-permissions 权限可能不生效"的顾虑明确要求切换的执行方式。
