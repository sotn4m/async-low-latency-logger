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
- [ ] Milestone 4 — overflow policy (drop-on-full + counter)
- [ ] Milestone 5 — consumer thread: batched drain, deferred formatting, buffered write
- [ ] Milestone 6 — correctness under concurrency (ThreadSanitizer stress tests)
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

_TBD — filled in at milestone 6: ThreadSanitizer-verified stress tests for
the lock-free ring buffer._
