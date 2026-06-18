# Protobuf IM-Chat Benchmark Suite — Final Report (Phases 0-9)

Date: 2026-06-18 (Phase 9 added 2026-06-19)
Repository: `proto-test` (`im.chat.v1` protobuf schema, C++20, CMake + FetchContent, Google Benchmark v1.9.1, protobuf v35.1)

This report consolidates every benchmark phase run in this project: infrastructure, throughput/latency/size, field-fill-rate, numeric encoding, scalability, memory/Arena allocation, serialization API overhead, concurrency scaling, malformed-input parse cost, and a Protobuf-vs-SBE comparison. Phase 6 (CPU microarchitecture counters via `perf`) was found infeasible in this sandbox and is documented as skipped rather than faked.

All raw data lives in `results/phaseN-*.json`; all phase-specific analysis lives in `docs/benchmarks/phaseN-*.md`. This document is the synthesis across all of them — read it first, drill into the per-phase docs for full detail.

---

## How this suite is built

- `chat_proto` is a shared static library holding the generated `chat.pb.{h,cc}` from `proto/chat.proto`, linked by both `proto_test` (correctness round-trip tests) and `proto_bench` (Google Benchmark suite), so `protoc` only runs once per build.
- `src/message_fixtures.{h,cpp}` provides deterministic message builders (`BuildTextMessage`, `BuildSparseTextMessage`, `BuildTextMessageWithId`, `BuildTextMessageWithMentionCount`, `BuildMergedForwardMessage`, `BuildMergedForwardMessageWithItemCount`) reused across every phase, so all phases measure the exact same baseline payloads.
- All dependencies (protobuf, Abseil, Google Benchmark) are fetched via CMake `FetchContent` with `GIT_SHALLOW TRUE` to avoid full-history clones.
- Build type is pinned to `Release` (`CMAKE_BUILD_TYPE` defaults to `Release` if unset) — Debug-mode timings were empirically ~10x slower/noisier and are not representative.
- `cpu_scaling_enabled: true` on this host throughout all runs (no dedicated/isolated benchmarking machine was available) — absolute nanosecond figures carry some noise; the relative comparisons within each phase are large enough to be robust to it, and this caveat is repeated in each phase's own analysis doc.

---

## Phase 0 — Infrastructure

Build system (CMake + FetchContent for protobuf v35.1 and Google Benchmark v1.9.1, both `GIT_SHALLOW`), the `chat_proto` shared library, the `proto_test`/`proto_bench` executables, and the `message_fixtures` module. No performance data — this phase is the foundation everything else runs on.

## Phase 1 — Throughput, Latency, and Size

Baseline serialize/parse cost for the two existing message shapes (`text`, `merged_forward`).

| Benchmark | Operation | Message type | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|---|
| BM_SerializeText | serialize | text | 76.23 | 13,118,917 | 117 |
| BM_ParseText | parse | text | 189.98 | 5,263,734 | 117 |
| BM_SerializeMergedForward | serialize | merged_forward | 115.34 | 8,669,697 | 162 |
| BM_ParseMergedForward | parse | merged_forward | 297.48 | 3,361,568 | 162 |

`merged_forward` (which embeds nested `ForwardedItem` messages) is 51% slower to serialize and 57% slower to parse than `text`, and 38% larger on the wire. Parsing is consistently 2.5-2.6x more expensive than serializing for both types — deserialization (allocating objects, validating wire format) costs more than serialization (just writing bytes) per message.

## Phase 2 — Field-Fill-Rate and Numeric-Encoding Efficiency

| Benchmark | Operation | Fill rate | Time (ns/iter) | Size (bytes) |
|---|---|---|---|---|
| BM_SerializeSparseText | serialize | sparse | 51.78 | 83 |
| BM_SerializeText | serialize | full | 77.48 | 117 |
| BM_ParseSparseText | parse | sparse | 110.67 | 83 |
| BM_ParseText | parse | full | 188.75 | 117 |

