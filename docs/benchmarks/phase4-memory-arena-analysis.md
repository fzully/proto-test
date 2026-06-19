# Phase 4 — Memory Allocations / Arena Comparison Analysis

Date: 2026-06-18 (extended 2026-06-19 with Reset()-per-iteration and Reset()-every-10-iterations scenarios)
Raw data: `results/phase4-2026-06-18.json`, `results/phase4-arena-reset-2026-06-19.json`, `results/phase4-arena-reset10-2026-06-19.json`

## Run conditions

`cpu_scaling_enabled: true` per the JSON `context` block (consistent with prior phases on this host).

## Heap vs Arena: parsing the same text message

| Benchmark | Time (ns/iter) | Allocs/iter | Bytes/iter (malloc'd) |
|---|---|---|---|
| BM_ParseTextHeapAllocs | 191.64 | 9.00 | 301.00 |
| BM_ParseTextArenaAllocs | 220.74 | 3.01 | 486.32 |
| BM_ParseTextArenaResetAllocs | 188.18 | 6.00 | 1101.00 |
| BM_ParseTextArenaReset10Allocs | 192.61 | 3.60 | 910.60 |

`allocs_per_iter` and `bytes_per_iter` count calls that reach the overridden global `operator new`/`delete` (i.e. real `malloc`/`free` calls), not "objects constructed."

**Allocation count.** The Arena path needs ~3x fewer `malloc` calls per parse (3.01 vs 9). The `text` fixture's `ChatMessage` has a top-level message, a nested `QuoteInfo` submessage, and several `string` fields (body, quote text, sender name, etc.) — on the heap path each of those would normally be a separate allocation (the message object itself, plus a heap buffer for any string field whose payload exceeds the small-string-optimization inline buffer). The Arena path collapses the per-object allocations (the `ChatMessage` and `QuoteInfo` objects themselves are bump-allocated out of the arena's existing memory block, no separate `malloc` per message/submessage), leaving only the residual `malloc` calls — chiefly the occasional fetch of a new arena memory block once the current block is exhausted, plus any string payloads that protobuf still allocates outside the arena's bump region.

**Allocation count is not near zero, and time is *higher*, not lower.** This is the most informative part of the result. The benchmark deliberately reuses one `arena` object across the entire timed loop without ever calling `Reset()` — modeling a long-lived arena that keeps accumulating live objects rather than periodically discarding them. Because nothing is ever freed, the arena's backing memory grows monotonically across millions of iterations; it must keep fetching new, larger blocks from the system allocator to keep up, and each new block touches fresh (cold) memory pages. That ongoing growth is what keeps `allocs_per_iter` above zero (≈3, not ≈0) and is also the most likely explanation for the ~15% higher latency (220.7ns vs 191.6ns) versus the heap path: the heap path's allocator can reuse the same handful of freed blocks every iteration (hot, cached memory), while the never-reset arena keeps growing into new territory and the per-call `Arena::Create<T>` bookkeeping (checking remaining space in the current block, falling back to a new block) adds overhead that a plain stack-allocated `ChatMessage parsed;` does not have.

**Takeaway (never-reset arena).** Arena allocation does cut the *number* of system allocator calls substantially when many sub-objects would otherwise each need their own `new`, confirming the basic mechanism works. But the win is conditional on periodic `Reset()` (or an arena scoped to a bounded batch of work) to reclaim and reuse memory; an arena that is only ever grown, never reset, can end up both higher in cumulative memory use and *slower* than plain heap allocation, because it forfeits memory reuse without removing per-call bookkeeping. This is a real, useful negative result, not a contradiction of protobuf's documented Arena guidance — it shows the documented benefit assumes a reset/reuse cadence that this specific benchmark intentionally omitted in order to isolate raw allocation-count behavior.

## Does adding `Reset()` actually deliver the documented win?

`BM_ParseTextArenaResetAllocs` answers the natural follow-up: take the exact same long-lived `arena` and call `arena.Reset()` after every single parse, so the arena's footprint is bounded instead of growing forever — the textbook "Arena per request" pattern. The result is the opposite of what the never-reset run would predict: `allocs_per_iter` goes *up* (6.00, vs 3.01 for never-reset and 9.00 for plain heap) and `bytes_per_iter` goes up even more sharply (1101.00, the highest of all three variants — more than 2x the never-reset Arena path and more than 3.5x the heap path), while latency lands in between (188.2ns, between the heap path's 191.6ns and the never-reset Arena path's 220.7ns).

