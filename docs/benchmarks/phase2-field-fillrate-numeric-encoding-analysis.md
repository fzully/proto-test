# Phase 2 — Field-Fill-Rate and Numeric-Encoding-Efficiency Analysis

Date: 2026-06-18
Raw data: `results/phase2-2026-06-18.json`

## Field fill rate: sparse vs. full text message

| Benchmark | Operation | Fill rate | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeSparseText | serialize | sparse | 51.78 | 19,312,263 | 83 |
| BM_SerializeText | serialize | full (Phase 1) | 77.48 | 12,906,425 | 117 |
| BM_ParseSparseText | parse | sparse | 110.67 | 9,035,574 | 83 |
| BM_ParseText | parse | full (Phase 1) | 188.75 | 5,298,076 | 117 |

Omitting the quote and mentioned_user_ids fields saves 34 bytes per message (29.1% reduction from 117 bytes). Both serialization and parsing show proportional latency improvements: serialize improves by 33.2% while bytes decrease by 29.1%, and parse improves by 41.3% — indicating latency improvements outpace the size reduction, likely due to field processing overhead beyond just the wire format.

## Numeric encoding: small vs. snowflake-scale message_id

| Benchmark | Operation | message_id | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeSmallId | serialize | 1 | 80.58 | 12,410,199 | 116 |
| BM_SerializeLargeId | serialize | 1950123456789012345 | 84.00 | 11,904,939 | 124 |
| BM_ParseSmallId | parse | 1 | 194.94 | 5,129,877 | 116 |
| BM_ParseLargeId | parse | 1950123456789012345 | 194.18 | 5,149,741 | 124 |

The large message_id (19 digits) costs exactly 8 additional bytes compared to the small one (1 digit), which matches varint encoding expectations: a single-digit value encodes in 1 byte while a ~19-digit int64 (1950123456789012345) requires up to 9 bytes, accounting for the 8-byte difference. The latency impact is minimal and within noise margins: serialization degrades by only 4.2% and parse remains essentially flat (within 0.4%), indicating varint encoding overhead is amortized well at the per-operation level.

## Scope note

This covers Phase 2 only (field-fill-rate and message_id numeric-encoding efficiency, both isolated to the `text` message type). Scalability sweeps, Arena, API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
