# Phase 7 — Concurrent Throughput Scaling Analysis

Date: 2026-06-18
Raw data: `results/phase7-2026-06-18.json`
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

**Takeaway:** "embarrassingly parallel, no shared state in your own code" is not sufficient for good multithreaded scaling if the workload is allocation-heavy — the default heap allocator becomes the de facto shared resource. This is the strongest practical argument in this whole benchmark suite for the Arena-based parsing path explored in Phase 4: an Arena, especially one scoped per-thread or per-connection, removes the parse path's dependence on the global allocator and would be expected to scale far closer to the serialize path's curve.

## Scope note

This covers Phase 7 only (concurrent serialize/parse scaling for the `text` message type, 1-20 threads, no shared state). Malformed-input parse-failure cost is a separate phase (Phase 8).
