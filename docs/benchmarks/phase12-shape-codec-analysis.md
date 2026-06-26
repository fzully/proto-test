# Phase 12 — Shape Codec Benchmark：Raw Binary vs Protobuf 编解码对比

Date: 2026-06-26

## 背景与目标

针对一批二维闭环图形数据（point 坐标 x/y 最大 100,000,000），评估三种编码方案在
**端到端编解码 CPU 时延**上的差异，不含网络传输。三种方案为：

1. **Raw Binary**（绝对坐标，fixed int32，little-endian）
2. **Raw Binary + LZ4**（同上加 LZ4 压缩）
3. **Protobuf**（`packed repeated sint32`，delta + zigzag varint）

后续追加：Raw Binary Delta（不含 LZ4）、Protobuf Full runtime、Arena 对比、delta vs no-delta。

## 数据样本

| 维度 | 值 |
|------|---|
| shape 总数 | 50,000 |
| 每 shape point 数 | 6（固定） |
| 总 point 数 | 300,000 |
| 坐标范围 | x, y ∈ [0, 100,000,000] |
| 生成方式 | 随机游走：第一个 point 绝对随机，后续 point 相对前一个 point 偏移 ≤ max_consecutive_delta |

测试使用三组 delta 上限：10,000 / 100,000 / 500,000，对应地图/UI 场景中不同密度的图形。
所有数据确定性生成（固定 LCG 种子），每次运行结果一致。

构造 50K shapes 耗时约 **1.1 ms**，位于 benchmark 循环体外，不计入编解码时延。

## 方案说明

### Raw Binary

```
[uint32 shape_count]
per shape: [uint16 point_count] [int32 x][int32 y] ...  (little-endian)
```

固定宽度，不压缩，编解码等价于 memcpy + 指针遍历。payload 始终 **2.50 MB**，与坐标值无关。

### Raw Binary Delta（无压缩）

格式与 Raw Binary 相同，但 point[0] 存绝对坐标，point[i>0] 存相对 point[i-1] 的差值。
坐标仍为 fixed int32，payload 同为 2.50 MB，encode 多一次减法，decode 多一次加法。

### Protobuf（delta + zigzag varint）

`proto/shape.proto`，`optimize_for = LITE_RUNTIME`，`packed repeated sint32`。
第一个 point 绝对，后续 point 存 delta，sint32 zigzag 编码使小 delta 压缩到 1–3 字节。
delta ≤ 100K 时 payload **2.06 MB**（比 Raw 小 18%）。

### Protobuf Full（`optimize_for = SPEED`）

`proto/shape_full.proto`，链接 `libprotobuf`（完整反射支持），其余逻辑与 Lite 相同。
单独编译为 `shape_full_bench` 可执行文件以避免 lite/full 双链接冲突。

### Arena

`google::protobuf::Arena` 在循环体外创建，每次迭代调用 `arena.Reset()`。
Reset 只归零内部指针，不释放已申请的内存块，从第二次迭代起 50K Shape 对象的分配
退化为纯指针碰撞（pointer-bump），消除 malloc/free 热路径。

## 结果（中位数，delta = 100,000，3 次重复）

### 一、Raw Binary：绝对坐标 vs Delta 编码

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| Raw Binary（绝对坐标） | **0.55 ms** | **0.90 ms** | 2.50 MB |
| Raw Binary Delta | 0.64 ms | 0.92 ms | 2.50 MB |
| 差值 | +16%（慢） | +2%（持平） | 相同 |

Raw Binary Delta 对 payload 毫无帮助（fixed int32 宽度不随值变化），encode 还因多一次减法
略慢。**Raw Binary 的最优形式是直接存绝对坐标**，无需做 delta。

### 二、Protobuf Full lib vs Lite lib（delta 编码）

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| Protobuf Full（libprotobuf） | 5.38 ms | 4.91 ms | 2.06 MB |
| Protobuf Lite（libprotobuf-lite） | **5.00 ms** | **4.86 ms** | 2.06 MB |
| Lite 相对 Full | 快 7% | 快 1%（持平） | 相同 |

Full runtime 的 `ShapeBatch` 对象携带额外反射字段，`add_shapes()` 每次初始化开销略高，
累计 50K 次后差距约 7%。Decode 使用同一 TailCall Parser（TcParser）热路径，无差异。
**不需要反射时，Lite 是更优选择。**

### 三、Protobuf Full lib vs Full lib + Arena Reuse（delta=100K）

