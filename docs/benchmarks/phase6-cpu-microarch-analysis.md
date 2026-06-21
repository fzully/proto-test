# Phase 6 — CPU Microarchitecture Metrics: Analysis (Re-enabled)

Date: 2026-06-21
Raw data: `results/phase6-2026-06-21.txt`

## Status change

Phase 6 was originally documented as skipped (`docs/benchmarks/phase6-feasibility-note.md`) because this sandbox had `kernel.perf_event_paranoid=4`, which blocks all hardware-counter access without `CAP_PERFMON`. The user has since lowered it on the host:

```
$ echo 'kernel.perf_event_paranoid=2' | sudo tee /etc/sysctl.d/99-perf.conf
$ sudo sysctl --system
$ cat /proc/sys/kernel/perf_event_paranoid
2
```

`perf stat` is now usable, so this phase has been re-run for real. The feasibility note is preserved as a historical record but is no longer the active status (see its updated heading).

## Design adjustment: hybrid P-core/E-core CPU

The original Phase 6 scope (in the Phase 0-1 design doc and the Phase 5 design doc's feasibility caveat) assumed a single homogeneous CPU. This host is actually a **hybrid architecture** (`lscpu -e` shows logical CPUs 0-11 as 6 SMT P-cores, max 5100MHz; CPUs 12-19 as 8 single-thread E-cores, max 3900MHz — 20 logical CPUs total). `perf` exposes this as two separate PMUs, `cpu_atom` and `cpu_core`, and splits every requested event into both:

```
$ perf stat -e cycles,instructions,cache-misses,branch-misses ./build/proto_bench --benchmark_filter="BM_SerializeText$"
     721,402,174      cpu_atom/cycles/u                       (0.35%)
   1,848,917,300      cpu_core/cycles/u                       (99.65%)
   ...
```

An unpinned process gets scheduled across both core types during its lifetime, so most of the signal lands on whichever type it happened to run on, and the other type shows `<not counted>` or a tiny, noisy sliver. This is not how the original design (single PMU, one set of numbers) was scoped, so it needed adjusting:

- **Pin to one core type**: `taskset -c 0-11 perf stat ...` confines the process to the P-cores. This makes `cpu_atom/*` consistently `<not counted>` and gives `cpu_core/*` the full count with low relative stddev (sub-1.3% on cycles/instructions across repeats, vs uncontrolled runs splitting noisily across both PMUs).
- **Repeat and average**: `perf stat -r 3` reruns the whole process 3x and reports mean ± stddev, which also surfaces how noisy each counter actually is (see caveats below).
- **Lengthen the run**: bumped `--benchmark_min_time` to `2s` (from Google Benchmark's default ~0.5s). `perf stat` measures the *entire process lifetime* — binary load, fixture construction, Google Benchmark's own iteration-count auto-calibration passes, not just the final timed loop — so a longer steady-state run dilutes that one-time overhead relative to the measured iterations. This matters most for `BM_SerializeMergedItems/1000`, the slowest/lowest-iteration-count target tested (~108K iterations vs ~39M for the cheap targets); cross-checking `cycles/iter ÷ ~5.1GHz` against each benchmark's self-reported `ns/iter` after this change tracks within ~10-20% (vs ~70% off at the default min-time), which is the residual contamination still present — treat absolute per-iteration cycle/instruction counts as good to roughly one significant figure, not exact.

## Methodology

Targets were chosen to match Phase 6's original motivating question: does the Phase 3 repeated-message slowdown, and the Phase 5 `CodedOutputStream` slowdown, correlate with cache-miss/branch-misprediction effects rather than pure instruction-count growth?

```
taskset -c 0-11 perf stat -r 3 -e cycles,instructions,cache-misses,branch-misses \
  ./build/proto_bench --benchmark_filter="^<target>$" --benchmark_min_time=2s
```

Targets: `BM_SerializeMentions/1`, `BM_SerializeMentions/1000` (Phase 3, packed scalar repeated field), `BM_SerializeMergedItems/1`, `BM_SerializeMergedItems/1000` (Phase 3, repeated message field), `BM_SerializeText`, `BM_SerializeToPreallocatedArray`, `BM_SerializeToCodedStream` (Phase 5, serialize API comparison).

## Results — Phase 5 correlation (serialize API overhead)

Per-iteration values, derived from `perf`'s mean cycle/instruction/event totals divided by each target's mean reported iteration count across the 3 repeats:

| Benchmark | ns/iter (self-reported) | cycles/iter | instructions/iter | IPC | cache-misses/iter | branch-misses/iter |
|---|---|---|---|---|---|---|
| BM_SerializeText (baseline) | ~72-75 | 477.7 | 1863.6 | 3.90 | 0.00062 | 0.00057 |
| BM_SerializeToPreallocatedArray (fastest) | ~70-72 | 466.2 | 1786.0 | 3.83 | 0.00062 | 0.00058 |
| BM_SerializeToCodedStream (slowest) | ~82-85 | 565.9 | 2199.4 | 3.89 | 0.00078 | 0.00063 |

`CodedOutputStream` costs +21% cycles and +23% instructions per call versus `SerializeToPreallocatedArray`, while IPC stays flat across all three variants (3.83-3.90) and cache-miss/branch-miss rates stay in the same sub-0.001-per-iteration noise band for all three. **This confirms Phase 5's slowdown is a pure extra-instructions effect, not a stalling/cache effect**: routing through `MessageLite`'s `ZeroCopyOutputStream` virtual interface (`Next()`/`BackUp()` bookkeeping) makes the CPU execute more instructions per call, but doesn't make it stall more per instruction — the pipeline keeps the same throughput (~3.9 IPC), it just has more work queued up. This directly substantiates the mechanism Phase 5's analysis proposed from API knowledge alone ("virtual-dispatch and bookkeeping overhead") with a hardware-counter measurement.

## Results — Phase 3 correlation (packed scalar vs repeated message)

| Benchmark | n | cycles/iter | instructions/iter | cache-misses/iter | branch-misses/iter |
|---|---|---|---|---|---|
| BM_SerializeMentions (packed `repeated int64`) | 1 | 464.0 | 1828.5 | 0.00076 | 0.00062 |
| BM_SerializeMentions | 1000 | 6650.5 | 42184.0 | 0.00806 | 4.139 |
| BM_SerializeMergedItems (`repeated message`) | 1 | 500.2 | 2190.8 | 0.00072 | 0.00074 |
| BM_SerializeMergedItems | 1000 | 144,102 | 665,886 | 0.599 | 5.849 |

Marginal per-element cost (n=1000 value minus n=1 value, divided by 999), and the ratio of the repeated-message marginal cost to the packed-scalar marginal cost for each counter:

| Counter | Mentions (scalar), per element | MergedItems (message), per element | Ratio (message ÷ scalar) |
|---|---|---|---|
| cycles | 6.19 | 143.66 | **23.2x** |
| instructions | 40.39 | 663.95 | **16.4x** |
| cache-misses | 0.0000073 | 0.000598 | **81.9x** |
| branch-misses | 0.004138 | 0.005850 | **1.41x** |

This is the answer Phase 6 originally set out to find. The ~23x per-element cycle-cost gap between repeated-message and packed-scalar encoding (consistent with Phase 3's documented ~30x latency gap) is **not evenly explained by instruction count alone** (16.4x) — cache misses scale up ~82x per element, almost 5x more steeply than instructions do, while branch mispredictions barely move (1.4x). In other words: the dominant *microarchitectural* signature of the repeated-message slowdown is memory-access scatter (each `ForwardedItem` sub-message touching new, less-contiguous memory as it's constructed/encoded), not branch misprediction, and not purely "more instructions" — cache effects are a disproportionately large contributor on top of the instruction-count growth that Phase 3's analysis already attributed to per-item allocation and recursive sub-message encoding.

## Caveats on precision

- **Absolute cache-miss/branch-miss counts are small and noisy.** Per-repeat relative stddev on these two counters ranged 3.6%-16% (vs <1.3% for cycles/instructions) — they're built from raw counts in the tens of thousands to low millions, an order of magnitude lower-resolution signal than cycles/instructions. Treat the ratios above as directional/order-of-magnitude evidence, not precise point estimates.
- **`perf stat` measures the whole process, not just the timed loop.** Google Benchmark's own startup, fixture construction, and iteration-count auto-calibration are included in every counter. The 2-second `--benchmark_min_time` dilutes this to roughly 10-20% residual error (estimated by comparing `cycles/iter ÷ host clock` against each benchmark's self-reported `ns/iter`); it is not exactly zero, especially for `BM_SerializeMergedItems/1000` which has the fewest iterations of the targets tested.
- **This host has no isolated/pinned-frequency benchmarking setup** (`cpu_scaling_enabled: true`, consistent with every other phase's caveat in this suite) — `taskset` controls *which* cores run the work, not their clock frequency, which can still vary under turbo/thermal effects between repeats.

## Scope note

This phase covers the two specific cross-references its original goal named (Phase 3's repeated-message slowdown, Phase 5's `CodedOutputStream` slowdown) on the `text`/`merged_forward` message shapes already used throughout this suite. It does not cover Phase 7 (concurrent) or Phase 9 (SBE) workloads — extending this methodology there (especially per-thread `perf stat` for the concurrency benchmarks) is a natural follow-up but was not requested here.