This is consistent, not noisy — re-running with `--benchmark_repetitions=5` reproduces `allocs_per_iter=6` and `bytes_per_iter≈1101` exactly every time (relative stddev ~0.1-0.2%, vs ~20% on the never-reset Arena variant's timing). The cause is a documented internal detail of `google::protobuf::Arena`: every `Reset()` assigns the arena a new internal "lifecycle id" (`ThreadSafeArena::tag_and_id_`, see `thread_safe_arena.h`: "Unique for each arena. Changes on `Reset()`"). Each thread keeps a small fast-path cache (`ThreadCache::last_lifecycle_id_seen`) that lets repeated `Arena::Create<T>` calls on the same arena skip straight to bump-allocating from the cached block. Once `Reset()` changes the lifecycle id, that cache no longer matches, so the *very next* `Arena::Create<ChatMessage>` after each `Reset()` falls onto the slow path (`GetSerialArenaFallback`) instead of the fast path — and since this benchmark resets after every single iteration, *every* iteration pays that slow-path cost, which itself does real allocator work (re-registering the serial arena). The never-reset variant pays this cost once (when the cache first warms up) and then takes the fast path for the rest of the run, which is why it ends up cheaper than reset-every-iteration despite growing unboundedly.

**Takeaway (Reset every iteration).** `Reset()` is not a free way to bound an arena's memory — calling it more often than necessary defeats the same per-thread fast-path cache that makes Arena reuse cheap in the first place, and on this fixture resetting after *every* tiny parse is strictly worse, by allocation count and bytes, than never resetting at all. This matches protobuf's own guidance that Arenas are meant to be reset at a coarse granularity (e.g. once per request/batch covering many messages), not once per message.

## Does batching the reset (every 10 parses instead of every 1) close the gap?

`BM_ParseTextArenaReset10Allocs` is identical to `BM_ParseTextArenaResetAllocs` except `arena.Reset()` is only called once every 10 iterations instead of after every single one — the same `arena` and `bytes` buffer, the same warm-up-then-reset before the timed loop, just a coarser reset cadence.

Batching helps, but not enough to beat the never-reset baseline on this fixture:

| Benchmark | Allocs/iter | Bytes/iter |
|---|---|---|
| BM_ParseTextArenaAllocs (never reset) | 3.01 | 486.32 |
| BM_ParseTextArenaReset10Allocs (reset every 10) | 3.60 | 910.60 |
| BM_ParseTextArenaResetAllocs (reset every 1) | 6.00 | 1101.00 |

Going from "reset every 1" to "reset every 10" cuts `allocs_per_iter` by 40% (6.00 → 3.60) and `bytes_per_iter` by 17% (1101.00 → 910.60) — batching the reset clearly reduces the per-message tax, as expected. But it lands strictly between the two extremes, not below the never-reset baseline: `allocs_per_iter` is still ~20% above never-reset (3.60 vs 3.01) and `bytes_per_iter` is still ~87% above it (910.60 vs 486.32). Working the totals backward (allocs × batch size, to see the cost per reset *event* rather than per message): a batch of 1 spends 6.00 allocs per reset event; a batch of 10 spends 36.00 allocs per reset event (3.60 × 10) — i.e. the absolute tax per `Reset()` call did not stay flat as the batch grew, it roughly doubled. That rules out the simplest model (a fixed one-time "re-warm the lifecycle cache" cost that gets amortized over more messages); something about Reset()'s own bookkeeping cost (e.g. `CleanupList()`/`Free()` work, see `arena.cc`) likely scales with how much was allocated since the last reset, not just with the act of resetting itself. Pinning down the exact mechanism would need a few more batch sizes (e.g. 2, 5, 20, 100) to see whether allocs-per-event grows linearly, sub-linearly, or has a floor — not done here since it's beyond what was asked.

**Takeaway (Reset every 10).** Batching the reset cadence is directionally correct — it's better than resetting on every message — but 10 messages per `Reset()` is still too fine-grained to make the documented Arena benefit show up on this fixture; the never-reset variant (which forgoes memory bounding entirely) remains the cheapest of the three Arena variants by allocation count. Latency-wise, reset-every-10 (192.6ns) lands within noise of the plain heap path (191.6ns) — neither clearly better nor worse. The honest overall conclusion for this specific message shape and access pattern (one small message in/out at a time): **none of the three Arena variants tested deliver a clear win over plain heap allocation**, and finding a cadence that does would likely need either a much larger batch size, a message shape with more sub-objects to amortize the per-create overhead over (e.g. `merged_forward` with many repeated items, see Phase 3), or both.

## Scope note

This covers Phase 4 only (parse-side heap-vs-Arena allocation comparison for the `text` message type): a never-reset long-lived arena, a long-lived arena reset after every single parse, and a long-lived arena reset every 10 parses. API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
