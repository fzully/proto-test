# Phase 3 — Scalability Sweep Analysis

Date: 2026-06-18
Raw data: `results/phase3-2026-06-18.json`

## Run conditions

`cpu_scaling_enabled: true` per the JSON `context` block (consistent with prior phases on this host); absolute ns figures carry some noise, but the relative/scaling comparisons below are large enough to be robust to it.

## mentioned_user_ids (repeated int64)

| n | Operation | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|
| 1 | serialize | 75.13 | 13,309,810 | 116 |
| 10 | serialize | 80.85 | 12,369,282 | 125 |
| 100 | serialize | 149.70 | 6,680,026 | 215 |
| 1000 | serialize | 955.53 | 1,046,545 | 1989 |
| 1 | parse | 169.38 | 5,903,991 | 116 |
| 10 | parse | 212.21 | 4,712,227 | 125 |
| 100 | parse | 212.00 | 4,716,962 | 215 |
| 1000 | parse | 1214.35 | 823,487 | 1989 |

`mentioned_user_ids` is a `repeated int64` and proto3 packs repeated scalar numeric fields by default: the whole array is one tag + one length-varint + concatenated value-varints, not one tag per element. This exactly explains the byte counts: field cost = 1 (tag) + length-varint-size + Σ(payload bytes per value), where values 1-127 cost 1 payload byte and 128-1000 cost 2. Working it out: n=1 → 1+1+1=3 bytes (envelope is the other 113); n=100 → 1+1+100=102 bytes (all values ≤127, 1 byte each); n=1000 → 1+2+1873=1876 bytes (127 values at 1 byte + 873 values at 2 bytes = 1873, plus the length-varint itself growing to 2 bytes since 1873≥128) — both reconstruct the observed totals (215, 1989) exactly. So size growth is **not perfectly linear in n** — it has a small step where the per-value varint width crosses 128, and a second tiny step where the length-prefix itself crosses 1 vs 2 bytes — but is close to linear in practice (~1.9 bytes/element marginal cost from n=100→1000). Time scales roughly with n too (e.g. serialize 150ns→956ns from n=100→1000, ~0.9 ns/element, close to the ~0.7 ns/element seen from n=1→10), with no sharp non-linear jump — consistent with packed encoding just writing N varints in a tight loop rather than allocating per-element objects.

## merged_forward.items (repeated message)

| n | Operation | Time (ns/iter) | Throughput (ops/sec) | Size (bytes) |
|---|---|---|---|---|
| 1 | serialize | 85.28 | 11,726,092 | 109 |
| 10 | serialize | 313.24 | 3,192,486 | 425 |
| 100 | serialize | 2614.22 | 382,524 | 3648 |
| 1000 | serialize | 26514.75 | 37,715 | 36049 |
| 1 | parse | 192.36 | 5,198,638 | 109 |
| 10 | parse | 1012.23 | 987,916 | 425 |
| 100 | parse | 8427.91 | 118,653 | 3648 |
| 1000 | parse | 105635.65 | 9,467 | 36049 |

Unlike scalar fields, `repeated message` fields are **never packed** — each `ForwardedItem` gets its own tag + length prefix, plus its own nested tags for `message_id`/`sender_id`/`timestamp_ms`/`text.body`. Size grows almost perfectly linearly: marginal cost is ≈35.8 bytes/item from n=10→100 ((3648−425)/90) and ≈36.0 bytes/item from n=100→1000 ((36049−3648)/900) — consistent across the range, as expected since every item has identical structure (only small varint fields whose width doesn't change much in this range, plus the fixed 18-byte UTF-8 body text). Latency per item is far more expensive than for the scalar case: serialize costs ≈25.6-26.4 ns/item (vs ≈0.7-0.9 ns/element for `mentioned_user_ids`, ~30x more) and parse costs ≈82-108 ns/item (vs ≈1.0-1.1 ns/element for the scalar case, ~80-100x more). This gap is expected: each `ForwardedItem` requires constructing/allocating a sub-message object and recursively encoding or parsing 4 separate sub-fields (one of which, `text`, is itself a nested message), versus just writing or reading one packed varint per scalar element.

## Scope note

This covers Phase 3 only (scalability of `mentioned_user_ids` and `merged_forward.items` length, 1/10/100/1000). Arena/memory, API-overhead, CPU counters, concurrency, and malformed-input benchmarks are separate future phases.
