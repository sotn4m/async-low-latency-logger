# async-low-latency-logger

A ring-buffer-backed async logger for C++: application threads never block
on logging, no matter how slow the underlying I/O is. Built to measure and
document, precisely, how much latency that buys back versus a naive
synchronous logger under load.

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
- [ ] Milestone 9 — run benchmarks, collect results, plots
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
reversible per run:

- **Core layout.** Core 0 is left for the OS/desktop session. Cores 1-7
  are reserved for the benchmark: core 1 for `AsyncLogger`'s consumer
  thread, cores 2-7 for up to 6 producer threads (one core each).
- **Process steering.** `user.slice`/`system.slice`'s cgroup v2
  `cpuset.cpus` is temporarily confined to core 0 (best-effort — skipped
  if the `cpuset` controller isn't delegated), so other processes don't
  land on the reserved cores.
- **IRQ affinity.** Movable IRQs (`/proc/irq/*/smp_affinity_list`) are
  redirected to core 0. Kernel-managed MSI-X IRQs reject the write and
  are left alone.
- **CPU governor.** Set to `performance` on cores 1-7 only (via
  `cpupower`); core 0 stays on the system default.
- **Thread pinning.** `AsyncLogger`'s consumer thread and each
  benchmark producer thread self-pin via `sched_setaffinity`
  (`alll::Affinity` in `include/logger/async_logger.hpp`), targeting a
  fixed core layout hardcoded in `benchmarks/bench_main.cpp`
  (`BenchmarkConfig::consumer_cpu`, `BenchmarkConfig::producer_cpu_base`)
  that matches this script's
  default reservation. Unlike the rest of the tuning, this isn't
  conditional on running under the script — `bench_logger` always
  attempts it. Running it directly (as CI does) is still harmless:
  pinning to a CPU that exists but isn't specially reserved just
  succeeds trivially, and pinning to one that doesn't exist just fails
  and is silently ignored.

Run it with:

```bash
sudo scripts/run_bench_tuned.sh
```

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
  independent knobs were each run with and without: whether producer
  and consumer threads are pinned (see "OS tuning"/"Thread pinning"
  above), and how often `AsyncLogger`'s consumer flushes to disk —
  after *every* message (the original policy, milestone 5's TODO) vs.
  a fixed 300µs interval (`kFlushInterval` in `src/logger/async_logger.cpp`
  — chosen over a shorter 50µs interval also tried during development,
  which measured worse). Each of the 4 combinations is its own CSV in
  [`benchmarks/results/`](benchmarks/results/):
  `pinned_flush_periodic_300us.csv`, `unpinned_flush_periodic_300us.csv`,
  `pinned_flush_every_message.csv`, `unpinned_flush_every_message.csv`.

## Results

Measured on the tuned host (see "OS tuning"); raw data in
[`benchmarks/results/`](benchmarks/results/). Regenerate the plots
below with:

```bash
python3 -m venv benchmarks/plots/.venv   # first time only
benchmarks/plots/.venv/bin/pip install -r benchmarks/plots/requirements.txt
benchmarks/plots/.venv/bin/python benchmarks/plots/plot_results.py      # latency
benchmarks/plots/.venv/bin/python benchmarks/plots/plot_comparison.py   # drop rate
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
flush alone cuts that to ~3.6%, *regardless of affinity* (3.57% pinned,
3.57% unpinned — no measurable difference). At the 8-thread overload
case the same switch roughly halves the drop rate (51-58% down to
32-34%).

Affinity's own effect turns out to be **conditional on the flush
policy**, not independent of it: with the every-message flush still
bottlenecking the consumer, pinning meaningfully helps — about 7
points lower drop rate at both 6 threads and overload. But once the
300µs flush removes that bottleneck, affinity has little left to fix:
the gap shrinks to nothing at 6 threads and to ~2 points at overload.
In other words, CPU affinity mostly matters when something else is
already saturating the consumer thread — it isn't a substitute for
fixing that something else.

None of this is the ring buffer's fault — it's lock-free and sized
generously at 65,536 slots. It's the consumer's flush call: on every
single message, that's a `write()` syscall the consumer can't get
ahead of once producers arrive faster than disk I/O completes; on a
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
