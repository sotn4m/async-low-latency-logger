#!/usr/bin/env python3
"""Aggregate a repeated-run batch (or compare two) from run_bench_tuned.sh's
iteration mode, to tell a real effect apart from run-to-run noise.

Usage:
  python3 benchmarks/plots/aggregate_repeats.py <repeats_dir> [<repeats_dir_2>]

<repeats_dir> is one of the timestamped directories under
benchmarks/results/repeats/, e.g.:

  python3 benchmarks/plots/aggregate_repeats.py \
      benchmarks/results/repeats/20260903T151344Z

Pass a second directory to compare two batches (e.g. pinned vs unpinned)
side by side, with the gap between their means expressed in units of
the pooled standard deviation — a gap under ~1-2 is not distinguishable
from noise at this sample size.

Stdlib only, no venv needed.
"""

import csv
import statistics
import sys
from pathlib import Path

METRICS = ["p50_us", "p99_us", "dropped_rate"]


def load_batch (repeats_dir):
  """Returns {(logger, threads): {metric: [values across runs]}}."""
  runs = sorted (Path (repeats_dir).glob ("run_*.csv"))
  if not runs:
    print (f"no run_*.csv files found in {repeats_dir}", file=sys.stderr)
    sys.exit (1)

  series = {}
  for run in runs:
    with open (run, newline="") as f:
      for row in csv.DictReader (f):
        key = (row["logger"], row["threads"])
        bucket = series.setdefault (key, {m: [] for m in METRICS})
        for metric in METRICS:
          value = row.get (metric, "")
          if value != "":
            bucket[metric].append (float (value))

  return series, len (runs)


def mean_stdev (values):
  if not values:
    return None
  if len (values) == 1:
    return values[0], 0.0
  return statistics.mean (values), statistics.stdev (values)


def fmt (values):
  stats = mean_stdev (values)
  if stats is None:
    return "-"
  mean, stdev = stats
  return f"{mean:.3f}±{stdev:.3f}"


def print_single (series, n):
  print (f"n={n} runs\n")
  header = f"{'logger':16}{'threads':8}" + "".join (f"{m:20}" for m in METRICS)
  print (header)
  print ("-" * len (header))
  for (logger, threads), bucket in series.items ():
    row = f"{logger:16}{threads:8}"
    row += "".join (f"{fmt (bucket[m]):20}" for m in METRICS)
    print (row)


def print_comparison (series_a, n_a, series_b, n_b):
  print (f"A: n={n_a} runs   B: n={n_b} runs")
  print ("gap is in units of the pooled stdev — under ~1-2 isn't")
  print ("distinguishable from run-to-run noise at this sample size\n")

  keys = [k for k in series_a if k in series_b]
  for metric in METRICS:
    print (f"=== {metric} ===")
    header = f"{'logger':16}{'threads':8}{'A':18}{'B':18}{'gap (σ)':10}"
    print (header)
    print ("-" * len (header))
    for logger, threads in keys:
      a = series_a[(logger, threads)][metric]
      b = series_b[(logger, threads)][metric]
      a_stats = mean_stdev (a)
      b_stats = mean_stdev (b)
      if a_stats is None or b_stats is None:
        continue
      a_mean, a_std = a_stats
      b_mean, b_std = b_stats
      pooled = ((a_std ** 2 + b_std ** 2) / 2) ** 0.5
      gap = f"{(b_mean - a_mean) / pooled:.2f}" if pooled > 0 else "n/a"
      print (f"{logger:16}{threads:8}{fmt (a):18}{fmt (b):18}{gap:10}")
    print ()


def main ():
  if len (sys.argv) not in (2, 3):
    print (__doc__)
    sys.exit (1)

  series_a, n_a = load_batch (sys.argv[1])

  if len (sys.argv) == 2:
    print_single (series_a, n_a)
  else:
    series_b, n_b = load_batch (sys.argv[2])
    print_comparison (series_a, n_a, series_b, n_b)


if __name__ == "__main__":
  main ()
