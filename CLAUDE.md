# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A C++20 benchmark suite comparing Protobuf and SBE (Simple Binary Encoding) serialization for a single IM-chat message schema (`im.chat.v1.ChatMessage`, defined in `proto/chat.proto`, mirrored in `schema/chat.sbe.xml`). The project is organized as a sequence of numbered "phases," each asking one specific performance question (throughput, field fill-rate, scalability, Arena allocation, API overhead, concurrency, malformed input, Protobuf-vs-SBE). It is a research/analysis project, not a service — there is no runtime application to deploy.

## Build

```sh
cmake -S . -B build
cmake --build build -jN   # always pass an explicit job count, e.g. -j$(nproc)
```

CMake `FetchContent` pulls protobuf v35.1, Abseil, and Google Benchmark v1.9.1 on first configure (`GIT_SHALLOW`) — expect a slow first build. `CMAKE_BUILD_TYPE` defaults to `Release` if unset; benchmark numbers from Debug builds are ~10x slower/noisier and not representative, so never benchmark a Debug build.

SBE C++ codecs are generated from `schema/chat.sbe.xml` by a Java tool (`sbe-all-1.38.1.jar`, auto-downloaded) via the `generate_chat_sbe` CMake target — requires a JRE (`find_package(Java REQUIRED COMPONENTS Runtime)`).

Targets:
- `chat_proto` — static lib holding the generated `chat.pb.{h,cc}` from `proto/chat.proto`. Both `proto_test` and `proto_bench` link this so `protoc` only runs once per build.
- `chat_sbe` — interface lib exposing the generated SBE headers (`build/generated-sbe/im_chat_sbe/`).
- `proto_test` — correctness round-trip tests for both Protobuf and SBE encodings (`src/main.cpp`).
- `proto_bench` — the Google Benchmark suite (`src/bench.cpp`), covers both Protobuf and SBE paths.
- `reflection_demo` — standalone example of `google::protobuf::Reflection`/`Descriptor` walking a message generically (`src/reflection_demo.cpp`).

## Running

```sh
./build/proto_test                                                              # correctness checks, asserts on failure
./build/proto_bench                                                             # full benchmark suite
./build/proto_bench --benchmark_filter="Concurrent"                             # subset by regex on benchmark name
./build/proto_bench --benchmark_format=json --benchmark_out=results/phaseN-DATE.json --benchmark_filter="..."
```

Every phase's raw output lives in `results/phaseN-*.json`; never hand-edit these — regenerate via `proto_bench` with `--benchmark_out`.

## Architecture

- `proto/chat.proto` is the single source of truth for the message schema (`ChatMessage` envelope with `oneof content` of `TextContent`/`ImageContent`/`AudioContent`/`VideoContent`/`FileContent`/`RecallNotice`/`SystemEvent`/`MergedForwardContent`, plus `QuoteInfo`, `ForwardInfo`, a `map<string,string> extra`, etc.). `schema/chat.sbe.xml` defines the equivalent SBE templates (`TextChatMessage`, `MergedForwardChatMessage`) for the same logical content — when changing one, check whether the other needs a matching update for the comparison to stay apples-to-apples.
- `src/message_fixtures.{h,cpp}` builds deterministic `ChatMessage` payloads (`BuildTextMessage`, `BuildSparseTextMessage`, `BuildTextMessageWithId`, `BuildTextMessageWithMentionCount`, `BuildMergedForwardMessage`, `BuildMergedForwardMessageWithItemCount`) reused by `proto_test`, `proto_bench`, and `reflection_demo` so every phase measures identical baseline payloads. `src/sbe_message_fixtures.{h,cpp}` provides the SBE-encoding equivalents (`EncodeTextMessageSbe`, `EncodeMergedForwardMessageSbe`, etc.) for the same logical content. When adding a benchmark, prefer reusing/extending these fixtures over building one-off messages in `bench.cpp`.
- `src/alloc_counter.{h,cpp}` instruments heap allocations (count + bytes) for the Arena-vs-heap benchmarks (`ResetAllocCounters`/`GetAllocCount`/`GetAllocBytes`).
- `docs/superpowers/specs/` and `docs/superpowers/plans/` hold the spec → plan → implement → verify → commit → merge artifacts per phase (the `superpowers` skill workflow). `docs/benchmarks/phaseN-*.md` are the resulting analysis writeups, and `docs/benchmarks/final-report-phases-0-9.md` (with a `.zh-CN.md` mirror) is the cross-phase synthesis — read it first when picking up benchmark work, since it indexes every phase's spec/plan/results/analysis file and states the cross-cutting takeaways (e.g. repeated-message fields are the most expensive schema construct measured; Arena only pays off under concurrency, not single-threaded; "zero-copy" stream APIs aren't always faster than flat-buffer APIs).
- Phase 6 (CPU microarchitecture counters via `perf`) was skipped as infeasible in this sandbox (`perf_event_paranoid=4`, no privilege escalation) — documented in `docs/benchmarks/phase6-feasibility-note.md` rather than faked. If revisiting on a host with elevated privileges, `perf stat` can wrap `proto_bench` externally with no code changes.

## Conventions for adding a new phase

Follow the existing pattern visible in `docs/superpowers/{specs,plans}/` and the final report's file index: write a spec, write a plan, implement against `message_fixtures`/`sbe_message_fixtures`, run `proto_bench` with `--benchmark_out` into `results/phaseN-DATE.json`, write the analysis into `docs/benchmarks/phaseN-*.md`, then fold the finding into `final-report-phases-0-9.md` (and its `.zh-CN.md` mirror) under both the phase table and the cross-cutting takeaways if it changes the overall picture. Report negative/counter-intuitive results honestly (e.g. Phase 4's never-reset Arena being *slower* than heap, Phase 9's SBE-encode-scaling-worse-under-concurrency result) rather than smoothing them into a cleaner-sounding narrative — this project's existing docs consistently do this and future analysis should match that standard.

<!-- code-review-graph MCP tools -->
## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes` or `query_graph` instead of Grep
- **Understanding impact**: `get_impact_radius` instead of manually tracing imports
- **Code review**: `detect_changes` + `get_review_context` instead of reading entire files
- **Finding relationships**: `query_graph` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview` + `list_communities`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context` | Need source snippets for review — token-efficient |
| `get_impact_radius` | Understanding blast radius of a change |
| `get_affected_flows` | Finding which execution paths are impacted |
| `query_graph` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes` | Finding functions/classes by name or keyword |
| `get_architecture_overview` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes` for code review.
3. Use `get_affected_flows` to understand impact.
4. Use `query_graph` pattern="tests_for" to check coverage.
