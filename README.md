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
  (`kConsumerCpu`, `kProducerCpuBase`) that matches this script's
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

_TBD — filled in at milestone 7. Will cover: what's measured (producer-side
enqueue latency only), the open-loop load generator used to avoid
coordinated omission, thread-count/load sweeps, and the tools used to
compute latency percentiles._

## Results

_TBD — filled in at milestone 9: sync vs async latency percentiles under
load, throughput and drop-rate scaling by producer thread count, and a
tuned-OS vs untuned-OS comparison._

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