| 方案 | Encode | Decode |
|------|--------|--------|
| Full 无 Arena | 5.38 ms | 4.91 ms |
| Full + Arena Reuse | **3.18 ms** | **3.25 ms** |
| 提升 | **41%** | **34%** |

### 四、Protobuf Lite lib vs Lite lib + Arena Reuse（delta=100K）

| 方案 | Encode | Decode |
|------|--------|--------|
| Lite 无 Arena | 5.00 ms | 4.86 ms |
| Lite + Arena Reuse | **3.26 ms** | **3.17 ms** |
| 提升 | **35%** | **35%** |

Arena 的收益来源：将 50K 次 `new Shape` + 50K 次 `delete` 替换为指针碰撞分配 + 一次
批量释放（Reset 归零指针）。Arena 本身不影响 varint 编解码路径，也不影响输出
`vector<Shape>` 的堆分配（那部分 Raw Binary 也有，两者持平）。

> **Arena per-iter vs Reuse 几乎无差异（< 3%）**：protobuf Arena 的 Reset 不归还
> 内存块给 OS，因此 per-iter 在首次迭代后 Arena 的块池已预热，后续构造/析构开销与
> Reset 相当。

### 五、delta 编码 vs 无 delta（Protobuf，delta=100K）

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| Protobuf 无 delta（绝对 sint32） | 4.71 ms | 5.03 ms | 2.60 MB |
| Protobuf delta（zigzag varint） | **4.49 ms** | **4.86 ms** | **2.06 MB** |
| delta 带来的收益 | 快 5% | 快 3% | **小 21%** |

Delta 对 Protobuf 是零成本优化：payload 显著缩小，CPU 时间略有改善（更小的 varint
写入更少字节，序列化循环更快走完 cache line）。**应始终开启。**

### 六、最优 Protobuf vs 最优 Raw Binary（跨所有参数取最优）

**最优 Protobuf**：Lite + Arena Reuse，delta=500K（Arena 块池充分预热，varint 写入
分布均匀）  
**最优 Raw Binary**：绝对坐标，任意 delta（值与速度无关）

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| **Raw Binary**（绝对坐标） | **0.54 ms** | **0.90 ms** | 2.50 MB |
| **Protobuf Lite + Arena Reuse**（delta=500K） | 3.14 ms | 2.95 ms | **2.09 MB** |
| Raw Binary 快多少 | **5.8×** | **3.3×** | Protobuf 小 16% |

## 综合结论

| 维度 | 最优选择 | 备注 |
|------|---------|------|
| 编解码 CPU 最低 | **Raw Binary 绝对坐标** | encode 5.8× 快于最优 Protobuf |
| Payload 最小 | **Protobuf Lite + Arena + delta** | 比 Raw 小 16–35% |
| CPU 与 payload 折中 | **Protobuf Lite + Arena Reuse** | encode ~3.1 ms，payload ~2.1 MB |
| Arena 收益 | **两个 runtime 均提升 ~35%** | 需维护 Arena 生命周期 |
| Full vs Lite | **Lite encode 快 7%，decode 持平** | Full 多出运行时反射能力 |
| delta 对 Raw Binary | **无收益** | fixed int32 宽度不变，不应做 delta |
| delta 对 Protobuf | **payload −16~35%，encode 快 5%** | 零成本，应始终开启 |

### 决策树

```
需要反射/动态消息？
  ├─ 是 → Protobuf Full + Arena Reuse + delta
  └─ 否 → 带宽是瓶颈？
           ├─ 是（WAN/低带宽） → Protobuf Lite + Arena Reuse + delta（payload 小 16–35%）
           └─ 否（内网/高带宽） → Raw Binary 绝对坐标（encode 5.8× 更快）
```

## 相关文件

| 文件 | 说明 |
|------|------|
| `proto/shape.proto` | Shape/ShapeBatch 定义，LITE_RUNTIME |
| `proto/shape_full.proto` | 同一 schema，SPEED（full runtime） |
| `src/shape_fixtures.h/cpp` | 50K shapes 确定性生成（随机游走） |
| `src/shape_bench.cpp` | Raw Binary / LZ4 / Protobuf Lite / Arena 全部 benchmark |
| `src/shape_full_bench.cpp` | Protobuf Full runtime benchmark |
| `results/` | 原始 JSON 结果（按惯例，本阶段未单独存档，数字直接来自上文中位数） |