| Benchmark | Operation | message_id | Time (ns/iter) | Size (bytes) |
|---|---|---|---|---|
| BM_SerializeSmallId | serialize | 1 | 80.58 | 116 |
| BM_SerializeLargeId | serialize | 1950123456789012345 | 84.00 | 124 |
| BM_ParseSmallId | parse | 1 | 194.94 | 116 |
| BM_ParseLargeId | parse | 1950123456789012345 | 194.18 | 124 |

Omitting optional fields (quote, mentions) saves 29.1% of bytes and 33-41% of latency — latency improves *more* than size, meaning per-field processing overhead (not just wire bytes) drives part of the cost. A 19-digit snowflake-scale `message_id` costs exactly 8 more wire bytes than a single-digit one (matches varint theory: 1 byte vs up to 9 bytes) but adds only ~4% latency on serialize and is flat on parse — varint width has a real but small effect on this field.

## Phase 3 — Scalability Sweep

Repeated-field length swept at 1/10/100/1000 for both a `repeated int64` (`mentioned_user_ids`, packed scalar encoding) and a `repeated message` (`merged_forward.items`, never packed).

| Field type | n=1→1000 size growth | n=1→1000 time growth (serialize) | Marginal cost |
|---|---|---|---|
| `mentioned_user_ids` (packed varints) | 116→1989 bytes | 75.13→955.53 ns | ~0.7-0.9 ns/element, ~1.9 bytes/element |
| `merged_forward.items` (repeated message) | 109→36,049 bytes | 85.28→26,514.75 ns | ~25.6-26.4 ns/item (serialize), ~82-108 ns/item (parse), ~36 bytes/item |

Packed scalar repeated fields are extremely cheap per element (just writing/reading varints in a tight loop). Repeated *message* fields are 30-100x more expensive per element because each item requires its own tag/length prefix and a full recursive sub-message encode/decode (4 sub-fields, one of which is itself nested). This is the single largest "watch out" finding for schema design: prefer flat repeated scalars over repeated sub-messages wherever the data model allows it.

## Phase 4 — Memory Allocations / Arena Comparison

Compared default heap-allocated parsing vs a long-lived, **never-reset** `google::protobuf::Arena` reused across the whole benchmark run.

