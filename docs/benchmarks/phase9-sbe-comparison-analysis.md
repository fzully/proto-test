# Phase 9 — SBE (Simple Binary Encoding) Comparison Analysis

Date: 2026-06-19
Raw data: `results/phase9-2026-06-19.json` (SBE + same-run Protobuf baselines), `results/phase4-2026-06-18.json` (allocation reference)

This phase replaces the earlier pure estimate (made before any SBE code existed) with measured numbers, for the identical logical message content used throughout Phases 1-8.

## Phase 1 equivalent: basic encode/decode

| Benchmark | Protobuf (ns/iter) | SBE (ns/iter) | Speedup | Protobuf bytes | SBE bytes |
|---|---|---|---|---|---|
| text encode | 76.84 | 50.84 | 1.51x | 117 | 175 |
| text decode | 195.51 | 58.14 | 3.36x | 117 | 175 |
| merged_forward encode | 116.13 | 66.78 | 1.74x | 162 | 219 |
| merged_forward decode | 318.56 | 75.91 | 4.20x | 162 | 219 |

The real speedups (1.51x-4.20x) are well below the earlier pure estimate of 5-30x; decode benefits far more than encode (3.4x-4.2x vs 1.5x-1.7x), consistent with Protobuf's decode path paying for heap allocation and field-presence dispatch that SBE's flyweight decode skips entirely, while Protobuf's encode path is comparatively cheap already. On the wire, SBE is larger for both messages (175 vs 117 bytes for text, +49.6%; 219 vs 162 bytes for merged_forward, +35.2%), which matches the "no varint compaction" prediction: SBE uses fixed-width fields (e.g. a full 8-byte int64 for every ID regardless of magnitude) and has no equivalent of Protobuf's variable-length integer encoding, so small values that Protobuf would compact into 1-2 bytes cost SBE their full fixed width.

## Phase 2 equivalent: sparse fill rate and ID width sensitivity

| Benchmark | Protobuf (ns/iter) | SBE (ns/iter) |
|---|---|---|
| sparse text encode | 51.49 | 35.02 |
| sparse text decode | 114.91 | 48.90 |
| small-ID text encode | 76.54 | 50.94 |
| large-ID text encode | 78.77 | 50.79 |

The small-ID vs large-ID prediction is confirmed: SBE's encode time is flat (50.94 ns vs 50.79 ns, a -0.3% difference, within noise) and its wire size is identical (175 bytes both ways), because fixed-width fields cost the same regardless of value magnitude. Protobuf, by contrast, shows a real varint-driven difference: encode time rises 2.9% (76.54 -> 78.77 ns) and size grows by 8 bytes (116 -> 124) when the message_id grows from 1 digit to 19 digits. The sparse-vs-full prediction is only partly confirmed: on the encode side SBE's improvement from full to sparse (31.1%) is close to Protobuf's (33.0%), but on the decode side SBE's improvement (15.9%) is markedly smaller than Protobuf's (41.2%) rather than larger — SBE's flyweight decode is already so cheap (58 ns) that skipping two fields saves proportionally less than it does for Protobuf's heavier per-field decode path.

## Phase 3 equivalent: scalability sweep (n=1/10/100/1000)

| n | Protobuf mentions encode (ns) | SBE mentions encode (ns) | Protobuf merged items encode (ns) | SBE merged items encode (ns) |
|---|---|---|---|---|
| 1 | 74.47 | 51.08 | 84.00 | 49.14 |
| 10 | 82.77 | 52.32 | 356.34 | 175.69 |
| 100 | 155.46 | 75.61 | 3096.65 | 1443.47 |
| 1000 | 972.60 | 341.50 | 30896.29 | 14573.74 |

