# Phase 1 — Throughput, Latency, and Size Analysis

Date: 2026-06-17
Raw data: `results/phase1-2026-06-17.json`

## Run conditions

This run had `cpu_scaling_enabled: true` and a non-idle host (`load_avg` around 3.4/3.4/2.0) per the JSON `context` block; Google Benchmark itself warns that CPU frequency scaling makes timings noisier and less trustworthy than a dedicated/idle benchmarking environment. The absolute ns/iter and ops/sec figures below should be read with that caveat in mind, though the relative comparisons (text vs merged_forward, serialize vs parse) are large enough to be robust to this noise.

## Results

| Benchmark | Operation | Message type | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeText | serialize | text | 76.23 | 13,118,917 | 117 |
| BM_ParseText | parse | text | 189.98 | 5,263,734 | 117 |
| BM_SerializeMergedForward | serialize | merged_forward | 115.34 | 8,669,697 | 162 |
| BM_ParseMergedForward | parse | merged_forward | 297.48 | 3,361,568 | 162 |

Throughput (ops/sec) = 1e9 / time (ns/iter).

## Observations

The merged_forward message type consistently demonstrates higher latency and larger serialized size compared to the text message type. Serialization of merged_forward is 51% slower (115.34 ns vs 76.23 ns), while parsing is 57% slower (297.48 ns vs 189.98 ns), with both serialization and parsing operations showing worse performance for the more complex message structure. The merged_forward messages are 38% larger on the wire (162 bytes vs 117 bytes), which accounts for the additional ForwardedItem nested messages in the schema. Parsing is consistently more expensive than serialization for both message types, suggesting the deserialization logic carries greater computational overhead—text parsing is 2.5x slower than text serialization, and merged_forward parsing is 2.6x slower than its serialization counterpart.

## Scope note

This covers Phase 1 only (throughput/latency/size on the existing 2 message types). Field-fill-rate, numeric encoding efficiency, scalability sweeps, Arena, API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
