#!/usr/bin/env bash
#
# Runtime-only CPU tuning for a single benchmark run on a daily-driver
# desktop (no isolcpus/nohz_full, no reboot). Reserves cores 1-N for the
# benchmark, leaves core 0 for the OS, steers movable IRQs and other
# cgroup-managed processes off the reserved cores, sets the "performance"
# governor on just the reserved cores, and resets everything to known
# defaults on exit (including on failure or Ctrl-C). No snapshotting —
# this always runs against the same known starting state, so cleanup
# just writes back defaults rather than replaying a saved one.
#
# The benchmark itself is launched via `systemd-run --scope` with
# `AllowedCPUs=` rather than a hand-rolled cgroup + `cgroup.procs` write:
# systemd places the process into the right cpuset atomically at launch,
# with no PID-race (sudo may fork a monitor process, so the PID bash
# captures via `$!` for a backgrounded `sudo -u ... cmd &` is not
# guaranteed to be the actual target process) and no separate cgroup
# lifecycle for us to create/track/remove.
#
# If this script is killed with SIGKILL (or the host crashes) mid-run,
# the trap below never runs and tuning is left applied — recover by
# hand with the same commands cleanup() below runs: powersave governor
# on every core, full IRQ affinity, and user.slice/system.slice cpuset
# reset to the full core range.
#
# Usage: sudo scripts/run_bench_tuned.sh [bench_binary] [reserved_cpus] [system_cpu]
#   bench_binary   default: build/bench_logger
#   reserved_cpus  default: 1-7   (cpupower/taskset CPU list syntax)
#   system_cpu     default: 0

set -euo pipefail

BENCH_BINARY="${1:-build/bench_logger}"
RESERVED_CPUS="${2:-1-7}"
SYSTEM_CPU="${3:-0}"

if [[ "$EUID" -ne 0 ]]; then
  echo "must be run as root (sudo) — governor/IRQ/cgroup writes require it" >&2
  exit 1
fi

if [[ -z "${SUDO_USER:-}" ]]; then
  echo "must be run via sudo (not as root directly) so output files are owned by you" >&2
  exit 1
fi

if [[ ! -x "$BENCH_BINARY" ]]; then
  echo "benchmark binary not found or not executable: $BENCH_BINARY" >&2
  exit 1
fi

NPROC_VAL="$(nproc)"
FULL_RANGE="0-$((NPROC_VAL - 1))"

log () { echo "[run_bench_tuned] $*"; }

cleanup () {
  local status=$?
  log "restoring defaults (exit status $status)"

  for gov_file in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [[ -w "$gov_file" ]] && echo powersave > "$gov_file" 2>/dev/null || true
  done

  for irq_dir in /proc/irq/*/; do
    local num="${irq_dir%/}"
    num="${num##*/}"
    [[ "$num" =~ ^[0-9]+$ ]] || continue
    local affinity_file="/proc/irq/${num}/smp_affinity_list"
    [[ -w "$affinity_file" ]] && echo "$FULL_RANGE" > "$affinity_file" 2>/dev/null || true
  done

  for slice in user.slice system.slice; do
    local cpuset_file="/sys/fs/cgroup/${slice}/cpuset.cpus"
    [[ -w "$cpuset_file" ]] && echo "$FULL_RANGE" > "$cpuset_file" 2>/dev/null || true
  done

  log "done"
  exit "$status"
}

trap cleanup EXIT INT TERM

# Steer other processes off the reserved cores. Best-effort: only touch
# a slice's cpuset if the cpuset controller is actually delegated to it —
# forcing delegation on a live session is out of scope here.
if grep -qw cpuset /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null; then
  for slice in user.slice system.slice; do
    cpuset_file="/sys/fs/cgroup/${slice}/cpuset.cpus"
    if [[ -w "$cpuset_file" ]]; then
      echo "$SYSTEM_CPU" > "$cpuset_file" 2>/dev/null \
        && log "confined $slice to cpu $SYSTEM_CPU" \
        || log "could not confine $slice (continuing without it)"
    fi
  done
else
  log "cpuset controller not delegated at cgroup root — skipping process steering"
fi

# Steer movable IRQs off the reserved cores. Kernel-managed (MSI-X)
# IRQs will reject the write — expected, not an error.
for irq_dir in /proc/irq/*/; do
  irq="${irq_dir%/}"
  num="${irq##*/}"
  [[ "$num" =~ ^[0-9]+$ ]] || continue
  affinity_file="${irq}/smp_affinity_list"
  [[ -w "$affinity_file" ]] || continue
  echo "$SYSTEM_CPU" > "$affinity_file" 2>/dev/null || true
done
log "steered movable IRQs to cpu $SYSTEM_CPU"

# Performance governor on reserved cores only; core 0 (and any core not
# reserved) is left untouched.
if command -v cpupower >/dev/null 2>&1; then
  cpupower -c "$RESERVED_CPUS" frequency-set -g performance >/dev/null
  log "set performance governor on cpus $RESERVED_CPUS"
else
  log "cpupower not found — governor unchanged"
fi

# bench_main.cpp pins its own threads to a fixed core layout (consumer
# on cpu 1, producers starting at cpu 2) matching this script's default
# reservation — see BenchmarkConfig::consumer_cpu/producer_cpu_base
# there. Nothing needs to be communicated to the process at launch
# beyond which cpuset it's allowed to run on.
if command -v systemd-run >/dev/null 2>&1; then
  log "launching $BENCH_BINARY via systemd-run scope (uid=$SUDO_USER, AllowedCPUs=$RESERVED_CPUS)"
  systemd-run --scope --quiet --same-dir \
    --uid="$SUDO_USER" \
    --slice=bench.slice \
    -p AllowedCPUs="$RESERVED_CPUS" \
    -- "$BENCH_BINARY"
else
  log "systemd-run not found — falling back to sudo without cgroup cpuset confinement on the benchmark process itself"
  sudo -u "$SUDO_USER" "$BENCH_BINARY"
fi
