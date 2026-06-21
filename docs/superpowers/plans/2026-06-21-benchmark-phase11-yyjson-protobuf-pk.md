# Phase 11 — yyjson vs Protobuf PK Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the 8 already-existing Protobuf (Phase 1) and yyjson (Phase 10) benchmarks together, write an honest encode/decode-time-and-size comparison, and fold it into the final report.

**Architecture:** Zero new code. A single precise `--benchmark_filter` regex selects exactly the 4 Protobuf + 4 yyjson benchmarks already defined in `src/bench.cpp`; the run's JSON output is the sole source of truth for the analysis doc.

**Tech Stack:** Google Benchmark, existing `proto_bench` binary (no rebuild of new sources needed).

## Global Constraints

- Write zero new/modified `.cpp`/`.h`/`CMakeLists.txt` — every benchmark used here already exists and is already committed.
- The 8 target benchmarks, exactly: `BM_SerializeText`, `BM_ParseText`, `BM_SerializeMergedForward`, `BM_ParseMergedForward` (Protobuf, Phase 1); `BM_EncodeTextJsonYyjson`, `BM_DecodeTextJsonYyjson`, `BM_EncodeMergedForwardJsonYyjson`, `BM_DecodeMergedForwardJsonYyjson` (yyjson, Phase 10).
- Filter regex (verified to match exactly these 8 and nothing else): `^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$`
- Every number in the analysis doc and the final report must come from this run's `results/phase11-2026-06-21.json` — not estimated, not carried over from an older run.
- Report results honestly, including counter-intuitive ones (no smoothing toward "binary must be faster/smaller").
- Full spec: `docs/superpowers/specs/2026-06-21-benchmark-phase11-yyjson-protobuf-pk-design.md`.

---

## Task 1: Run the benchmarks, save results, write the analysis doc

**Files:**
- Create: `results/phase11-2026-06-21.json`
- Create: `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`

**Interfaces:**
- Consumes: the 8 existing benchmark functions in `src/bench.cpp` (no source changes — run only).

- [ ] **Step 1: Confirm the binary is current**

```bash
cmake --build build -j$(nproc)
```

Expected: builds successfully with no changes (no source files were touched), `proto_bench` up to date.

- [ ] **Step 2: Run the filtered benchmark suite and save JSON output**

```bash
./build/proto_bench --benchmark_filter="^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$" --benchmark_format=json --benchmark_out=results/phase11-2026-06-21.json
```

Expected: exits 0. Verify exactly 8 entries with exactly these names, nothing else:

```bash
python3 -c "import json; names = [b['name'] for b in json.load(open('results/phase11-2026-06-21.json'))['benchmarks']]; assert sorted(names) == sorted(['BM_SerializeText','BM_ParseText','BM_SerializeMergedForward','BM_ParseMergedForward','BM_EncodeTextJsonYyjson','BM_DecodeTextJsonYyjson','BM_EncodeMergedForwardJsonYyjson','BM_DecodeMergedForwardJsonYyjson']), names; print('OK', len(names), 'entries')"
```

Expected output: `OK 8 entries`. If this assertion fails, stop — the filter regex matched something unexpected; do not proceed to write the analysis doc until this passes.

- [ ] **Step 3: Extract the 8 numbers**

Read `results/phase11-2026-06-21.json` and, for each of the 8 `name` entries, record `real_time` (ns/op) and the `bytes` counter.

- [ ] **Step 4: Write `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`**

Use this skeleton, filling every `<...>` placeholder with the real number extracted in Step 3 (every `<...>` must be replaced — none may remain when this file is considered done):

```markdown
# Phase 11 — yyjson vs Protobuf PK Analysis

Date: 2026-06-21

## Method

复用 Phase 1 的 4 个 Protobuf benchmark 和 Phase 10 的 4 个 yyjson benchmark（均定义于
`src/bench.cpp`，本阶段未新增/修改任何代码），用
`--benchmark_filter="^BM_(Serialize|Parse)(Text|MergedForward)$|JsonYyjson$"` 同时跑出。
yyjson 是 Phase 10 在 nlohmann/json、RapidJSON、yyjson、cJSON 四库横向对比中选出的胜者
（详见 `docs/benchmarks/phase10-json-shootout-analysis.md`）。

## Results — TextMessage 形态

| 格式 | Encode (ns) | Decode (ns) | bytes |
| --- | --- | --- | --- |
| Protobuf | <real_time of BM_SerializeText> | <real_time of BM_ParseText> | <bytes> |
| yyjson | <real_time of BM_EncodeTextJsonYyjson> | <real_time of BM_DecodeTextJsonYyjson> | <bytes> |

## Results — MergedForwardMessage 形态

| 格式 | Encode (ns) | Decode (ns) | bytes |
| --- | --- | --- | --- |
| Protobuf | <real_time of BM_SerializeMergedForward> | <real_time of BM_ParseMergedForward> | <bytes> |
| yyjson | <real_time of BM_EncodeMergedForwardJsonYyjson> | <real_time of BM_DecodeMergedForwardJsonYyjson> | <bytes> |

## 结论

<按实测数字给出耗时倍数（Protobuf 比 yyjson 快/慢多少倍，encode 和 decode 分开说，因为
两者的倍数很可能不一样）和体积倍数，诚实报告——如果某一项 yyjson 反而更快或体积更小，
直接写出来，不要为了"二进制理应更优"这个先验印象去美化或回避。>

## Carried into final report

并入 `docs/benchmarks/final-report-phases-0-11.md`（和 `.zh-CN.md` 镜像）的 Phase 11 一节和
跨阶段结论。
```

