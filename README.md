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
      (Flush-per-message was the original policy, flagged in code as a
      follow-up pending numbers on what it cost. The benchmarks below
      supplied those numbers — see "Results" — and the consumer now
      flushes on a 300µs interval instead, `kFlushInterval` in
      `src/logger/async_logger.cpp`.)
- [x] Milestone 6 — correctness under concurrency (ThreadSanitizer stress tests)
- [x] Milestone 7 — benchmark harness: open-loop load, latency percentiles, sync vs async
- [x] Milestone 8 — OS tuning (CPU isolation, IRQ affinity) on the benchmark host
- [x] Milestone 9 — run benchmarks, collect results, plots
- [ ] Milestone 10 — re-run the full benchmark suite (thread sweep, overload, affinity ×
      flush policy, n=100 repeats) against the fixed code and refill in Results. No
      dedicated before/after micro-benchmark of the move fix itself — the full-suite
      re-run is the only measurement planned for it.
- [ ] Milestone 10a — report the overload case's actual per-thread pinning
      state (see "Benchmark methodology") instead of assuming uniform
      pinning, alongside Milestone 10's re-run.
- [ ] Milestone 11 — this README's Hardware / OS Tuning / Results sections filled in
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


