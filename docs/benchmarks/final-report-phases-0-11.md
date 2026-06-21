# Protobuf IM-Chat Benchmark Suite — Final Report (Phases 0-11)

Date: 2026-06-18 (Phase 9 added 2026-06-19; Phase 10 added 2026-06-20; Phase 11 added 2026-06-21)
Repository: `proto-test` (`im.chat.v1` protobuf schema, C++20, CMake + FetchContent, Google Benchmark v1.9.1, protobuf v35.1)

This report consolidates every benchmark phase run in this project: infrastructure, throughput/latency/size, field-fill-rate, numeric encoding, scalability, memory/Arena allocation, serialization API overhead, concurrency scaling, malformed-input parse cost, a Protobuf-vs-SBE comparison, a JSON-library shootout, and a yyjson-vs-Protobuf PK. Phase 6 (CPU microarchitecture counters via `perf`) was found infeasible in this sandbox and is documented as skipped rather than faked.

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

Compared default heap-allocated parsing vs a long-lived, **never-reset** `google::protobuf::Arena` reused across the whole benchmark run, plus two reset-cadence variants — `Reset()` after every single parse, and `Reset()` every 10 parses — to test whether adding the textbook "reset cadence" actually delivers the documented win, and whether batching the reset closes any gap.

| Benchmark | Time (ns/iter) | Allocs/iter | Bytes/iter (malloc'd) |
|---|---|---|---|
| BM_ParseTextHeapAllocs | 191.64 | 9.00 | 301.00 |
| BM_ParseTextArenaAllocs (never reset) | 220.74 | 3.01 | 486.32 |
| BM_ParseTextArenaReset10Allocs (reset every 10 parses) | 192.61 | 3.60 | 910.60 |
| BM_ParseTextArenaResetAllocs (reset every parse) | 188.18 | 6.00 | 1101.00 |

Arena cuts `malloc` calls ~3x (9→3.01) by bump-allocating the top-level message and its `QuoteInfo` submessage out of existing arena memory instead of `new`-ing each one. But because the never-reset benchmark deliberately never calls `Reset()`, the arena's memory only ever grows — it has to keep fetching new, larger, cold-memory blocks, and `Arena::Create<T>`'s per-call bookkeeping has its own cost. Net effect: Arena is *slower* here (220.7ns vs 191.6ns, +15%), not faster. **This is a deliberate, informative negative result**, not a bug: it shows Arena's documented benefit assumes a reset/reuse cadence (e.g. one arena per request, reset between requests) — an arena that only grows forfeits memory reuse without removing per-call overhead.

Adding `Reset()` does not simply fix this — resetting after *every* parse makes allocation counts *worse* (6.00 allocs/iter and 1101 bytes/iter, both higher than the never-reset variant), even though it brings latency down to roughly heap-path levels (188.2ns). The reason is internal to protobuf's `Arena`: every `Reset()` assigns the arena a new "lifecycle id," which invalidates the per-thread fast-path cache that lets repeated `Arena::Create<T>` calls skip straight to bump allocation. Resetting after every parse means every parse pays the slow re-registration path that the never-reset variant only pays once.

Batching the reset to every 10 parses helps but still doesn't close the gap: `allocs_per_iter` drops 40% versus reset-every-parse (6.00 → 3.60) and `bytes_per_iter` drops 17% (1101 → 910.6), but both remain above the never-reset baseline (3.01 / 486.32). The cost-per-`Reset()`-event (allocs × batch size) roughly doubles from batch-1 to batch-10 (6.00 → 36.00 total), so the reset tax isn't a flat one-time cost that simply gets amortized over more messages — something in `Reset()`'s own bookkeeping (`CleanupList()`/`Free()`) scales with how much was allocated since the last reset. **Takeaway:** none of the three Arena variants tested beat plain heap allocation outright on this message shape and access pattern (one small `text` message in/out at a time); `Reset()` only pays for itself at some coarser-than-10 batch size, a different (larger, more sub-object-heavy) message shape, or both. See `docs/benchmarks/phase4-memory-arena-analysis.md` for the full mechanism writeup.

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

| Threads | Serialize aggregate (ops/sec) | Serialize scaling ratio | Parse (heap) aggregate (ops/sec) | Parse (heap) scaling ratio |
|---|---|---|---|---|
| 1 | 13,172,661 | 1.00 | 5,102,884 | 1.00 |
| 2 | 26,481,638 | 2.01 | 2,905,831 | **0.57** |
| 4 | 52,230,107 | 3.97 | 3,726,085 | 0.73 |
| 8 | 92,535,545 | 7.02 | 3,898,634 | 0.76 |
| 16 | 161,741,472 | 12.28 | 4,626,571 | 0.91 |
| 20 | 177,219,044 | 13.45 | 5,645,058 | 1.11 |

Serialize (allocation-light, reused buffer) scales reasonably (67% efficiency at 20 threads — likely turbo-boost frequency throttling under full core load, not contention). Parse (which needs ~9 heap allocations per call — see Phase 4) scales *badly*: at 2 threads, aggregate throughput **drops below the single-thread baseline** (ratio 0.57) and only crosses back above 1.0x at 20 threads. **This is the most actionable cross-phase finding in the whole suite**: even with zero shared state in application code, an allocation-heavy workload turns the global heap allocator into a hidden shared resource, and that contention can make adding threads actively counterproductive at low concurrency. It directly strengthens the case for Phase 4's Arena exploration — a per-thread or per-connection Arena would remove parse's dependency on the global allocator and should scale far closer to serialize's curve.

### Does a per-thread Arena actually fix this?

`BM_ConcurrentParseTextArena` tests the prediction directly: each benchmark thread gets its own long-lived, never-reset `google::protobuf::Arena` (mirroring `BM_ParseTextArenaAllocs` from Phase 4, the never-reset variant — chosen because it was the cheapest of the three reset cadences tested there once Reset()'s lifecycle-id tax is factored out of the picture), reused for the entire timed loop, with zero sharing across threads.

| Threads | Parse (heap) aggregate (ops/sec) | Parse (heap) ratio (vs its own 1-thread) | Parse (Arena) aggregate (ops/sec) | Parse (Arena) ratio (vs its own 1-thread) | Arena vs heap, same thread count |
|---|---|---|---|---|---|
| 1 | 5,029,919 | 1.00 | 4,498,290 | 1.00 | 0.89x (Arena slower) |
| 2 | 3,471,238 | 0.69 | 7,757,650 | 1.73 | 2.24x |
| 4 | 3,398,124 | 0.68 | 9,969,083 | 2.22 | 2.93x |
| 8 | 4,033,400 | 0.80 | 10,304,663 | 2.29 | 2.55x |
| 16 | 4,558,275 | 0.91 | 10,937,667 | 2.43 | 2.40x |
| 20 | 4,863,837 | 0.97 | 11,580,251 | 2.58 | 2.38x |

(This run's absolute heap numbers differ slightly from the table above — same qualitative pattern, re-measured on the same host at a different time, included here for an apples-to-apples Arena comparison.)

The improvement is exactly what the Phase 4/7 cross-reference predicted, and it's large. At 1 thread, the per-thread Arena is ~11% *slower* than heap — the same never-reset bookkeeping tax Phase 4 already found (no concurrency benefit to offset it yet). But from 2 threads on, Arena wins by 2.2x-2.9x at the *same* thread count, and that gap doesn't shrink as thread count rises — if anything it's largest at 4 threads (2.93x) and settles around 2.4x at high thread counts. More strikingly: **heap parsing never recovers to its own 1-thread throughput even at 20 threads** (ratio 0.97, still below 1.0), while Arena's *worst* multi-threaded result (2 threads: 7.76M ops/sec) already beats heap's *best* result at any thread count tested (20 threads: 4.86M ops/sec) by 1.6x. The mechanism is the same one Phase 4 identified for the never-reset arena, just inverted into a benefit here: each thread's Arena almost never calls into the global allocator after warm-up (no cross-thread malloc/free contention), so the parse path stops depending on a resource that's secretly shared across all 20 threads (glibc's per-thread arena pool, its `mmap`/`brk` bookkeeping). **This is the strongest, most unambiguous win for Arena anywhere in this benchmark suite** — it confirms that Arena's real-world case is concurrency/allocator-contention relief, not single-threaded latency, which is exactly where Phase 4's single-threaded tests came up empty.

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

## Phase 10 — JSON Library Shootout

4 JSON libraries (nlohmann/json, RapidJSON, yyjson, cJSON) hand-encode/decode the same `text` and `merged_forward` logical message content into JSON (camelCase keys, int64/enum as JSON strings, compact output), ranked by total encode+decode `real_time` summed across both shapes. simdjson was excluded — it has no symmetric high-performance writer, so it cannot be ranked on the same metric as the other 4. Full detail, including the per-shape encode/decode breakdown, is in `docs/benchmarks/phase10-json-shootout-analysis.md`.

| Rank | Library | Total Encode+Decode (ns, both shapes) |
|---|---|---|
| 1 | yyjson | 1203.82 |
| 2 | RapidJSON | 2717.31 |
| 3 | cJSON | 4575.36 |
| 4 | nlohmann/json | 13008.57 |

`yyjson` wins decisively: 2.26x faster than 2nd-place RapidJSON and 10.8x faster than last-place nlohmann/json, matching yyjson's own marketing claim of being the fastest C/C++ JSON library by a margin large enough that it isn't noise. The more counter-intuitive result is cJSON: despite being the oldest and simplest of the four libraries (plain C, no SIMD, no arena allocator), it beats nlohmann/json by 2.84x on total time (4575.36 ns vs 13008.57 ns) — nlohmann/json's modern, ergonomic API comes from a `std::map`-backed DOM with heavy type-erasure and exception-driven parsing, and that overhead makes it slower than all three other libraries on every single one of the 8 encode/decode measurements, not just on average. `yyjson` is carried forward as the JSON library used for the Phase 11 vs-Protobuf comparison.

## Phase 11 — yyjson vs Protobuf PK

Head-to-head between Protobuf and `yyjson` (Phase 10's winner) on the same `text` and `merged_forward` logical message content, reusing Phase 1's 4 Protobuf benchmarks and Phase 10's 4 yyjson benchmarks unchanged — no new code was written for this phase. Full detail is in `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`.

| Shape | Format | Encode (ns) | Decode (ns) | Bytes |
|---|---|---|---|---|
| TextMessage | Protobuf | 73.67 | 191.10 | 117 |
| TextMessage | yyjson | 159.98 | 454.22 | 408 |
| MergedForwardMessage | Protobuf | 104.97 | 316.25 | 162 |
| MergedForwardMessage | yyjson | 203.16 | 474.80 | 500 |

Protobuf wins every single metric on both shapes — there is no yyjson win to report here. On TextMessage, Protobuf is 2.17x faster encoding, 2.38x faster decoding, and 3.49x smaller on the wire. On MergedForwardMessage, Protobuf is 1.94x faster encoding, 1.50x faster decoding, and 3.09x smaller on the wire. The margins are not a single uniform multiplier: they range 1.50x-3.49x depending on metric and shape, and notably the decode advantage *narrows* on the MergedForwardMessage shape (1.50x) compared to TextMessage (2.38x) — i.e. yyjson's relative decode penalty shrinks as repeated-field load increases, consistent with this project's earlier finding that repeated fields are the most expensive schema construct (Phase 3) and impose extra decode cost on both formats, just proportionally less on yyjson's side here.

---

## Cross-cutting takeaways

1. **Repeated message fields are the most expensive schema construct measured** (Phase 3) — 30-100x more per-element cost than packed repeated scalars. If a schema choice exists between "many scalar values" and "many small sub-messages," prefer the former for hot paths.
2. **Parsing is consistently 2-3x more expensive than serializing** (Phases 1, 3) for the same logical message, across both message shapes tested — largely attributable to per-call heap allocation (Phase 4 quantifies ~9 `malloc` calls for a single `text` parse).
3. **Heap allocation is the dominant hidden cost in this benchmark suite**, surfacing in three independent phases: Phase 4 (allocation count/bytes), Phase 5 (fresh-string allocation tax), and most dramatically Phase 7 (multithreaded allocator contention degrading parse throughput below single-threaded baseline at low concurrency).
4. **Arena allocation is not a single-threaded win, but it is a decisive concurrency win.** All single-threaded Arena variants tested in Phase 4 (never-reset, reset-every-parse, reset-every-10) ended up slower or no better than plain heap allocation for one small `text` message at a time — "just add `Reset()`" is not a free fix either, since resetting too finely defeats Arena's per-thread fast-path cache. But Phase 7's per-thread-Arena retest flips the picture entirely: from 2 threads up, a per-thread never-reset Arena beats heap-allocated parsing by 2.2x-2.9x in aggregate throughput at the same thread count, and heap parsing never even recovers to its own 1-thread baseline through 20 threads, while Arena's worst multithreaded result already beats heap's best. **The lesson: Arena's real payoff in this suite is relieving global-allocator contention under concurrency, not improving single-threaded latency** — benchmark the concurrency profile you actually care about, not just single-call latency, before deciding Arena isn't worth it.
5. **"Zero-copy" stream-based serialization APIs are not unconditionally faster than simpler flat-buffer APIs** (Phase 5) — for small, fully-buffered payloads, the streaming abstraction's virtual dispatch overhead can outweigh its allocation savings. `SerializeToArray` into a preallocated buffer was the fastest of all four serialize paths tested.
6. **No asymmetric DoS surface was found in the two malformed-input shapes tested** (Phase 8) — rejecting bad input was always cheaper than accepting good input, in both the "well-formed-prefix-then-truncated" and "immediately-garbled" failure modes.
7. **Phase 6 (CPU microarchitecture counters) could not be run in this sandbox** due to `perf_event_paranoid=4` and no privilege-escalation path; this is an environment constraint, not a result, and is flagged for follow-up if the suite is ever run with elevated privileges.
8. **SBE beats Protobuf on single-threaded CPU time but not by the margin originally guessed, and not unconditionally** (Phase 9) — measured speedups are 1.51x-4.20x (decode benefiting more than encode), far below the 5-30x pre-implementation estimate, and SBE pays for that speed with 35-50% larger messages on the wire for these specific payloads (no varint compaction). SBE decode also scales far better under concurrency (near-linear, vs Protobuf decode's collapse to 0.07x at 20 threads), but SBE *encode* surprisingly scales worse than Protobuf encode at high thread counts (0.04x vs 0.67x at 20 threads) — a reproducible, independently-verified result attributed to machine-level saturation rather than a structural flaw, reported honestly rather than smoothed into a "SBE is just better" narrative.
9. **Among 4 JSON libraries benchmarked head-to-head, `yyjson` wins decisively** (Phase 10) — 1203.82 ns total encode+decode time across both shapes, 2.26x faster than runner-up RapidJSON (2717.31 ns) and 10.8x faster than last-place nlohmann/json (13008.57 ns). `yyjson` is the library carried forward into Phase 11's Protobuf comparison.
10. **Protobuf beats even the fastest JSON library (`yyjson`) on every metric, but not by a single uniform margin** (Phase 11) — on TextMessage, Protobuf is 2.17x faster encoding, 2.38x faster decoding, and 3.49x smaller on the wire; on MergedForwardMessage, 1.94x faster encoding, 1.50x faster decoding, and 3.09x smaller. The margins range 1.50x-3.49x by metric/shape rather than collapsing to one number, and the decode advantage notably narrows on the repeated-field-heavy MergedForwardMessage shape (1.50x) versus TextMessage (2.38x) — consistent with this suite's repeated finding that repeated fields are the most expensive schema construct, here showing up as a proportionally smaller relative penalty for yyjson than for Protobuf as repeated-field load increases.

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
| 10 | `docs/superpowers/specs/2026-06-20-benchmark-phase10-json-shootout-design.md` | `docs/superpowers/plans/2026-06-20-benchmark-phase10-json-shootout.md` | `results/phase10-2026-06-20.json` | `docs/benchmarks/phase10-json-shootout-analysis.md` |
| 11 | `docs/superpowers/specs/2026-06-21-benchmark-phase11-yyjson-protobuf-pk-design.md` | `docs/superpowers/plans/2026-06-21-benchmark-phase11-yyjson-protobuf-pk.md` | `results/phase11-2026-06-21.json` | `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md` |

## Execution note

Phases 3 onward (3, 4, 5, 7, 8) plus this report were executed autonomously, without per-step user confirmation, per explicit user authorization to continue unattended. Phases 0-2 went through subagent-driven-development with human-reviewable spec/plan/review checkpoints; Phases 3+ used the same spec → plan → implement → verify → commit → merge pipeline but executed directly (no subagent dispatch) per a later explicit instruction, after a permission-propagation concern was raised about subagents and bypass-permissions mode.
