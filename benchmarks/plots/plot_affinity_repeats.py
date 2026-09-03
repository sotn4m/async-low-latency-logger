#!/usr/bin/env python3
"""Bar chart with error bars (mean ± stdev) for the pinned-vs-unpinned
drop-rate comparison, from the n=50 repeated-run batches — the visual
counterpart to aggregate_repeats.py's text table.

Usage:
  benchmarks/plots/.venv/bin/python benchmarks/plots/plot_affinity_repeats.py \
      [pinned_dir] [unpinned_dir] [output_dir]

Defaults to benchmarks/results/repeats/n50_pinned_flush_periodic_300us
and .../n50_unpinned_flush_periodic_300us.
"""

import csv
import statistics
import sys
from pathlib import Path

import matplotlib

matplotlib.use ("Agg")
import matplotlib.pyplot as plt

SCRIPT_DIR = Path (__file__).resolve ().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
REPEATS_DIR = REPO_ROOT / "benchmarks" / "results" / "repeats"
DEFAULT_PINNED = REPEATS_DIR / "n50_pinned_flush_periodic_300us"
DEFAULT_UNPINNED = REPEATS_DIR / "n50_unpinned_flush_periodic_300us"

# (threads, logger, x-axis label)
POINTS = [
  ("6", "async", "6 threads"),
  ("8", "async_overload", "8 (overload)"),
]


def load_batch (repeats_dir):
  runs = sorted (Path (repeats_dir).glob ("run_*.csv"))
  if not runs:
    print (f"no run_*.csv files found in {repeats_dir}", file=sys.stderr)
    sys.exit (1)

  values = {}
  for run in runs:
    with open (run, newline="") as f:
      for row in csv.DictReader (f):
        if not row["dropped_rate"]:
          continue
        key = (row["logger"], row["threads"])
        values.setdefault (key, []).append (float (row["dropped_rate"]))

  return values, len (runs)


def main ():
  pinned_dir = Path (sys.argv[1]) if len (sys.argv) > 1 else DEFAULT_PINNED
  unpinned_dir = Path (sys.argv[2]) if len (sys.argv) > 2 else DEFAULT_UNPINNED
  output_dir = Path (sys.argv[3]) if len (sys.argv) > 3 else SCRIPT_DIR

  pinned, n_pinned = load_batch (pinned_dir)
  unpinned, n_unpinned = load_batch (unpinned_dir)

  labels = [label for _, _, label in POINTS]
  pinned_means, pinned_errs, unpinned_means, unpinned_errs = [], [], [], []

  for threads, logger, _ in POINTS:
    p_values = pinned[(logger, threads)]
    u_values = unpinned[(logger, threads)]
    pinned_means.append (statistics.mean (p_values))
    pinned_errs.append (statistics.stdev (p_values))
    unpinned_means.append (statistics.mean (u_values))
    unpinned_errs.append (statistics.stdev (u_values))

  fig, ax = plt.subplots (figsize=(7, 5))
  x = list (range (len (labels)))
  width = 0.35

  ax.bar ([xi - width / 2 for xi in x], pinned_means, width,
         yerr=pinned_errs, capsize=5, color="tab:green",
         label=f"pinned (n={n_pinned})")
  ax.bar ([xi + width / 2 for xi in x], unpinned_means, width,
         yerr=unpinned_errs, capsize=5, color="tab:orange",
         label=f"unpinned (n={n_unpinned})")

  ax.set_xticks (x)
  ax.set_xticklabels (labels)
  ax.set_xlabel ("producer threads")
  ax.set_ylabel ("dropped messages (%)")
  ax.set_title (
    "AsyncLogger drop rate: pinned vs unpinned (300µs flush)\n"
    "error bars = ±1 stdev across repeated runs"
  )
  ax.legend ()
  ax.grid (True, axis="y", alpha=0.3)

  out = output_dir / "drop_rate_affinity_repeats.png"
  fig.savefig (out, dpi=150, bbox_inches="tight")
  plt.close (fig)
  print (f"wrote {out}")


if __name__ == "__main__":
  main ()
