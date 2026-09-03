#!/usr/bin/env python3
"""Generate the sync-vs-async latency plot from a bench_results CSV.

Drop rate is plotted separately by plot_comparison.py, across all
affinity/flush-policy result files at once — a single-file drop-rate
plot here would just be a strict subset of that, so this script only
covers latency.

Usage:
  benchmarks/plots/.venv/bin/python benchmarks/plots/plot_results.py \
      [path/to/results.csv] [output_dir]

Defaults to benchmarks/results/pinned_flush_periodic_300us.csv and this
script's own directory.
"""

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use ("Agg")
import matplotlib.pyplot as plt

SCRIPT_DIR = Path (__file__).resolve ().parent
REPO_ROOT = SCRIPT_DIR.parent.parent

# (logger, overload_logger, marker, color) — sync has no overload_logger
# equivalent worth separating visually the same way, but both loggers do
# have an "_overload" row in the CSV that's excluded from the clean
# sweep's trend line and shown as a separate marker instead.
SERIES = [
  ("sync", "sync_overload", "o", "tab:red"),
  ("async", "async_overload", "s", "tab:blue"),
]


def load_rows (csv_path):
  with open (csv_path, newline="") as f:
    return list (csv.DictReader (f))


def plot_latency (rows, output_dir):
  fig, ax = plt.subplots (figsize=(7, 5))

  for logger, overload_logger, marker, color in SERIES:
    clean = sorted (
      (r for r in rows if r["logger"] == logger),
      key=lambda r: int (r["threads"]),
    )
    threads = [int (r["threads"]) for r in clean]
    p50 = [float (r["p50_us"]) for r in clean]
    p99 = [float (r["p99_us"]) for r in clean]

    ax.plot (threads, p50, marker=marker, color=color, linestyle="-",
            label=f"{logger} p50")
    ax.plot (threads, p99, marker=marker, color=color, linestyle="--",
            label=f"{logger} p99")

    overload = next ((r for r in rows if r["logger"] == overload_logger), None)
    if overload:
      t = int (overload["threads"])
      for col in ("p50_us", "p99_us"):
        ax.scatter ([t], [float (overload[col])], marker="*", s=180,
                   color=color, edgecolor="black", zorder=5)

  ax.set_yscale ("log")
  ax.set_xlabel ("producer threads")
  ax.set_ylabel ("latency (µs, log scale)")
  ax.set_title (
    "Enqueue latency: sync vs async\n"
    "(★ = 8-thread overload case, not part of the clean sweep)"
  )
  ax.legend ()
  ax.grid (True, which="both", alpha=0.3)

  out = output_dir / "latency_by_threads.png"
  fig.savefig (out, dpi=150, bbox_inches="tight")
  plt.close (fig)
  return out


DEFAULT_CSV = REPO_ROOT / "benchmarks" / "results" / "pinned_flush_periodic_300us.csv"


def main ():
  csv_path = Path (sys.argv[1]) if len (sys.argv) > 1 else DEFAULT_CSV
  output_dir = Path (sys.argv[2]) if len (sys.argv) > 2 else SCRIPT_DIR

  rows = load_rows (csv_path)
  print (f"wrote {plot_latency (rows, output_dir)}")


if __name__ == "__main__":
  main ()