Per-element marginal cost ((n=1000 time - n=1 time) / 999): mentions (a repeated int32 field) costs Protobuf 0.90 ns/item and SBE 0.29 ns/item, both far below the 25-108 ns/item range Phase 3 reported — that range applied to repeated *message* fields, not scalar repeated fields, so this row is not a like-for-like match to that prediction and the low absolute numbers here are expected, not a discrepancy. Merged items (a repeated message field, the actual analog of Phase 3's 25-108 ns/item finding) costs Protobuf 30.84 ns/item and SBE 14.54 ns/item — SBE's repeating-group cost is indeed within the same order of magnitude as Protobuf's repeated-message cost (about half, not negligible), confirming that SBE's repeating-group encoding does not eliminate per-item overhead, it roughly halves it. So the "does SBE scale anywhere near as steeply" answer is: for message-typed repeated fields, yes — SBE scales at a meaningfully gentler but still clearly linear and non-trivial per-item rate (14.5 ns/item vs 30.8 ns/item).

## Phase 4 equivalent: decode allocation count

| Benchmark | allocs_per_iter |
|---|---|
| BM_ParseTextHeapAllocs (Protobuf, from Phase 4) | 9.0 |
| BM_DecodeTextHeapAllocsSbe (this phase) | 0.0 |

The "flyweight decode is zero-allocation" prediction is fully confirmed: SBE's decode performs exactly 0 heap allocations per iteration versus Protobuf's 9, because SBE decode only wraps the caller-provided buffer with field-accessor objects rather than constructing owned strings/sub-messages on the heap.

## Phase 7 equivalent: concurrency scaling

| Threads | Protobuf encode scaling ratio | SBE encode scaling ratio | Protobuf decode scaling ratio | SBE decode scaling ratio |
|---|---|---|---|---|
| 1 | 1.00 | 1.00 | 1.00 | 1.00 |
| 2 | 0.99 | 0.35 | 0.71 | 1.00 |
| 4 | 0.94 | 0.18 | 0.30 | 1.00 |
| 8 | 0.85 | 0.09 | 0.16 | 1.05 |
| 16 | 0.72 | 0.05 | 0.09 | 0.99 |
| 20 | 0.67 | 0.04 | 0.07 | 0.89 |

SBE decode does scale close to linear (ratios stay between 0.89 and 1.05 across all thread counts) and far better than Protobuf decode, which collapses to 0.07x at 20 threads — fully consistent with the zero-allocation finding above, since Protobuf's degradation is almost certainly allocator/lock contention under concurrent heap traffic that SBE's allocation-free decode never incurs. However, SBE *encode* shows the opposite and unexpected pattern: its scaling ratio collapses even more severely than Protobuf's, dropping to 0.04x at 20 threads versus Protobuf encode's comparatively mild decline to 0.67x. This does not match a simple "SBE always scales better" narrative. Both SBE benchmarks use a thread-local stack buffer with no shared state, so this is not a lock-contention artifact in the code; single-threaded SBE encode is only ~51 ns/iter, meaning at high thread counts the 20 cores are issuing hundreds of millions of tiny encode calls per second, and the degradation likely reflects machine-level effects (memory/cache bandwidth saturation or CPU frequency/turbo throttling under all-core load) that a ~51 ns operation is far more sensitive to, in relative terms, than Protobuf's much longer ~75-318 ns operations. This is a genuine measured result, reported as-is rather than adjusted to match the predicted narrative.

## What this phase did not test (and why)

No SBE equivalent of Phase 5 (serialization API overhead — SBE has only one natural encoding path, a flyweight wrap of a caller-provided buffer, so there is no multi-API tradeoff to measure) or Phase 8 (malformed-input cost — SBE decode has no self-validating failure mode; constructing one would mean benchmarking a hand-written bounds-checking wrapper we wrote ourselves, not the library, and risks undefined behavior if done incorrectly). See `docs/superpowers/specs/2026-06-19-benchmark-phase9-sbe-design.md` for the full reasoning.

## Cross-cutting conclusion

Across single-threaded operations, SBE is roughly 1.5x-4.2x faster than Protobuf, with the larger gains concentrated on decode (where Protobuf pays for heap allocation and field dispatch that SBE's flyweight decode skips) rather than encode (where Protobuf is already comparatively cheap). Protobuf's clear and durable advantage is wire size for variable-magnitude or sparse data: it is 30-50% smaller for these specific messages thanks to varint compaction and optional-field omission, and it remains self-describing/self-validating at decode time (Phase 8 territory), a property SBE deliberately trades away for speed. Protobuf also held up better under heavy concurrent *encode* load in this particular run, though that result is unexpected and may reflect machine-level saturation effects rather than a structural SBE weakness. For the protobuf-vs-SBE tradeoff to flip in a real system, the workload would need messages dominated by small/zero/absent field values at high volume (where varint and optional-field savings compound into real bandwidth and storage savings that outweigh SBE's CPU advantage), or a requirement for safe decoding of untrusted/partially-corrupt input without a hand-rolled validation layer — both scenarios where Protobuf's self-describing wire format and compact encoding matter more than raw single-message encode/decode latency.