When computing the ratios for the "结论" section: divide the larger `real_time` by the smaller one, separately for encode and for decode, and separately again for each shape if the two shapes disagree in which format wins — do not average across shapes or across encode/decode into one number, since the spec requires reporting any per-metric divergence honestly rather than collapsing it.

- [ ] **Step 5: Commit**

```bash
git add results/phase11-2026-06-21.json docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md
git commit -m "Run Phase 11 yyjson-vs-Protobuf PK benchmarks and record analysis"
```

---

## Task 2: Fold Phase 11 into the final report

**Files:**
- Modify: rename `docs/benchmarks/final-report-phases-0-10.md` → `docs/benchmarks/final-report-phases-0-11.md`
- Modify: rename `docs/benchmarks/final-report-phases-0-10.zh-CN.md` → `docs/benchmarks/final-report-phases-0-11.zh-CN.md`
- Modify: `CLAUDE.md` (check for stale `final-report-phases-0-10.md` references and update to `-0-11`)

**Interfaces:**
- Consumes: the numbers and conclusion written in Task 1's `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`.

- [ ] **Step 1: Rename both report files**

```bash
git mv docs/benchmarks/final-report-phases-0-10.md docs/benchmarks/final-report-phases-0-11.md
git mv docs/benchmarks/final-report-phases-0-10.zh-CN.md docs/benchmarks/final-report-phases-0-11.zh-CN.md
```

- [ ] **Step 2: Add a Phase 11 section to `final-report-phases-0-11.md`**

Find the existing per-phase index/table (the one with a Phase 10 row, added when Phase 10 was folded in) and add a Phase 11 row using the same column layout, pointing at:
- Spec: `docs/superpowers/specs/2026-06-21-benchmark-phase11-yyjson-protobuf-pk-design.md`
- Plan: `docs/superpowers/plans/2026-06-21-benchmark-phase11-yyjson-protobuf-pk.md`
- Results: `results/phase11-2026-06-21.json`
- Analysis: `docs/benchmarks/phase11-yyjson-protobuf-pk-analysis.md`

In the "Cross-cutting takeaways" section, add one bullet stating the real yyjson-vs-Protobuf margins from Task 1's analysis doc (encode and decode ratios, per shape if they diverge — use the actual numbers, not a placeholder).

Update every "Phases 0-10" prose reference in this file (title, date line, intro sentence — check the whole file, not just the title, since Phase 10's own fold-in touched more than one spot) to "Phases 0-11".

- [ ] **Step 3: Mirror the same edits into `final-report-phases-0-11.zh-CN.md`**

Add the equivalent Phase 11 row and cross-cutting takeaway bullet, translated to Chinese (not copy-pasted English), matching the style of the existing Phase 10 entry. Update every "Phases 0-10" prose reference to "Phases 0-11" here too.

- [ ] **Step 4: Check and fix `CLAUDE.md` for stale filename references**

```bash
grep -n "final-report-phases-0-10" CLAUDE.md
```

If this prints any matches (Phase 10's own fold-in needed exactly this fix for the `-0-9` → `-0-10` rename, so expect the same here), update each one from `final-report-phases-0-10.md` to `final-report-phases-0-11.md`. If it prints nothing, no action needed for this file.

- [ ] **Step 5: Confirm nothing else references the old filename**

```bash
grep -rn "final-report-phases-0-10" --include="*.md" . | grep -v "docs/superpowers/specs/\|docs/superpowers/plans/"
```

Expected: no output (specs/plans are historical records and are allowed to keep referencing the filename that was current when they were written — only living docs like `CLAUDE.md` and the final report itself need updating).

- [ ] **Step 6: Rebuild and re-run the correctness suite for safety**

```bash
cmake --build build -j$(nproc) && ./build/proto_test
```

Expected: unchanged — all protobuf/SBE/nlohmann/RapidJSON/yyjson/cJSON round-trip suites still pass (this task touched no source files, so this is a sanity check, not an expected-to-fail step).

- [ ] **Step 7: Commit**

```bash
git add docs/benchmarks/final-report-phases-0-11.md docs/benchmarks/final-report-phases-0-11.zh-CN.md CLAUDE.md
git commit -m "Fold Phase 11 yyjson-vs-Protobuf PK into the final report"
```

---

## Self-Review

- **Spec coverage:** Task 1 covers the spec's "组件设计" §1 (filter regex, already verified during planning), §2 (run + produce results JSON), §3 (analysis doc). Task 2 covers §4 (final-report rename + fold-in + `CLAUDE.md` check). The spec's "不包含" exclusions (no new code, no scale sweep/concurrency/alloc-counting/malformed-input, no re-running Phase 10's 4-library shootout, no new JSON convention) are honored by omission — no task introduces them.
- **Placeholder scan:** All commands and skeleton content are complete and exact. The only `<...>` placeholders are in Task 1's analysis-doc skeleton, explicitly called out as required-before-done runtime values (the benchmark numbers don't exist until the suite runs) — not a vague TBD.
- **Type consistency:** N/A — no code, no functions/types introduced across tasks. File paths referenced in Task 2 (the 4 paths for the Phase 11 row) match exactly what Task 1 creates.
