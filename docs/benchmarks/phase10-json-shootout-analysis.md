# Phase 10 — JSON Library Shootout Analysis

Date: 2026-06-20

## Method

4 libraries (nlohmann/json, RapidJSON, yyjson, cJSON) hand-encode/decode the same
two logical messages (`BuildTextMessage`, `BuildMergedForwardMessage`) into JSON,
using the field mapping defined in
`docs/superpowers/specs/2026-06-20-benchmark-phase10-json-shootout-design.md`
(camelCase keys, int64/enum as JSON strings, compact output). simdjson was
excluded — it has no symmetric high-performance writer, so it cannot be ranked
on the same "encode+decode total time" metric as the other 4.

Ranking metric: sum of encode + decode `real_time` (ns/op), summed across both
shapes (Text + MergedForward). Lowest total wins.

## Results — TextMessage shape

| Library | Encode (ns) | Decode (ns) | Encode+Decode (ns) | bytes |
| --- | --- | --- | --- | --- |
| nlohmann/json | 2246.14 | 3145.09 | 5391.23 | 408 |
| RapidJSON | 363.81 | 817.59 | 1181.40 | 408 |
| yyjson | 153.19 | 370.70 | 523.88 | 408 |
| cJSON | 893.98 | 1131.59 | 2025.57 | 408 |

## Results — MergedForwardMessage shape

| Library | Encode (ns) | Decode (ns) | Encode+Decode (ns) | bytes |
| --- | --- | --- | --- | --- |
| nlohmann/json | 3710.87 | 3906.47 | 7617.34 | 500 |
| RapidJSON | 495.59 | 1040.32 | 1535.91 | 500 |
| yyjson | 202.69 | 477.25 | 679.94 | 500 |
| cJSON | 1150.96 | 1398.83 | 2549.79 | 500 |

## Ranking (both shapes summed)

| Rank | Library | Total Encode+Decode (ns, both shapes) |
| --- | --- | --- |
| 1 | yyjson | 1203.82 |
| 2 | RapidJSON | 2717.31 |
| 3 | cJSON | 4575.36 |
| 4 | nlohmann/json | 13008.57 |

**Winner: `yyjson`.** yyjson's total (1203.82 ns) beats RapidJSON's (2717.31 ns)
by 2.26x and beats nlohmann/json's (13008.57 ns) by 10.8x — this matches
yyjson's own marketing claim of being the fastest C/C++ JSON library, and the
margin here is large enough that it isn't noise. The more counter-intuitive
result is cJSON: despite being the oldest and simplest of the four libraries
(plain C, no SIMD, no arena allocator), it beats nlohmann/json by 2.84x on
total time (4575.36 ns vs 13008.57 ns) on both shapes. nlohmann/json's
modern, ergonomic API comes from a `std::map`-backed DOM with heavy
type-erasure and exception-driven parsing, and that overhead shows up
directly here — it is slower than all three of the other libraries on every
single one of the 8 encode/decode measurements, not just on average.

## Carried into Phase 11

`yyjson` is the JSON library used for the Phase 11 vs-Protobuf PK (encode/decode
time + size, aligned with Phase 1/9's scope).
