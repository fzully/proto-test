# Phase 8 — Malformed-Input Parse Failure Cost Analysis

Date: 2026-06-18
Raw data: `results/phase8-2026-06-18.json`

## Run conditions

`cpu_scaling_enabled: true` per the JSON `context` block (consistent with prior phases on this host).

## Valid parse vs two malformed-input failure modes

| Input | Time (ns/iter) | Input bytes | parse_ok |
|---|---|---|---|
| BM_ParseText (valid `text` message, baseline) | 190.26 | 117 | 1 (implicit — `ParseFromString` succeeds) |
| BM_ParseTruncatedText (first 58 of 117 bytes) | 101.85 | 58 | 0 |
| BM_ParseGarbageText (117 bytes, all `0xFF`) | 12.44 | 117 | 0 |

Both malformed inputs reliably fail to parse (`parse_ok=0` confirmed), so the timings below are genuinely measuring failure-path cost, not an accidentally-valid input.

**Both failure modes are cheaper than a successful parse, but by very different margins.** The truncated input (101.85ns) costs about 54% of the valid-parse baseline (190.26ns) — it has to actually decode several real fields correctly (the first half of a valid serialized message is still valid wire-format data) before running out of bytes partway through a field and failing, so it does a meaningful fraction of the full parse's work before erroring.

**The all-`0xFF` garbage input fails dramatically faster — about 15x faster than truncation, and about 15x faster than a valid parse (12.44ns).** This matches the encoding mechanism directly: every `0xFF` byte has its varint continuation bit set, so the very first thing the parser does — decoding the leading tag as a varint — immediately runs into a malformed-varint condition once it reads past protobuf's maximum varint length (10 bytes) without finding a terminating byte. It never gets far enough to interpret a single real field; the error is detected almost immediately, within the first ~10 bytes of a 117-byte buffer, regardless of how long the buffer is.

**Takeaway.** Parse failure cost is not constant — it scales with *how far into the input* a parser gets before hitting the actual structural problem. A clearly-corrupt header (bad tag/wire-type, as modeled by the all-`0xFF` case) is detected almost instantly and is cheap to reject. A merely-truncated-but-otherwise-well-formed prefix (as modeled by the truncation case) costs roughly proportional to how many valid fields it manages to decode before running out of data — still cheaper than a full successful parse, but not free. Neither failure mode is more expensive than success, so a server doing `ParseFromString` directly on untrusted bytes is not exposed to an asymmetric "rejecting garbage is slower than accepting valid input" cost in either of these two failure shapes.

## Scope note

This covers Phase 8 only (parse-failure cost for two deterministic malformed-input shapes derived from the `text` message: truncation, and all-`0xFF` garbage). It does not cover other failure shapes (e.g. partially-valid-then-corrupted-midstream, oversized/recursive nested messages, or invalid UTF-8 in string fields), which were out of scope for this phase.
