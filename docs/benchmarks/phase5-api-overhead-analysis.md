# Phase 5 — Serialization API Overhead Analysis

Date: 2026-06-18
Raw data: `results/phase5-2026-06-18.json`

## Run conditions

`cpu_scaling_enabled: true` per the JSON `context` block (consistent with prior phases on this host).

## Comparison: 4 ways to serialize the same `text` message

| Benchmark | Time (ns/iter) | Throughput (ops/sec) | Bytes |
|---|---|---|---|
| BM_SerializeText (baseline: reused `std::string` + `.clear()`) | 79.55 | 12,570,606 | 117 |
| BM_SerializeToFreshString (new `std::string` every iteration) | 87.16 | 11,473,639 | 117 |
| BM_SerializeToPreallocatedArray (`SerializeToArray` into a reused raw buffer) | 75.93 | 13,169,701 | 117 |
| BM_SerializeToCodedStream (`ArrayOutputStream`+`CodedOutputStream` per iteration) | 87.25 | 11,461,000 | 117 |

**Fresh string vs reused string.** Allocating a brand-new `std::string` every iteration (`BM_SerializeToFreshString`) costs ~7.6ns more per call than reusing one via `.clear()` (87.16 vs 79.55ns, ~9.6% slower) — `clear()` keeps the existing heap buffer's capacity, so the reused-string baseline already avoids the per-call allocator round-trip that the fresh-string variant pays every time. This confirms the allocation tax is real, just smaller than one might expect, because for a 117-byte payload the buffer growth itself is cheap; the cost is almost entirely the allocator call, not copying bytes.

**Preallocated raw array is the fastest path.** `SerializeToArray` into a buffer allocated once outside the loop (`BM_SerializeToPreallocatedArray`, 75.93ns) is the fastest of the four, ~4.5% faster than the reused-string baseline — consistent with it being the only path with zero `std::string` machinery (no length tracking, no potential reallocation check) in the timed region at all.

**The "zero-copy" CodedOutputStream path is *not* faster — it's the slowest, tied with fresh-string allocation.** This is the most notable finding of this phase: building a `CodedOutputStream` over an `ArrayOutputStream` each iteration (87.25ns) is essentially as slow as allocating a fresh `std::string` (87.16ns), despite writing into a preallocated buffer with no heap allocation in the loop. The likely explanation is that `SerializeToCodedStream` goes through `MessageLite`'s generic stream-based serialization path, which talks to the destination through the abstract `ZeroCopyOutputStream` virtual interface (`Next()`/`BackUp()` calls to fetch/return buffer chunks) — that virtual-dispatch and bookkeeping overhead outweighs the benefit of avoiding allocation for a payload this small. `SerializeToArray`, by contrast, can write directly into a flat pointer without going through that streaming abstraction, which is why it wins despite conceptually doing "the same job." The lesson: protobuf's stream-based "zero-copy" APIs are designed to avoid copying when the destination is itself a stream you don't control (e.g. writing into a network socket or a `Cord`) — when you already have a flat, fully preallocated buffer in hand, the plain `SerializeToArray` call is the better choice, not the streaming API.

## Scope note

This covers Phase 5 only (serialize-side API comparison for the `text` message type). Parse-side API variants, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
