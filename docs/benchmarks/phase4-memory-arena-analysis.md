# Phase 4 — Memory Allocations / Arena Comparison Analysis

Date: 2026-06-18
Raw data: `results/phase4-2026-06-18.json`

## Run conditions

`cpu_scaling_enabled: true` per the JSON `context` block (consistent with prior phases on this host).

## Heap vs Arena: parsing the same text message

| Benchmark | Time (ns/iter) | Allocs/iter | Bytes/iter (malloc'd) |
|---|---|---|---|
| BM_ParseTextHeapAllocs | 191.64 | 9.00 | 301.00 |
| BM_ParseTextArenaAllocs | 220.74 | 3.01 | 486.32 |

`allocs_per_iter` and `bytes_per_iter` count calls that reach the overridden global `operator new`/`delete` (i.e. real `malloc`/`free` calls), not "objects constructed."

**Allocation count.** The Arena path needs ~3x fewer `malloc` calls per parse (3.01 vs 9). The `text` fixture's `ChatMessage` has a top-level message, a nested `QuoteInfo` submessage, and several `string` fields (body, quote text, sender name, etc.) — on the heap path each of those would normally be a separate allocation (the message object itself, plus a heap buffer for any string field whose payload exceeds the small-string-optimization inline buffer). The Arena path collapses the per-object allocations (the `ChatMessage` and `QuoteInfo` objects themselves are bump-allocated out of the arena's existing memory block, no separate `malloc` per message/submessage), leaving only the residual `malloc` calls — chiefly the occasional fetch of a new arena memory block once the current block is exhausted, plus any string payloads that protobuf still allocates outside the arena's bump region.

**Allocation count is not near zero, and time is *higher*, not lower.** This is the most informative part of the result. The benchmark deliberately reuses one `arena` object across the entire timed loop without ever calling `Reset()` — modeling a long-lived arena that keeps accumulating live objects rather than periodically discarding them. Because nothing is ever freed, the arena's backing memory grows monotonically across millions of iterations; it must keep fetching new, larger blocks from the system allocator to keep up, and each new block touches fresh (cold) memory pages. That ongoing growth is what keeps `allocs_per_iter` above zero (≈3, not ≈0) and is also the most likely explanation for the ~15% higher latency (220.7ns vs 191.6ns) versus the heap path: the heap path's allocator can reuse the same handful of freed blocks every iteration (hot, cached memory), while the never-reset arena keeps growing into new territory and the per-call `Arena::Create<T>` bookkeeping (checking remaining space in the current block, falling back to a new block) adds overhead that a plain stack-allocated `ChatMessage parsed;` does not have.

**Takeaway.** Arena allocation does cut the *number* of system allocator calls substantially when many sub-objects would otherwise each need their own `new`, confirming the basic mechanism works. But the win is conditional on periodic `Reset()` (or an arena scoped to a bounded batch of work) to reclaim and reuse memory; an arena that is only ever grown, never reset, can end up both higher in cumulative memory use and *slower* than plain heap allocation, because it forfeits memory reuse without removing per-call bookkeeping. This is a real, useful negative result, not a contradiction of protobuf's documented Arena guidance — it shows the documented benefit assumes a reset/reuse cadence that this specific benchmark intentionally omitted in order to isolate raw allocation-count behavior.

## Scope note

This covers Phase 4 only (parse-side heap-vs-Arena allocation comparison for the `text` message type, with the arena never reset across the run). API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
