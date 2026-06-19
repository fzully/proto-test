# Phase 7 — Concurrent Throughput Scaling Analysis

Date: 2026-06-18 (extended 2026-06-19 with a per-thread Arena retest)
Raw data: `results/phase7-2026-06-18.json`, `results/phase7-arena-2026-06-19.json`
Host: 20 logical CPUs (`nproc` = 20), `cpu_scaling_enabled: true` per the JSON `context` block.

Each thread builds its own local `ChatMessage`/`std::string` — no shared mutable state, no locks in the benchmarked code itself. "Aggregate throughput" = `threads * 1e9 / real_time_ns` (each iteration is one wall-clock round across all threads). "Scaling ratio" = aggregate throughput at N threads ÷ aggregate throughput at 1 thread (ideal = N).

## Serialize (`BM_ConcurrentSerializeText`, reused `std::string` + `.clear()`)

| Threads | real_time (ns/iter) | Aggregate throughput (ops/sec) | Scaling ratio | Ideal |
|---|---|---|---|---|
| 1 | 75.91 | 13,172,661 | 1.00 | 1 |
| 2 | 75.52 | 26,481,638 | 2.01 | 2 |
| 4 | 76.58 | 52,230,107 | 3.97 | 4 |
| 8 | 86.45 | 92,535,545 | 7.02 | 8 |
| 16 | 98.92 | 161,741,472 | 12.28 | 16 |
| 20 | 112.85 | 177,219,044 | 13.45 | 20 |

## Parse (`BM_ConcurrentParseText`, fresh heap-allocated `ChatMessage` per call)

| Threads | real_time (ns/iter) | Aggregate throughput (ops/sec) | Scaling ratio | Ideal |
|---|---|---|---|---|
| 1 | 195.97 | 5,102,884 | 1.00 | 1 |
| 2 | 688.27 | 2,905,831 | 0.57 | 2 |
| 4 | 1073.51 | 3,726,085 | 0.73 | 4 |
| 8 | 2052.00 | 3,898,634 | 0.76 | 8 |
| 16 | 3458.28 | 4,626,571 | 0.91 | 16 |
| 20 | 3542.92 | 5,645,058 | 1.11 | 20 |

## Analysis

**Serialize scales sublinearly but reasonably:** up to 4 threads it's essentially linear (3.97x at 4 threads), then increasingly falls behind ideal — 7.02x at 8, 12.28x at 16, only 13.45x (67% efficiency) at 20 threads despite 20 independent, lock-free workers. The per-iteration `real_time` itself creeping up (75.9ns → 112.9ns as thread count rises) under a workload with no shared data points to a host-level effect rather than contention in this code: with `cpu_scaling_enabled: true`, per-core clock frequency typically drops as more cores go active simultaneously (turbo-boost budget is shared across cores), so each thread does less work per cycle even though there is no logical contention.

**Parse shows a much more dramatic, qualitatively different effect: it gets *worse* than single-threaded at 2 threads, and never fully recovers.** Going from 1 to 2 threads, per-iteration time more than triples (196ns → 688ns) and aggregate throughput actually *drops* below the 1-thread baseline (ratio 0.57) — adding a second worker makes the system parse fewer messages per second in total than one worker alone. It only crosses back above 1.0x at 20 threads. This lines up directly with [[phase4-memory-arena-analysis]]'s finding that the default heap-allocated parse path needs ~9 `malloc`/`free` calls per message: with `text` fixtures parsed concurrently from independent threads, those allocator calls aren't logically related, but glibc's `malloc` still has to coordinate across its limited number of per-thread arenas (and the underlying `mmap`/`brk` bookkeeping is process-global) — at low thread counts the arena/lock contention overhead from the allocator dominates, masking the otherwise-parallel CPU work; only once thread count is high enough does the parallel work outweigh that fixed contention cost. The serialize path, by contrast, almost never calls into the allocator at all (the `std::string` buffer is reused via `.clear()`), so it never pays this tax — directly explaining why it scales so much better than parse.