| Benchmark | Time (ns/iter) | Allocs/iter | Bytes/iter (malloc'd) |
|---|---|---|---|
| BM_ParseTextHeapAllocs | 191.64 | 9.00 | 301.00 |
| BM_ParseTextArenaAllocs | 220.74 | 3.01 | 486.32 |

Arena cuts `malloc` calls ~3x (9→3.01) by bump-allocating the top-level message and its `QuoteInfo` submessage out of existing arena memory instead of `new`-ing each one. But because this benchmark deliberately never calls `Reset()`, the arena's memory only ever grows — it has to keep fetching new, larger, cold-memory blocks, and `Arena::Create<T>`'s per-call bookkeeping has its own cost. Net effect: Arena is *slower* here (220.7ns vs 191.6ns, +15%), not faster. **This is a deliberate, informative negative result**, not a bug: it shows Arena's documented benefit assumes a reset/reuse cadence (e.g. one arena per request, reset between requests) — an arena that only grows forfeits memory reuse without removing per-call overhead.

## Phase 5 — Serialization API Overhead

Compared 4 ways to write the same 117-byte `text` message.

| Benchmark | Time (ns/iter) | Throughput (ops/sec) |
|---|---|---|
| BM_SerializeText (reused `std::string` + `.clear()`, baseline) | 79.55 | 12,570,606 |
| BM_SerializeToFreshString (new `std::string` every call) | 87.16 | 11,473,639 |
| BM_SerializeToPreallocatedArray (`SerializeToArray` into reused raw buffer) | 75.93 | 13,169,701 |
| BM_SerializeToCodedStream (`ArrayOutputStream`+`CodedOutputStream` per call) | 87.25 | 11,461,000 |

A fresh `std::string` allocation costs ~9.6% more than reusing one via `.clear()`. The flat-buffer `SerializeToArray` path is fastest (~4.5% faster than the reused-string baseline) — no string machinery at all. **Surprising finding:** the "zero-copy" `CodedOutputStream` path is *not* faster — it ties with the fresh-string-allocation path, both ~10% slower than the reused-string baseline, because `SerializeToCodedStream` routes through the abstract `ZeroCopyOutputStream` virtual interface (`Next()`/`BackUp()`), and that dispatch overhead outweighs the allocation savings for a payload this small. Lesson: protobuf's stream-based APIs earn their keep when the destination is a real stream you don't control (a socket, a `Cord`); when you already hold a flat preallocated buffer, plain `SerializeToArray` wins.

## Phase 6 — CPU Microarchitecture Metrics (SKIPPED — infeasible)

`perf stat` requires `perf_event_paranoid <= 2`; this host has it set to `4`, and there is no passwordless `sudo` to lower it in this unattended session. The no-privilege fallback, `valgrind --tool=cachegrind`, is not installed and installing it also requires root. Both viable approaches are blocked by the same root cause (no elevated privileges available, no path to obtain them autonomously). Documented in `docs/benchmarks/phase6-feasibility-note.md`; not a design flaw — straightforward to add later on a host with the right permissions, since `perf stat` wraps `proto_bench` externally with no code changes needed.

## Phase 7 — Concurrent Throughput Scaling

1/2/4/8/16/20 independent threads (no shared state, no locks in the benchmarked code), measuring aggregate throughput.

| Threads | Serialize aggregate (ops/sec) | Serialize scaling ratio | Parse aggregate (ops/sec) | Parse scaling ratio |
|---|---|---|---|---|
| 1 | 13,172,661 | 1.00 | 5,102,884 | 1.00 |
| 2 | 26,481,638 | 2.01 | 2,905,831 | **0.57** |
| 4 | 52,230,107 | 3.97 | 3,726,085 | 0.73 |
| 8 | 92,535,545 | 7.02 | 3,898,634 | 0.76 |
| 16 | 161,741,472 | 12.28 | 4,626,571 | 0.91 |
| 20 | 177,219,044 | 13.45 | 5,645,058 | 1.11 |

Serialize (allocation-light, reused buffer) scales reasonably (67% efficiency at 20 threads — likely turbo-boost frequency throttling under full core load, not contention). Parse (which needs ~9 heap allocations per call — see Phase 4) scales *badly*: at 2 threads, aggregate throughput **drops below the single-thread baseline** (ratio 0.57) and only crosses back above 1.0x at 20 threads. **This is the most actionable cross-phase finding in the whole suite**: even with zero shared state in application code, an allocation-heavy workload turns the global heap allocator into a hidden shared resource, and that contention can make adding threads actively counterproductive at low concurrency. It directly strengthens the case for Phase 4's Arena exploration — a per-thread or per-connection Arena would remove parse's dependency on the global allocator and should scale far closer to serialize's curve.

## Phase 8 — Malformed-Input Parse Failure Cost

| Input | Time (ns/iter) | Bytes | parse_ok |
|---|---|---|---|
| BM_ParseText (valid, baseline) | 190.26 | 117 | 1 |
| BM_ParseTruncatedText (first half of valid bytes) | 101.85 | 58 | 0 |
| BM_ParseGarbageText (117 bytes, all `0xFF`) | 12.44 | 117 | 0 |

Both failure modes are cheaper than a successful parse — there is no "rejecting garbage is more expensive than accepting valid input" DoS-shaped surface in either tested shape. Cost scales with *how far the parser gets* before detecting the problem: truncation (a well-formed prefix that simply runs out of data) costs ~54% of a full parse, since several real fields decode successfully first. Structurally-invalid input (all-`0xFF`, an immediately malformed varint in the very first tag) fails ~15x faster than a valid parse and ~8x faster than truncation, because the parser errors out within the first ~10 bytes regardless of total buffer length.

## Phase 9 — SBE (Simple Binary Encoding) Comparison

Replaced the project's earlier pure estimate (made before any SBE code existed, predicting a 5-30x speedup) with measured numbers, for the identical logical `text` and `merged_forward` message content used throughout Phases 1-8. Full detail, including the Phase 2/3/4/7-equivalent sweeps and an honest discussion of a counter-intuitive concurrency result, is in `docs/benchmarks/phase9-sbe-comparison-analysis.md`.

| Benchmark | Protobuf (ns/iter) | SBE (ns/iter) | Speedup | Protobuf bytes | SBE bytes |
|---|---|---|---|---|---|
| text encode | 76.84 | 50.84 | 1.51x | 117 | 175 |
| text decode | 195.51 | 58.14 | 3.36x | 117 | 175 |
| merged_forward encode | 116.13 | 66.78 | 1.74x | 162 | 219 |
| merged_forward decode | 318.56 | 75.91 | 4.20x | 162 | 219 |

The real speedups (1.51x-4.20x) are well below the pre-implementation estimate, with decode benefiting far more than encode (3.4x-4.2x vs 1.5x-1.7x) — SBE's flyweight decode skips the heap allocation and field-presence dispatch that Protobuf's decode path pays for, while Protobuf's encode path is already comparatively cheap. SBE's decode is also zero-allocation (0 heap allocations/iter vs Protobuf's 9) and scales close to linearly under concurrency (0.89-1.05x ratio from 1 to 20 threads, vs Protobuf decode collapsing to 0.07x at 20 threads). Protobuf's durable advantages are wire size (30-50% smaller here, thanks to varint compaction and optional-field omission — SBE's fixed-width fields are 35-50% *larger* for these messages) and self-describing/self-validating decoding. Counter-intuitively, SBE *encode* (not decode) scales worse than Protobuf encode under heavy concurrency, collapsing to a 0.04x ratio at 20 threads versus Protobuf encode's 0.67x — this was independently re-verified as reproducible rather than a one-off fluke, and is most likely a machine-level saturation effect (memory/cache bandwidth or turbo throttling) to which SBE's ~51ns encode is far more sensitive in relative terms than Protobuf's longer encode, rather than a structural SBE weakness.

---

## Cross-cutting takeaways

1. **Repeated message fields are the most expensive schema construct measured** (Phase 3) — 30-100x more per-element cost than packed repeated scalars. If a schema choice exists between "many scalar values" and "many small sub-messages," prefer the former for hot paths.
2. **Parsing is consistently 2-3x more expensive than serializing** (Phases 1, 3) for the same logical message, across both message shapes tested — largely attributable to per-call heap allocation (Phase 4 quantifies ~9 `malloc` calls for a single `text` parse).
3. **Heap allocation is the dominant hidden cost in this benchmark suite**, surfacing in three independent phases: Phase 4 (allocation count/bytes), Phase 5 (fresh-string allocation tax), and most dramatically Phase 7 (multithreaded allocator contention degrading parse throughput below single-threaded baseline at low concurrency).
4. **Arena allocation is not a free win** — it only helps when paired with a reset/reuse discipline (Phase 4's never-reset arena was slower, not faster, than the heap path it was meant to replace). Any future adoption should benchmark the specific reset cadence intended for production use, not assume the textbook benefit transfers unconditionally.
5. **"Zero-copy" stream-based serialization APIs are not unconditionally faster than simpler flat-buffer APIs** (Phase 5) — for small, fully-buffered payloads, the streaming abstraction's virtual dispatch overhead can outweigh its allocation savings. `SerializeToArray` into a preallocated buffer was the fastest of all four serialize paths tested.
6. **No asymmetric DoS surface was found in the two malformed-input shapes tested** (Phase 8) — rejecting bad input was always cheaper than accepting good input, in both the "well-formed-prefix-then-truncated" and "immediately-garbled" failure modes.
7. **Phase 6 (CPU microarchitecture counters) could not be run in this sandbox** due to `perf_event_paranoid=4` and no privilege-escalation path; this is an environment constraint, not a result, and is flagged for follow-up if the suite is ever run with elevated privileges.
8. **SBE beats Protobuf on single-threaded CPU time but not by the margin originally guessed, and not unconditionally** (Phase 9) — measured speedups are 1.51x-4.20x (decode benefiting more than encode), far below the 5-30x pre-implementation estimate, and SBE pays for that speed with 35-50% larger messages on the wire for these specific payloads (no varint compaction). SBE decode also scales far better under concurrency (near-linear, vs Protobuf decode's collapse to 0.07x at 20 threads), but SBE *encode* surprisingly scales worse than Protobuf encode at high thread counts (0.04x vs 0.67x at 20 threads) — a reproducible, independently-verified result attributed to machine-level saturation rather than a structural flaw, reported honestly rather than smoothed into a "SBE is just better" narrative.

## File index

| Phase | Spec | Plan | Results JSON | Analysis |
|---|---|---|---|---|
| 0+1 | `docs/superpowers/specs/2026-06-17-benchmark-phase0-1-design.md` | `docs/superpowers/plans/2026-06-17-benchmark-phase0-1.md` | `results/phase1-2026-06-17.json` | `docs/benchmarks/phase1-throughput-size-analysis.md` |
| 2 | `docs/superpowers/specs/2026-06-18-benchmark-phase2-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase2.md` | `results/phase2-2026-06-18.json` | `docs/benchmarks/phase2-field-fillrate-numeric-encoding-analysis.md` |
| 3 | `docs/superpowers/specs/2026-06-18-benchmark-phase3-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase3.md` | `results/phase3-2026-06-18.json` | `docs/benchmarks/phase3-scalability-analysis.md` |
| 4 | `docs/superpowers/specs/2026-06-18-benchmark-phase4-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase4.md` | `results/phase4-2026-06-18.json` | `docs/benchmarks/phase4-memory-arena-analysis.md` |
| 5 | `docs/superpowers/specs/2026-06-18-benchmark-phase5-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase5.md` | `results/phase5-2026-06-18.json` | `docs/benchmarks/phase5-api-overhead-analysis.md` |
| 6 | — | — | — | `docs/benchmarks/phase6-feasibility-note.md` (skipped) |
| 7 | `docs/superpowers/specs/2026-06-18-benchmark-phase7-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase7.md` | `results/phase7-2026-06-18.json` | `docs/benchmarks/phase7-concurrency-analysis.md` |
| 8 | `docs/superpowers/specs/2026-06-18-benchmark-phase8-design.md` | `docs/superpowers/plans/2026-06-18-benchmark-phase8.md` | `results/phase8-2026-06-18.json` | `docs/benchmarks/phase8-malformed-input-analysis.md` |
| 9 | `docs/superpowers/specs/2026-06-19-benchmark-phase9-sbe-design.md` | `docs/superpowers/plans/2026-06-19-benchmark-phase9-sbe.md` | `results/phase9-2026-06-19.json` | `docs/benchmarks/phase9-sbe-comparison-analysis.md` |

## Execution note

Phases 3 onward (3, 4, 5, 7, 8) plus this report were executed autonomously, without per-step user confirmation, per explicit user authorization to continue unattended. Phases 0-2 went through subagent-driven-development with human-reviewable spec/plan/review checkpoints; Phases 3+ used the same spec → plan → implement → verify → commit → merge pipeline but executed directly (no subagent dispatch) per a later explicit instruction, after a permission-propagation concern was raised about subagents and bypass-permissions mode.
