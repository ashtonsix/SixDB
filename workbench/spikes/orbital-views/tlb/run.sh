#!/bin/bash
set -euo pipefail
tlb_output=${SIXDB_RESULTS:-build/spikes/orbital-views/tlb-results}
mkdir -p "$tlb_output"
clang++-21 -std=c++23 -O2 -Wall -Wextra -Werror -pthread \
  workbench/spikes/orbital-views/tlb/probe.cpp -o "$tlb_output/probe"
clang++-21 --version > "$tlb_output/compiler.txt"
uname -a > "$tlb_output/kernel.txt"
cat /proc/interrupts > "$tlb_output/interrupts-before.txt"
cat /proc/cpuinfo > "$tlb_output/cpuinfo.txt"
cat /proc/sys/kernel/perf_event_paranoid > "$tlb_output/perf_event_paranoid.txt"
if [[ -r /boot/config-$(uname -r) ]]; then
  python3 -c 'import re,sys; print("".join(line for line in open(sys.argv[1]) if re.search("BROADCAST_TLB|X86_INVLPGB|PARAVIRT|VM_EVENT_COUNTERS", line)), end="")' \
    /boot/config-$(uname -r) > "$tlb_output/kernel-config.txt"
fi
cat /sys/kernel/debug/x86/tlb_single_page_flush_ceiling > "$tlb_output/flush-ceiling.txt" 2>&1 || true
for trial in 1 2 3; do
  "$tlb_output/probe" > "$tlb_output/forward-$trial.csv"
  "$tlb_output/probe" --reverse > "$tlb_output/reverse-$trial.csv"
done
cat /proc/interrupts > "$tlb_output/interrupts-after.txt"
python3 workbench/spikes/orbital-views/tlb/summarize.py "$tlb_output"
