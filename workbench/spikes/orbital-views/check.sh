#!/bin/bash
set -euo pipefail
view_output=${SIXDB_RESULTS:-build/spikes/orbital-views/composition-results}
mkdir -p "$view_output"
cmake -S . -B build/spikes/orbital-views -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=orbital-views -DSIXDB_MARCH="${SIXDB_MARCH:-x86-64-v4}" \
  -DSIXDB_TUNE="${SIXDB_TUNE:-zen5}" > "$view_output/configure.log" 2>&1
view_probes=(orbital_views_probe orbital_views_recycling_probe \
  orbital_views_lifecycle_probe orbital_views_acquisition_probe orbital_views_embedded_probe \
  orbital_views_ikea_probe orbital_views_embedded_acquisition_probe)
cmake --build build/spikes/orbital-views --target "${view_probes[@]}" \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$view_output/build.log" 2>&1
view_bin=build/spikes/orbital-views/workbench/spikes/orbital-views
for probe in "${view_probes[@]}"; do
  "$view_bin/$probe" > "$view_output/$probe.jsonl"
done
view_systems=build/spikes/orbital-views/systems
cmake -S workbench/spikes/orbital-views/systems -B "$view_systems" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_MARCH="${SIXDB_MARCH:-x86-64-v4}" -DSIXDB_TUNE="${SIXDB_TUNE:-zen5}" \
  > "$view_output/systems-configure.log" 2>&1
cmake --build "$view_systems" --target composition_probe unit_boundary intent_probe binding_probe \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$view_output/systems-build.log" 2>&1
for probe in composition_probe unit_boundary intent_probe binding_probe; do
  "$view_systems/$probe" > "$view_output/$probe.txt"
done
clang++-21 --version > "$view_output/compiler.txt"
uname -a > "$view_output/kernel.txt"
