#!/bin/bash
# Kind-batched substrate probe on a worker: phase 1's real columns, one build
# per ISA, one part (65,536 rows) and the full slice, three process repeats.
# Usage: worker.py run workbench/spikes/frame-phase-2/substrate/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
inputs=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
cp "$inputs" "$SIXDB_RESULTS/input-artifact.json"
python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/in" \
  > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
cols="$SIXDB_RESULTS/in/inputs/cols"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx2 avx512}" ;;
  *) isas="${ISAS:-native}" ;;
esac
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="" ;;
  esac
  build="build/clang/frame-substrate-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' $flags \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_substrate_measure frame_substrate_select frame_substrate_points frame_substrate_masks frame_substrate_image frame_substrate_dict frame_substrate_relocate frame_substrate_geometry frame_substrate_programs frame_substrate_grain frame_substrate_lifecycle -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  bin="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_measure"
  sel="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_select"
  pts="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_points"
  msk="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_masks"
  for repeat in 1 2 3; do
    if [ -n "${LIFECYCLE_ONLY:-}" ]; then
      lif="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_lifecycle"
      taskset -c "$SIXDB_CPU" "$lif" "$cols" 2999808 "${ROUNDS:-5}" UserID:URLHash OS:RegionID EventTime:WatchID \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/lifecycle.jsonl"
      continue
    fi
    if [ -n "${GRAIN_ONLY:-}" ]; then
      grn="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_grain"
      taskset -c "$SIXDB_CPU" "$grn" "$cols" 2999808 "${ROUNDS:-21}" OS EventTime UserID URLHash \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/grain.jsonl"
      continue
    fi
    if [ -n "${PROGRAMS_ONLY:-}" ]; then
      prg="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_programs"
      taskset -c "$SIXDB_CPU" "$prg" "$cols" 2999808 "${ROUNDS:-21}" \
        OS:RegionID ResolutionWidth:OS RegionID:ClientIP EventTime:UserID UserID:OS URLHash:UserID WatchID:EventTime \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/programs.jsonl"
      continue
    fi
    if [ -n "${GEOMETRY_ONLY:-}" ]; then
      geo="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_geometry"
      taskset -c "$SIXDB_CPU" "$geo" "$cols" 2999808 "${ROUNDS:-21}" OS UserID URLHash \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/geometry.jsonl"
      continue
    fi
    if [ -n "${RELOCATE_ONLY:-}" ]; then
      rel="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_relocate"
      taskset -c "$SIXDB_CPU" "$rel" "$cols" 2999808 "${ROUNDS:-21}" OS EventTime UserID URLHash \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/relocate.jsonl"
      continue
    fi
    if [ -n "${DICT_ONLY:-}" ]; then
      dct="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_dict"
      taskset -c "$SIXDB_CPU" "$dct" "$cols" 2999808 "${ROUNDS:-21}" OS ResolutionWidth RegionID ClientIP UserID URLHash \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/dict.jsonl"
      continue
    fi
    if [ -n "${IMAGE_ONLY:-}" ]; then
      img="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_image"
      taskset -c "$SIXDB_CPU" "$img" "$cols" 2999808 "${ROUNDS:-21}" \
        OS ResolutionWidth EventTime WatchID URLHash:UserID UserID:OS ClientIP:RegionID WatchID:EventTime \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/image.jsonl"
      continue
    fi
    if [ -n "${SCALING_IMAGE:-}" ]; then
      # Concurrent streams over the shared image (front C's leaves), full SUM only.
      img="$build/workbench/spikes/frame-phase-2/substrate/frame_substrate_image"
      ncpu=$(nproc)
      for k in 1 2 4 $ncpu; do
        pids=()
        for j in $(seq 0 $((k - 1))); do
          CELLS=sum taskset -c "$j" "$img" "$cols" 2999808 "${ROUNDS:-21}" OS EventTime UserID URLHash WatchID \
            | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",\"streams\":$k,\"stream\":$j,/" > "$out/iscale-$repeat-$k-$j.jsonl" &
          pids+=($!)
        done
        wait "${pids[@]}"
        cat "$out"/iscale-$repeat-$k-*.jsonl >> "$out/image-scaling.jsonl"
        rm -f "$out"/iscale-$repeat-$k-*.jsonl
      done
      continue
    fi
    if [ -n "${SCALING_ONLY:-}" ]; then
      # Concurrent independent streams: k processes, one per core, each with
      # its own copy of the data, timing the same full-slice scans at once.
      ncpu=$(nproc)
      for k in 1 2 4 $ncpu; do
        pids=()
        for j in $(seq 0 $((k - 1))); do
          taskset -c "$j" "$bin" "$cols" 2999808 "${ROUNDS:-21}" OS EventTime UserID URLHash WatchID \
            | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",\"streams\":$k,\"stream\":$j,/" > "$out/scaling-$repeat-$k-$j.jsonl" &
          pids+=($!)
        done
        wait "${pids[@]}"
        cat "$out"/scaling-$repeat-$k-*.jsonl >> "$out/scaling.jsonl"
        rm -f "$out"/scaling-$repeat-$k-*.jsonl
      done
      continue
    fi
    if [ -n "${MASKS_ONLY:-}" ]; then
      taskset -c "$SIXDB_CPU" "$msk" "$cols" 2999808 "${ROUNDS:-41}" OS ResolutionWidth EventTime UserID URLHash WatchID \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/masks.jsonl"
      continue
    fi
    if [ -n "${POINTS_ONLY:-}" ]; then
      for rows in 65536 2999808; do
        for d in 4 8 16; do
          taskset -c "$SIXDB_CPU" "$pts" "$cols" "$rows" "${ROUNDS:-41}" "$d" OS UserID EventTime URLHash WatchID \
            | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/points.jsonl"
        done
      done
      continue
    fi
    taskset -c "$SIXDB_CPU" "$sel" "$cols" 2999808 "${ROUNDS:-41}" OS:UserID UserID:URLHash EventTime:WatchID RegionID:ClientIP \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/select.jsonl"
    [ -n "${SELECT_ONLY:-}" ] && continue
    for rows in 65536 2999808; do
      taskset -c "$SIXDB_CPU" "$bin" "$cols" "$rows" "${ROUNDS:-41}" \
        OS ResolutionWidth RegionID ConnectTiming IsRefresh CounterID EventTime UserID ClientIP URLHash WatchID \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/samples.jsonl"
    done
  done
done
echo done > "$SIXDB_RESULTS/status.txt"
