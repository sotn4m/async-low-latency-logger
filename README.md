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
- [ ] Milestone 7 — benchmark harness: open-loop load, latency percentiles, sync vs async
- [ ] Milestone 8 — OS tuning (CPU isolation, IRQ affinity) on the benchmark host
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

_TBD — filled in at milestone 8: `isolcpus`/`nohz_full` CPU isolation,
IRQ affinity, CPU governor, and the exact commands used, once the
benchmark scripts land in `scripts/`._

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

# ThreadSanitizer build — this is the one that actually proves "no data
# race", not just "values came out right"
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DALLL_ENABLE_TSAN=ON -DALLL_BUILD_BENCHMARKS=OFF
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

Both configurations run in CI on every push.
