#!/usr/bin/env python3
"""Grouped drop-rate comparison across affinity/flush-policy result files.

Usage:
  benchmarks/plots/.venv/bin/python benchmarks/plots/plot_comparison.py \
      [results_dir] [output_dir]

Defaults to benchmarks/results/ and benchmarks/plots/.
"""

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use ("Agg")
import matplotlib.pyplot as plt

SCRIPT_DIR = Path (__file__).resolve ().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_RESULTS_DIR = REPO_ROOT / "benchmarks" / "results"

# (filename, label, color) — order controls bar-group order.
CONFIGS = [
  ("pinned_flush_periodic_300us.csv", "pinned + 300µs periodic flush", "tab:green"),
  ("unpinned_flush_periodic_300us.csv", "unpinned + 300µs periodic flush", "tab:orange"),
  ("pinned_flush_every_message.csv", "pinned + flush every msg", "tab:blue"),
  ("unpinned_flush_every_message.csv", "unpinned + flush every msg", "tab:red"),
]

THREAD_LABELS = ["1", "2", "4", "6", "8 (overload)"]
THREAD_LOGGERS = ["async", "async", "async", "async", "async_overload"]
THREAD_COUNTS = ["1", "2", "4", "6", "8"]


def load_rows (csv_path):
  with open (csv_path, newline="") as f:
    return list (csv.DictReader (f))


def drop_rates_by_thread_label (rows):
  rates = {}
  for label, logger, threads in zip (THREAD_LABELS, THREAD_LOGGERS, THREAD_COUNTS):
    row = next (
      (r for r in rows if r["logger"] == logger and r["threads"] == threads),
      None,
    )
    rates[label] = float (row["dropped_rate"]) if row and row["dropped_rate"] else 0.0
  return rates


def main ():
  results_dir = Path (sys.argv[1]) if len (sys.argv) > 1 else DEFAULT_RESULTS_DIR
  output_dir = Path (sys.argv[2]) if len (sys.argv) > 2 else SCRIPT_DIR

  series = []
  for filename, label, color in CONFIGS:
    path = results_dir / filename
    if not path.exists ():
      print (f"skipping missing {path}", file=sys.stderr)
      continue
    series.append ((label, color, drop_rates_by_thread_label (load_rows (path))))

  fig, ax = plt.subplots (figsize=(8, 5))

  n = len (series)
  bar_width = 0.8 / n
  x = range (len (THREAD_LABELS))

  for i, (label, color, rates) in enumerate (series):
    offsets = [xi + (i - (n - 1) / 2) * bar_width for xi in x]
    heights = [rates[t] for t in THREAD_LABELS]
    ax.bar (offsets, heights, width=bar_width, color=color, label=label)

  ax.set_xticks (list (x))
  ax.set_xticklabels (THREAD_LABELS)
  ax.set_xlabel ("producer threads")
  ax.set_ylabel ("dropped messages (%)")
  ax.set_title ("AsyncLogger drop rate: affinity × flush policy")
  ax.legend ()
  ax.grid (True, axis="y", alpha=0.3)

  out = output_dir / "drop_rate_comparison.png"
  fig.savefig (out, dpi=150, bbox_inches="tight")
  plt.close (fig)
  print (f"wrote {out}")


if __name__ == "__main__":
  main ()