**Takeaway:** "embarrassingly parallel, no shared state in your own code" is not sufficient for good multithreaded scaling if the workload is allocation-heavy — the default heap allocator becomes the de facto shared resource. This is the strongest practical argument in this whole benchmark suite for the Arena-based parsing path explored in [[phase4-memory-arena-analysis]]: an Arena, especially one scoped per-thread or per-connection, removes the parse path's dependence on the global allocator and would be expected to scale far closer to the serialize path's curve.

## Retest: per-thread Arena (`BM_ConcurrentParseTextArena`)

Phase 4 tested Arena single-threaded and found no variant (never-reset, reset-every-parse, reset-every-10) beat plain heap allocation — but flagged multithreaded allocator contention as the scenario where Arena's removal of the global-allocator dependency should actually pay off. `BM_ConcurrentParseTextArena` tests that directly: each benchmark thread gets its own private, long-lived, never-reset `Arena` (the cheapest of Phase 4's three variants once `Reset()`'s own tax is excluded), reused for the whole timed loop, never shared across threads — same `Threads(1/2/4/8/16/20)` sweep as `BM_ConcurrentParseText`.

| Threads | Parse (heap) aggregate (ops/sec) | Parse (heap) ratio vs its own 1-thread | Parse (Arena) aggregate (ops/sec) | Parse (Arena) ratio vs its own 1-thread | Arena vs heap, same thread count |
|---|---|---|---|---|---|
| 1 | 5,029,919 | 1.00 | 4,498,290 | 1.00 | 0.89x (Arena slower) |
| 2 | 3,471,238 | 0.69 | 7,757,650 | 1.73 | 2.24x |
| 4 | 3,398,124 | 0.68 | 9,969,083 | 2.22 | 2.93x |
| 8 | 4,033,400 | 0.80 | 10,304,663 | 2.29 | 2.55x |
| 16 | 4,558,275 | 0.91 | 10,937,667 | 2.43 | 2.40x |
| 20 | 4,863,837 | 0.97 | 11,580,251 | 2.58 | 2.38x |

(Heap numbers here come from a same-session rerun of `BM_ConcurrentParseText` alongside the new Arena benchmark, for an apples-to-apples comparison; absolute values differ slightly from the table above due to normal run-to-run host noise, but the qualitative pattern — heap collapsing at low thread counts, slowly recovering — is the same.)

**Single-threaded, Arena loses, as expected from Phase 4:** at 1 thread, the never-reset Arena is ~11% slower than heap (0.89x) — there's no concurrency yet to offset its per-call bookkeeping tax.

**From 2 threads on, Arena wins decisively, and the win doesn't erode with scale:** Arena's aggregate throughput is 2.2x-2.9x higher than heap's at the *same* thread count across the entire 2-20 thread range, peaking at 4 threads (2.93x) rather than fading out. Looked at relative to each path's own single-thread baseline, the contrast is stark: heap parsing's scaling ratio stays *below 1.0 all the way through 20 threads* (0.97 at best) — meaning 20 threads of heap-allocated parsing still can't out-throughput 1 thread of heap-allocated parsing on this host. Arena, by contrast, scales past its own baseline immediately at 2 threads (1.73x) and keeps climbing to 2.58x at 20 threads. The practical headline: **Arena's worst multithreaded result (2 threads: 7.76M ops/sec) beats heap's best result at any thread count (20 threads: 4.86M ops/sec) by 1.6x.**

This is the same mechanism Phase 4 identified as a negative for never-reset Arena in isolation — it almost never calls into the system allocator after warm-up — but here that property is exactly what removes the parse path's dependence on the secretly-shared global allocator (glibc's per-thread arena pool, process-global `mmap`/`brk` bookkeeping) that was strangling heap-allocated parsing's scaling.

**Takeaway:** this is the one place in the whole Arena investigation (Phase 4 + this retest) where Arena delivers an unambiguous, large win — and it's exactly the scenario this phase's own earlier analysis predicted it would be: concurrent, allocation-heavy parsing where the global allocator is the bottleneck, not single-call latency. Anyone deciding whether Arena is "worth it" based on a single-threaded microbenchmark alone would reach the wrong conclusion for a concurrent service.

## Scope note

This covers Phase 7 only (concurrent serialize/parse scaling for the `text` message type, 1-20 threads, no shared state, plus a per-thread-Arena retest of the parse path). Malformed-input parse-failure cost is a separate phase (Phase 8).
