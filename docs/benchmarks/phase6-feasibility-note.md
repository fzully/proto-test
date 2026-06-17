# Phase 6 — CPU Microarchitecture Metrics: Feasibility Check (Skipped)

Date: 2026-06-18

## Goal (as originally scoped)

Use `perf stat` to collect CPU microarchitecture counters (cycles, instructions, IPC, cache misses, branch mispredictions) for the existing serialize/parse benchmarks, to see whether e.g. the Phase 3 `repeated message` slowdown or the Phase 5 CodedOutputStream slowdown correlate with cache or branch-prediction effects rather than pure instruction count.

## Feasibility check performed

```
$ perf --version
perf version 6.17.13
$ cat /proc/sys/kernel/perf_event_paranoid
4
$ perf stat -e cycles,instructions ./build/proto_test
Error: Access to performance monitoring and observability operations is limited.
...perf_event_paranoid setting is 4: Disallow CPU event access (and more)...
```

`perf_event_paranoid=4` blocks all hardware performance-counter access for processes without `CAP_PERFMON`/`CAP_SYS_PTRACE`/`CAP_SYS_ADMIN`. Lowering it requires root:

```
$ sudo -n true
sudo: a password is required
$ echo 0 > /proc/sys/kernel/perf_event_paranoid
Permission denied
```

No passwordless `sudo` is configured, so the setting cannot be lowered without an interactive password prompt — not available in this autonomous, unattended run.

Checked for a no-privilege fallback (`valgrind --tool=cachegrind`, which simulates cache/branch behavior in software and needs no special kernel permissions):

```
$ which valgrind
(not found)
```

Not installed, and installing it (`apt install valgrind`) also requires root.

## Decision

Per the Phase 5 design spec's explicit caveat ("若不可行则在该阶段说明并跳过/降级，而不是强行设计一个跑不起来的方案" — if infeasible, document and skip/downgrade rather than force a design that can't run), Phase 6 is **skipped** in this environment. Both viable approaches (`perf` hardware counters, `valgrind` software cache simulation) are blocked by the same root cause: no elevated privileges available in this unattended session, and no passwordless path to obtain them.

This is an environment limitation, not a design flaw — if this benchmark suite is later run on a host with `perf_event_paranoid<=2` (or as root/with `CAP_PERFMON`), the originally scoped `perf stat` design (wrapping each `proto_bench --benchmark_filter=...` invocation with `perf stat -e cycles,instructions,cache-misses,branch-misses`) would be straightforward to add — no code changes to `proto_bench` itself are needed, since `perf stat` profiles a child process externally.

## Scope note

Phase 6 (CPU microarchitecture counters) is not implemented in this repository due to sandbox/permission constraints documented above. Phases 0-5, 7, and 8 are unaffected.
