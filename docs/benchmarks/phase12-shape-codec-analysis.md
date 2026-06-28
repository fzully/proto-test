# Phase 12 — Shape Codec Benchmark：Raw Binary vs Protobuf 编解码对比

Date: 2026-06-28（fixture 更新：每 shape 点数改为随机 5–7，均值 6）

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
| 每 shape point 数 | **5–7（随机均匀分布，均值 6）** |
| 总 point 数 | ~300,000（均值） |
| 坐标范围 | x, y ∈ [0, 100,000,000] |
| 生成方式 | 随机游走：第一个 point 绝对随机，后续 point 相对前一个 point 偏移 ≤ max_consecutive_delta |

`shape_bench`（Lite runtime）使用三组 delta 上限：**1,000 / 2,000 / 5,000**，模拟 UI/矢量图形场景的小幅偏移。  
`shape_full_bench`（Full runtime）使用三组 delta 上限：**10,000 / 100,000 / 500,000**，模拟地图/地理场景的大幅偏移。

两个可执行文件的 delta 范围不重叠，跨文件数值对比时以 payload 体积作为参照而非 delta 参数本身。

构造 50K shapes 耗时约 **1.46 ms**（含随机点数生成），位于 benchmark 循环体外，不计入编解码时延。

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
第一个 point 绝对，后续 point 存 delta，sint32 zigzag 编码使小 delta 压缩到 1–2 字节。
delta ≤ 5,000 时 payload 约 **1.59 MB**（比 Raw 小 36%）；delta ≤ 100,000 时约 **2.06 MB**（小 18%）。

### Protobuf Full（`optimize_for = SPEED`）

`proto/shape_full.proto`，链接 `libprotobuf`（完整反射支持），其余逻辑与 Lite 相同。
单独编译为 `shape_full_bench` 可执行文件以避免 lite/full 双链接冲突。

### Arena

`google::protobuf::Arena` 在循环体外创建，每次迭代调用 `arena.Reset()`。
Reset 只归零内部指针，不释放已申请的内存块，从第二次迭代起 50K Shape 对象的分配
退化为纯指针碰撞（pointer-bump），消除 malloc/free 热路径。

## 结果

所有时延为单次迭代 cpu_time（Google Benchmark 自动取多次迭代中位数）。

### 一、Raw Binary：绝对坐标 vs Delta 编码（shape_bench，delta=5,000）

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| Raw Binary（绝对坐标） | **0.68 ms** | **1.23 ms** | 2.50 MB |
| Raw Binary Delta | 0.74 ms | 1.26 ms | 2.50 MB |
| 差值 | +9%（慢） | +2%（持平） | 相同 |

Raw Binary Delta 对 payload 毫无帮助（fixed int32 宽度不随值变化），encode 还因多一次减法略慢。
**Raw Binary 的最优形式是直接存绝对坐标**，无需做 delta。

### 二、Protobuf Full lib vs Lite lib（delta 编码，近似同等 payload）

注：两个 benchmark 的 delta 参数不重叠，以接近的 payload 体积作参照。

| 方案 | Encode | Decode | Payload | delta 参数 |
|------|--------|--------|---------|----------|
| Protobuf Full（libprotobuf） | 5.80 ms | 5.88 ms | 1.69 MB | 10,000 |
| Protobuf Lite（libprotobuf-lite） | **4.62 ms** | **5.07 ms** | 1.59 MB | 5,000 |
| Lite 相对 Full（近似） | 快 ~20% | 快 ~14% | 小 6%（payload 不完全等价） |

payload 存在约 6% 差异（不同 delta 范围），数值仅供参考。对 NoDelta（绝对坐标，payload 一致）：

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| Full NoDelta（delta=100K） | **4.97 ms** | **5.74 ms** | 2.60 MB |
| Lite NoDelta（delta=5K） | 5.46 ms | 5.65 ms | 2.59 MB |

NoDelta 场景 payload 与 delta 参数无关，两者可直接对比：Full encode 比 Lite 快约 9%，差异来自
libprotobuf 对绝对坐标序列化路径的额外优化；Lite decode 略快 2%，基本持平。

### 三、Protobuf Full lib vs Full lib + Arena Reuse（delta=100,000）

| 方案 | Encode | Decode |
|------|--------|--------|
| Full 无 Arena | 5.05 ms | 5.80 ms |
| Full + Arena Reuse | **3.51 ms** | **3.96 ms** |
| 提升 | **30%** | **32%** |

### 四、Protobuf Lite lib vs Lite lib + Arena（delta=5,000）

| 方案 | Encode | Decode |
|------|--------|--------|
| Lite 无 Arena | 4.62 ms | 5.07 ms |
| Lite + Arena/iter | **3.00 ms** | **3.26 ms** |
| Lite + Arena Reuse | **3.00 ms** | **3.26 ms** |
| 提升 | **35%** | **36%** |

Arena 的收益来源：将 50K 次 `new Shape` + 50K 次 `delete` 替换为指针碰撞分配 + 一次
批量释放（Reset 归零指针）。Arena 本身不影响 varint 编解码路径，也不影响输出
`vector<Shape>` 的堆分配（那部分 Raw Binary 也有，两者持平）。

> **Arena per-iter vs Reuse 几乎无差异（< 1%）**：protobuf Arena 的 Reset 不归还
> 内存块给 OS，因此 per-iter 在首次迭代后 Arena 的块池已预热，后续构造/析构开销与
> Reset 相当。

### 五、delta 编码 vs 无 delta（Protobuf Lite）

delta 的收益随 delta 上限降低而增大——更小的差值对应更短的 varint 编码。

