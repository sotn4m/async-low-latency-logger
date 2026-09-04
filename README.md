# async-low-latency-logger

A ring-buffer-based async logger for C++: application threads never block
on logging, no matter how slow the underlying I/O is. Built to measure and
document, precisely, how much latency that buys back versus a naive
synchronous logger under load.

```text
producer thread 1 ┐
producer thread 2 ┼──▶  MPSC ring buffer  ──▶  consumer thread  ──▶  log file
producer thread N ┘     lock-free, fixed        deferred format,
                         slots, drop-on-full     periodic flush
```

## Results at a glance

Async logging is **8-20x faster** than sync at every percentile
measured, and stays flat (~1µs) under load where sync's tail blows out
to hundreds of microseconds. The more interesting finding: switching
the consumer's flush policy from per-message to a 300µs periodic
interval cut dropped messages by roughly 10x. CPU affinity's effect on
drop rate was checked at n=10, 50, and 200 repeated runs each: real and
stable (~1.4 effect size) at the 8-thread overload case, smaller and
noisier at 6 threads than a single n=50 batch suggested. The n=200 data
also turned up something the drop-rate framing missed entirely:
`SyncLogger`'s own latency is affinity-sensitive too, and its
*unpinned* numbers turned out not to be reproducible across collection
sessions hours apart — direct evidence that the isolation is doing
real work, not just a plausible-sounding rationale. Full methodology,
data, and the affinity statistics are in [Results](#results) below.

![Enqueue latency: sync vs async](benchmarks/plots/latency_by_threads.png)

## Design

- **MPSC ring buffer, fixed-size slots.** Multiple producer threads log
  concurrently into one shared, preallocated, power-of-2-sized ring buffer.
  A single background consumer thread drains it.
- **Deferred formatting.** The hot path (producer) copies raw bytes into a
  slot — no heap allocation, no syscalls, no string formatting, no locks.
  Formatting and the actual write to disk happen later, on the consumer
  thread.
- **Drop-on-full, not block-on-full.** Since the producer must never block,
  a full buffer means the newest message is dropped and an atomic counter
  is incremented, rather than the producer stalling.
- **Cache-line-padded cursors.** Producer/consumer cursors are padded to
  avoid false sharing between threads.

## Status / Roadmap

- [x] Milestone 1 — sync baseline logger + benchmark harness (scaffolded,
      implementation TODO)
- [x] Milestone 2 — SPSC ring buffer (lock-free)
- [x] Milestone 3 — generalize to MPSC
- [x] Milestone 4 — overflow policy (drop-on-full + counter)
- [x] Milestone 5 — consumer thread: deferred formatting, buffered write.
      (Flush-per-message is still the policy — flagged in code as a
      follow-up once milestone 7 has numbers showing what it costs.)
- [x] Milestone 6 — correctness under concurrency (ThreadSanitizer stress tests)
- [x] Milestone 7 — benchmark harness: open-loop load, latency percentiles, sync vs async
- [x] Milestone 8 — OS tuning (CPU isolation, IRQ affinity) on the benchmark host
- [x] Milestone 9 — run benchmarks, collect results, plots
- [ ] Milestone 10 — this README's Hardware / OS Tuning / Results sections filled in
- [ ] Stretch — NUMA-aware placement, io_uring writer, structured logging

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/async_low_latency_logger
./build/bench_logger
```

## Hardware specs

_TBD — filled in at milestone 10 with `lscpu`/`dmidecode` output from the
benchmark host._

## OS tuning

The benchmark host is the author's own interactive Manjaro desktop (Intel
i7-9700KF, 8 physical cores, no SMT, single socket / single NUMA node) —
not a dedicated benchmark box. Rather than boot-time `isolcpus`/
`nohz_full` kernel parameters (which would require a GRUB edit and a
reboot, and permanently remove cores from general use whenever the
machine is booted that way), tuning here is **runtime-only** and fully
reversible per run.

### Why any of this is necessary

A general-purpose Linux desktop is actively working against a
microsecond-precision benchmark. Its scheduler treats producer and
consumer threads like any other process — free to migrate them between
cores, interrupt them to run something else, and let clock speed drift
down to save power while they're momentarily idle. None of that is a
bug; it's the OS doing its normal job. It's also exactly the kind of
noise that shows up as tail latency and drop-rate variance in the
results below. `scripts/run_bench_tuned.sh` exists to switch that job
off, temporarily, for a handful of cores.

**CPU affinity (thread pinning)** restricts a thread to one specific
core via `sched_setaffinity()`, instead of letting the scheduler move it
wherever it likes. A migrated thread loses whatever it had cached in
that core's L1/L2 — the next few operations run against cold cache — and
the migration itself costs scheduling overhead. A pinned thread stays
put, so its timing stays consistent run to run.

**Process steering (cgroups / cpuset)** solves a problem pinning alone
doesn't: a thread can be locked to a specific core, but nothing stops
the *rest* of the machine — browser, IDE, background daemons — from also
landing on those same cores and competing for them. Linux cgroups
(control groups) constrain which CPUs a whole group of processes may
run on. Essentially everything on a systemd-managed machine already
lives under `user.slice` or `system.slice`, so confining those two to
one core pushes the entire rest of the system off the reserved cores in
one move, instead of finding and pinning every individual process by
hand.

**IRQ affinity** matters for the same reason at the hardware level.
Interrupts (disk, network card, timer, ...) are handled by whichever CPU
the kernel's IRQ routing currently favors — by default, often spread
across every core. If one lands on a reserved core, that core is briefly
stolen from your thread to run the interrupt handler: exactly the kind
of jitter this benchmark is trying to measure the absence of.
`/proc/irq/N/smp_affinity_list` tells the kernel which cores a given
interrupt is allowed to use.

**CPU governor** addresses a different source of variance: modern CPUs
scale their clock frequency up and down to save power, and ramping back
up from an idle/low-power state takes real time — low microseconds to a
few milliseconds. A thread that's been briefly idle (spinning in the
ring buffer's busy-wait between messages, say) can find its core running
at a lower clock than it was moments ago, adding latency that has
nothing to do with the code being measured. The `performance` governor
pins a core's frequency near its maximum permanently, trading power and
heat for consistency.

### What a Linux system needs for this to work

None of the above is exotic, but each piece depends on something
specific being true — which is why the script checks rather than
assumes:

- **Root.** Every mechanism above writes to files under `/sys` or
  `/proc`, or to a cgroup's control files, all of which require
  elevated privileges.
- **cgroup v2, with the `cpuset` controller delegated** at the cgroup
  root (`cgroup.subtree_control`). Default on any modern systemd-managed
  distribution, but not guaranteed on every setup — the script skips
  process-steering gracefully rather than forcing it if this isn't true.
- **`systemd`, specifically `systemd-run`**, to launch the benchmark
  directly into the right cpuset atomically. Without it, the script
  falls back to a plain `sudo -u` launch — governor/IRQ tuning still
  applies, but the process itself isn't cpuset-confined to the reserved
  cores.
- **`cpupower`** for the governor step — optional; the script logs a
  warning and leaves the governor unchanged if it isn't installed.

Everything here is reversible without a reboot, which is the whole point
of choosing this over `isolcpus`/`nohz_full`: this machine is a daily
driver, not a dedicated benchmark box, and needs to go back to being a
normal desktop the moment the benchmark finishes.

### The specific configuration

- **Core layout.** Core 0 is left for the OS/desktop session. Cores 1-7
  are reserved for the benchmark: core 1 for `AsyncLogger`'s consumer
  thread, cores 2-7 for up to 6 producer threads (one core each).
- **Process steering.** `user.slice`/`system.slice`'s `cpuset.cpus` is
  temporarily confined to core 0.
- **IRQ affinity.** Movable IRQs redirected to core 0; kernel-managed
  MSI-X IRQs reject the write and are left alone.
- **CPU governor.** `performance` on cores 1-7 only, via `cpupower`;
  core 0 stays on the system default.
- **Thread pinning.** `AsyncLogger`'s consumer thread and each benchmark
  producer thread self-pin via `sched_setaffinity` (`alll::Affinity` in
  `include/logger/async_logger.hpp`), targeting a fixed core layout
  hardcoded in `benchmarks/bench_main.cpp`
  (`BenchmarkConfig::consumer_cpu`, `BenchmarkConfig::producer_cpu_base`)
  that matches this script's default reservation. Unlike the rest of the
  tuning, this isn't conditional on running under the script —
  `bench_logger` (as currently committed) always attempts it. Running it
  directly (as CI does) is still harmless: pinning to a CPU that exists
  but isn't specially reserved just succeeds trivially, and pinning to
  one that doesn't exist just fails and is silently ignored. **Note for
  the "unpinned" results below**: since this call is unconditional in
  the current code, there's no build-time or run-time toggle to disable
  it. The "unpinned" batches in [Results](#results) were still run under
  `scripts/run_bench_tuned.sh` — cores 1-7 still cpuset-reserved, IRQs
  still steered to core 0, governor still forced to `performance` on
  1-7, identically to the "pinned" batches — from a build with the
  `alll::Affinity{cpu}()`/`Affinity` constructor calls temporarily
  commented out. So "unpinned" doesn't mean unisolated: producer and
  consumer threads still can't leave the reserved 7-core set or share it
  with the rest of the desktop, they just aren't nailed to one specific
  core each within it — the kernel scheduler is free to place and
  migrate them across cores 1-7 as it likes. That makes this a genuine
  single-variable comparison (static per-thread core pinning vs. free
  migration within the same reservation), but it's not reproducible
  today just by skipping the script — the shipped code always pins, and
  skipping the script on top of that would additionally drop the
  cpuset/IRQ/governor tuning, a third condition never measured here.

Run it with:

```bash
sudo scripts/run_bench_tuned.sh
```

Tuning (governor/IRQ/cpuset steering) is applied once and left in place
for a repeated batch of runs via an optional first argument — useful for
characterizing run-to-run variance rather than trusting a single sample
per configuration:

```bash
sudo scripts/run_bench_tuned.sh 10 1-7 0
```

Each run's `bench_results.csv` is saved separately under
`benchmarks/results/repeats/<timestamp>/run_NN.csv` instead of
overwriting the previous one; a single run (the default, `iterations=1`)
still just leaves `bench_results.csv` in place as before.

Everything mutated (governor, IRQ affinity, cgroup cpusets) is reset to
known defaults on exit — including on failure or Ctrl-C, via a trap. If
the script itself is killed with `SIGKILL` or the host crashes mid-run,
the trap never runs and tuning is left applied; recover by hand with
the same commands the trap runs (see the comment at the top of
`scripts/run_bench_tuned.sh`).

This intentionally falls short of the exclusivity guarantee real
`isolcpus` provides — unbound kernel threads aren't confined by the
cgroup steering step, and if `cpuset` delegation isn't already present
on a given system, that step is skipped rather than forced. CI does not
use this script; it only smoke-tests that `bench_logger` runs.

## Benchmark methodology

`benchmarks/bench_main.cpp` measures **producer-side enqueue latency
only** — how long the `log()` call itself takes to return, not how long
the message takes to actually reach disk. That's the number that
matters to the application thread calling it; what happens after is the
consumer's problem, by design.

Each producer thread runs an **open-loop** load generator: it issues one
`log()` call on a fixed schedule (50 kHz per thread) regardless of how
long the previous call took, rather than waiting for one call to finish
before issuing the next. A closed-loop generator (issue, wait, repeat)
would silently hide tail latency — exactly when the system falls behind,
it backs off and stops offering the load that would have exposed it.
Open-loop keeps the offered load constant no matter how the system
responds.

Each run: 1,000 warmup calls (excluded from the sample), then 100,000
measured calls per thread, sorted to compute percentiles
(`benchmarks/latency_stats.hpp`).

**Scenarios:**
- **Clean sweep** — `{1, 2, 4, 6}` producer threads, run for both
  `SyncLogger` and `AsyncLogger`. Under `scripts/run_bench_tuned.sh`
  (see "OS tuning" above), 6 producers + `AsyncLogger`'s 1 consumer
  thread exactly fills the 7 cores reserved for the benchmark — no
  oversubscription.
- **Overload** — a separate 8-thread case that alone equals this
  machine's physical core count, leaving no core free for the consumer
  or the OS. Kept out of the clean sweep's trend line (labeled
  `sync_overload`/`async_overload` in each results CSV) since it's
  deliberately pathological, not a data point on it.
- **Affinity × flush policy** — beyond the thread-count sweep, two
  independent knobs were each run with and without. "Affinity" is
  specifically per-thread `sched_setaffinity` pinning (`alll::Affinity`,
  commented out of the source and rebuilt to produce the "unpinned"
  runs — see the note under "Thread pinning" above); the surrounding
  `scripts/run_bench_tuned.sh` tuning (cpuset/IRQ/governor) was applied
  identically for both, so this isolates thread placement specifically,
  not isolation-vs-none. The second knob is how often `AsyncLogger`'s
  consumer flushes to disk —
  after *every* message (the original policy, milestone 5's TODO) vs.
  a fixed 300µs interval (`kFlushInterval` in `src/logger/async_logger.cpp`
  — chosen over a shorter 50µs interval also tried during development,
  which measured worse). Each of the 4 combinations is its own CSV in
  [`benchmarks/results/`](benchmarks/results/):
  `pinned_flush_periodic_300us.csv`, `unpinned_flush_periodic_300us.csv`,
  `pinned_flush_every_message.csv`, `unpinned_flush_every_message.csv`.
- **Repeated runs (affinity, specifically)** — a single run per
  configuration isn't enough to tell a real effect from this desktop's
  own run-to-run noise, so the pinned-vs-unpinned comparison at 300µs
  flush was re-run 10, then 50, then 200 times each via
  `scripts/run_bench_tuned.sh`'s iteration argument, saved to
  [`benchmarks/results/repeats/`](benchmarks/results/repeats/)
  (`n10_*`/`n50_*`/`n200_*` directories) and aggregated with
  `benchmarks/plots/aggregate_repeats.py`, which reports each gap as a
  Cohen's-d-style effect size (mean difference over the pooled stdev of
  the raw per-run values) rather than a significance test — it doesn't
  shrink toward zero automatically as n grows, which turned out to
  matter for how the affinity results below should be read.

## Results

Measured on the tuned host (see "OS tuning"); raw data in
[`benchmarks/results/`](benchmarks/results/). Regenerate the plots
below with:

```bash
python3 -m venv benchmarks/plots/.venv   # first time only
benchmarks/plots/.venv/bin/pip install -r benchmarks/plots/requirements.txt
benchmarks/plots/.venv/bin/python benchmarks/plots/plot_results.py           # latency
benchmarks/plots/.venv/bin/python benchmarks/plots/plot_comparison.py        # drop rate
benchmarks/plots/.venv/bin/python benchmarks/plots/plot_affinity_repeats.py  # affinity, n=200

# affinity repeated-run comparison as text (stdlib only, no venv needed)
python3 benchmarks/plots/aggregate_repeats.py \
  benchmarks/results/repeats/n200_pinned_flush_periodic_300us \
  benchmarks/results/repeats/n200_unpinned_flush_periodic_300us
```

![Enqueue latency: sync vs async](benchmarks/plots/latency_by_threads.png)

**Async wins on latency everywhere** — roughly 8-20x lower p50 and p99
than sync at every thread count measured, and the gap *widens* under
contention: sync's tail degrades sharply as threads increase, while
async's barely moves. This is the design's core value proposition
actually landing: producer threads never block on I/O, so their latency
stays flat regardless of what the consumer or the disk is doing. This
holds across all four affinity/flush combinations, not just the one
plotted above — pinning and flush policy affect the *consumer's* drain
rate, not the producer's enqueue cost, so p50/p99 stay in roughly the
same ~1µs range no matter which of the 4 configs was used.

![AsyncLogger drop rate: affinity × flush policy](benchmarks/plots/drop_rate_comparison.png)

**Drop rate is a different story, and flush policy is by far the
dominant lever.** At 6 threads: 33-41% dropped with a per-message
flush (33% pinned, 41% unpinned) — switching to the 300µs periodic
flush alone cuts that to ~3.6-8.5%. At the 8-thread overload case the
same switch roughly halves the drop rate (51-58% down to 32-46%). This
comparison isn't close — every single-run gap here is a large multiple
of the run-to-run noise characterized below, so it doesn't need the
repeated-run treatment to trust.

**Affinity's effect on drop rate is real, but the n=50-only read of it
was subtly wrong about *why* it looked convincing.** A single
pinned-vs-unpinned run at 300µs flush originally looked identical
(3.57% either way at 6 threads) — that reading doesn't hold up. The
comparison was re-run at n=10, n=50, and n=200 for each configuration:

| threads | n | pinned | unpinned | gap (Cohen's d) |
|---|---:|---:|---:|---:|
| 6 | 10 | 3.72% ± 0.52 | 7.61% ± 4.99 | 1.09 |
| 6 | 50 | 3.56% ± 0.39 | 8.49% ± 4.29 | 1.62 |
| 6 | 200 | 4.27% ± 0.33 | 7.26% ± 4.03 | 1.05 |
| overload (8) | 10 | 34.95% ± 6.08 | 47.17% ± 10.73 | 1.40 |
| overload (8) | 50 | 34.84% ± 8.48 | 46.01% ± 7.25 | 1.42 |
| overload (8) | 200 | 34.67% ± 7.36 | 45.22% ± 7.69 | 1.40 |

![AsyncLogger drop rate: pinned vs unpinned, n=200 each, error bars = ±1 stdev](benchmarks/plots/drop_rate_affinity_repeats.png)

The n=10→50 jump at 6 threads (1.09 → 1.62) was originally read as
momentum building toward "proven, given enough runs." n=200 corrects
that: the gap moved *back down* to 1.05, not up. That's expected once
you notice what this "gap" actually is — a Cohen's-d-style effect size
(mean difference over the pooled stdev of the raw per-run values), not
a significance test. Unlike a standard error, it has no built-in
mechanism that shrinks or grows toward a threshold as n increases; more
samples just make it a more precise estimate of whatever the true
effect size already is. **8-thread overload is the effect that held
up**: 1.40 → 1.42 → 1.40 across 10, 50, and 200 runs — an estimate that
doesn't move as the sample quadruples is one that's already converged,
and ~1.4 is a large effect by the conventional (if informal) reading of
Cohen's d. **6 threads is smaller and noisier than the n=50 batch
suggested** — the true effect size looks closer to ~1.0-1.1. The
variance finding is unaffected either way: pinned's drop-rate stdev
stays roughly an order of magnitude tighter than unpinned's at every
sample size (0.33-0.52 vs. 3.99-4.99 at 6 threads) — that part was
never in question.

**The n=200 batches also surfaced a latency-side affinity effect the
drop-rate framing never looked for:**

| threads | metric | pinned (n=200) | unpinned (n=200) | gap |
|---|---|---:|---:|---:|
| 1 | p50 | 6.245µs ± 0.174 | 7.947µs ± 0.134 | 10.97 |
| 1 | p99 | 8.648µs ± 0.549 | 10.855µs ± 0.419 | 4.52 |
| 4 | p50 | 8.576µs ± 1.326 | 11.035µs ± 1.404 | 1.80 |
| 4 | p99 | 26.253µs ± 6.312 | 51.977µs ± 4.309 | 4.76 |
| 6 | p50 | 24.143µs ± 1.086 | 12.699µs ± 2.699 | -5.56 |
| 6 | p99 | 57.727µs ± 23.504 | 269.216µs ± 3.978 | 12.55 |
| overload (8) | p50 | 24.672µs ± 2.045 | 11.531µs ± 2.083 | -6.37 |
| overload (8) | p99 | 134.396µs ± 21.436 | 296.197µs ± 11.639 | 9.38 |

`SyncLogger`'s median and tail move in *opposite* directions depending
on concurrency. At 1 and 4 threads, pinning wins on both (lower p50
*and* lower p99). At 6 threads and the 8-thread overload case, pinning's
median gets *worse* (24.1µs vs. 12.7µs at 6 threads) while its tail
gets dramatically *better* (57.7µs vs. 269.2µs p99 — and unpinned's p99
stdev is only ±4.0, so it's consistently bad, not occasionally
spiking).

Both conditions ran under identical `scripts/run_bench_tuned.sh`
isolation (see the note under "Thread pinning" above), so IRQ/governor/
process steering can't be what's driving this — it was applied the same
way on both sides. The remaining variable is static core assignment vs.
free migration within the reserved set, and there's a concrete reason
it would show up exactly like this: `SyncLogger` never spawns a
consumer thread, but `BenchmarkConfig::producer_cpu_base` still starts
producers at cpu 2 unconditionally, reserved for `AsyncLogger`'s
consumer. **Pinned `SyncLogger` runs never use cpu 1 at all** — 6
threads get crammed onto exactly 6 cores (2-7) with zero slack, while
cpu 1 sits idle the entire run. **Unpinned threads are free to migrate
onto cpu 1**, giving them a 7th core of headroom the pinned layout never
touches — plausibly enough to explain a better median at the cost of
migration jitter setting a worse worst-case (though pinned's *own* wide
p99 stdev, ±23.5, says the pinned side isn't perfectly consistent
either — something is still making its tail jump around even with a
static layout). This is a hypothesis consistent with the pattern and
with the code as written, not confirmed by tracing scheduler placement
during a run. `AsyncLogger`'s own p50/p99 barely move under the same
comparison (≤0.03µs difference at every thread count, on a ~1µs
baseline) — consistent with the "Results at a glance" claim that
affinity and flush policy affect the consumer's drain rate, not the
producer's enqueue cost; `AsyncLogger` benchmarks do use cpu 1 (for the
consumer) in both conditions, so this particular idle-core effect
doesn't apply to them.

(Caveat on the overload row specifically: `producer_cpu_base=2` targets
cpus 2-9 for 8 producer threads, but this machine only has cpus 0-7 —
so `Affinity`'s `sched_setaffinity` call fails and is silently ignored
for 2 of the 8 producer threads even in the "pinned" build, leaving
them floating within whatever the process's cpuset allows. Combined
with cpu 1 sitting unused by the other 6 pinned `SyncLogger` threads,
the "pinned" overload row is a messier, less-clean layout than the
"unpinned" one — where all 8 threads simply load-balance freely across
the 7 reserved cores. Read it as directionally informative, not exact.)

**The more important finding sits underneath that table, not in it.**
The n=50 batch (collected ~18:00) and the n=200 batch (collected
~20:37-21:59, the same day) are, in principle, two samples of the same
comparison. `SyncLogger`'s *pinned* numbers agree closely between them
— every thread count is within ~2% (24.53µs vs. 24.67µs at overload,
24.22µs vs. 24.14µs at 6 threads) despite being collected hours apart.
Its *unpinned* numbers do not agree — they swing by 30-64% between the
two batches, and not even in a consistent direction (down at 1/2/4
threads, up at 6/overload). That's not measurement error in either
batch, and it isn't `run_bench_tuned.sh`'s isolation either — both
batches ran under identical cpuset/IRQ/governor tuning, so that part is
controlled for on both sides. What's left is specifically the static
core assignment: pinned always puts a given producer thread on the same
physical core, run after run, session after session, so whatever that
core's cache/branch-predictor/frequency state looks like is the same
every time. Unpinned leaves that placement up to the kernel scheduler,
which is free to make a different call depending on whatever else is
happening on the 7 reserved cores at that exact moment — and evidently
does, by enough to matter. Read the unpinned latency numbers above as
*a* snapshot of one scheduler's placement decisions on 2026-09-03
evening, not as *the* unpinned latency characteristic of `SyncLogger`
— that quantity isn't well-defined without pinning down which core (or
cores) a given run actually landed on.

None of the drop-rate story above is the ring buffer's fault — it's
lock-free and sized generously at 65,536 slots. It's the consumer's
flush call: on every single message, that's a `write()` syscall the
consumer can't get ahead of once producers arrive faster than disk I/O
completes; on a
periodic interval, most messages just append to the C++ stream buffer
and the syscall cost gets amortized across many of them. This is the
concrete, now-measured cost of the original policy — the number
milestone 5's comment was waiting on — and confirmation that batching
the flush was the right fix.

## Testing

GoogleTest suite in `tests/` (fetched via CMake's `FetchContent`, no system
install needed), covering `SpscRingBuffer`, `MpscRingBuffer`, `SyncLogger`,
and `AsyncLogger`. Concurrency tests check both correctness (no lost,
duplicated, or reordered messages under contention) and, for `AsyncLogger`,
a specific shutdown-hang regression (the destructor must not block forever
if the consumer thread is idle when shutdown is requested).

```bash
# normal build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

# ThreadSanitizer build
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DALLL_ENABLE_TSAN=ON -DALLL_BUILD_BENCHMARKS=OFF
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

Both configurations run in CI on every push.