| delta 参数 | 方案 | Encode | Decode | Payload |
|-----------|------|--------|--------|---------|
| 1,000 | NoDelta | 5.47 ms | 5.68 ms | 2.59 MB |
| 1,000 | **Delta** | **4.77 ms** | **5.24 ms** | **1.57 MB** |
| 1,000 | delta 收益 | 快 13% | 快 8% | **小 39%** |
| 5,000 | NoDelta | 5.46 ms | 5.65 ms | 2.59 MB |
| 5,000 | **Delta** | **4.62 ms** | **5.07 ms** | **1.59 MB** |
| 5,000 | delta 收益 | 快 15% | 快 10% | **小 39%** |

即使 delta=5,000（最大步长约为坐标范围的 0.005%），zigzag 编码每个差值只需 2 字节，
payload 缩小 39%，encode 也因减少 cache line 写入而提速 15%。**对 Protobuf 应始终开启 delta。**

对照：Full runtime delta=100K 时，payload 仅缩小 21%，encode 无明显提速（+1.6% 反慢，噪声范围内）。
delta 压缩收益的大小完全由差值的实际分布决定。

### 六、最优 Protobuf vs 最优 Raw Binary

**最优 Protobuf**：Lite + Arena Reuse，delta=5,000（最小 delta 下 varint 最短）  
**最优 Raw Binary**：绝对坐标，任意 delta（值与速度无关）

| 方案 | Encode | Decode | Payload |
|------|--------|--------|---------|
| **Raw Binary**（绝对坐标） | **0.68 ms** | **1.23 ms** | 2.50 MB |
| **Protobuf Lite + Arena Reuse**（delta=5,000） | 3.00 ms | 3.26 ms | **1.59 MB** |
| Raw Binary 快多少 | **4.4×** | **2.7×** | Protobuf 小 36% |

### 七、全量对比表（shape_bench，50K shapes，delta=5,000）

| Codec | Encode | Decode | Payload |
|-------|--------|--------|---------|
| Raw Binary | **0.68 ms** | 1.23 ms | 2.50 MB |
| Raw Delta | 0.74 ms | 1.26 ms | 2.50 MB |
| Raw+LZ4 | 0.83 ms | 1.36 ms | 2.51 MB |
| Raw Delta+LZ4 | 2.81 ms | 1.55 ms | 2.43 MB |
| Proto Lite + Arena | 3.00 ms | **3.26 ms** | 1.59 MB |
| Proto Lite（无 Arena） | 4.62 ms | 5.07 ms | 1.59 MB |
| Proto Lite NoDelta | 5.46 ms | 5.65 ms | 2.59 MB |

Raw+LZ4 与 Raw 几乎无大小差异（2.50→2.51 MB），原因是绝对坐标的高位字节随机分布，
LZ4 无法找到规律性重复串；encode 也因 LZ4 压缩过程额外增加 ~0.15 ms。

Raw Delta+LZ4 在 delta=1,000 时压缩到 2.13 MB（encode 4.12 ms），delta=5,000 时降至
2.43 MB（encode 2.81 ms）——delta 越小，差值的高位 0 字节越多，LZ4 效率越高，但仍慢于
Proto Lite + Arena，且 payload 更大。

## 综合结论

| 维度 | 最优选择 | 备注 |
|------|---------|------|
| 编解码 CPU 最低 | **Raw Binary 绝对坐标** | encode 4.4× 快于最优 Protobuf |
| Payload 最小 | **Protobuf Lite + Arena + delta（小 delta）** | 比 Raw 小 36% |
| CPU 与 payload 折中 | **Protobuf Lite + Arena Reuse** | encode ~3.0 ms，payload ~1.6 MB |
| Arena 收益 | **两个 runtime 均提升 30–36%** | 需维护 Arena 生命周期 |
| Full vs Lite（NoDelta） | Full encode 快 9%，decode 持平 | Full 多出运行时反射能力 |
| Full vs Lite（Delta） | Lite 约快 20%（不同 delta 范围，近似值） | — |
| delta 对 Raw Binary | **无收益** | fixed int32 宽度不变，不应做 delta |
| delta 对 Protobuf（小步长） | **payload −39%，encode 快 15%** | delta ≤ 5,000 时收益显著 |
| delta 对 Protobuf（大步长） | **payload −21%，encode 持平** | delta ≥ 100,000 时收益递减 |

### 决策树

```
需要反射/动态消息？
  ├─ 是 → Protobuf Full + Arena Reuse + delta
  └─ 否 → 带宽是瓶颈？
           ├─ 是（WAN/低带宽） → Protobuf Lite + Arena Reuse + delta（payload 小 36%）
           └─ 否（内网/高带宽） → Raw Binary 绝对坐标（encode 4.4× 更快）
```

## 相关文件

| 文件 | 说明 |
|------|------|
| `proto/shape.proto` | Shape/ShapeBatch 定义，LITE_RUNTIME |
| `proto/shape_full.proto` | 同一 schema，SPEED（full runtime） |
| `src/shape_fixtures.h/cpp` | 50K shapes 确定性生成（随机游走，5–7 点/shape） |
| `src/shape_bench.cpp` | Raw Binary / LZ4 / Protobuf Lite / Arena 全部 benchmark（delta=1K/2K/5K） |
| `src/shape_full_bench.cpp` | Protobuf Full runtime benchmark（delta=10K/100K/500K） |
| `results/phase12-shape-random-pts-2026-06-28.json` | shape_bench 原始结果 |
| `results/phase12-shape-full-random-pts-2026-06-28.json` | shape_full_bench 原始结果 |
