#!/bin/bash
set -euo pipefail
study=workbench/spikes/frame-phase-2/instruments
case "$(uname -m)" in
  x86_64) profiles="${ISAS:-avx2 avx512}" ;;
  *) profiles="${ISAS:-neon}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    avx2) march=x86-64-v3; tune=zen5 ;;
    avx512) march=znver5; tune=zen5 ;;
    neon) march=armv8-a+simd; tune=neoverse-v2 ;;
    *) exit 2 ;;
  esac
  build="build/clang/frame-codecs-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out/bin"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' -DSIXDB_FRAME_CODEC_CONTROLS=ON \
    -DFETCHCONTENT_BASE_DIR="$PWD/build/deps/frame-codecs" \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$out/configure.log" 2>&1
  cmake --build "$build" --target frame_turbopfor_check frame_fastlanes_check frame_alp_check \
    -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  cp "$build/compile_commands.json" "$out/"
  cp "$build/CMakeCache.txt" "$out/"
  for peer in turbopfor fastlanes alp; do
    binary="$build/$study/codecs/frame_${peer}_check"
    cp "$binary" "$out/bin/"
    sha256sum "$binary" > "$out/$peer-before.sha256"
    taskset -c "$SIXDB_CPU" "$binary" > "$out/$peer.json" 2> "$out/$peer.stderr"
    sha256sum -c "$out/$peer-before.sha256" > "$out/$peer-after.txt"
  done
done
python3 - "$SIXDB_RESULTS/peer-source-sha256.json" <<'PY'
from pathlib import Path
import hashlib,json,sys
root=Path('build/deps/frame-codecs')
result={}
for source in root.glob('*-src'):
    result[source.name]={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(source.rglob('*')) if p.is_file()}
Path(sys.argv[1]).write_text(json.dumps(result,indent=2)+'\n')
PY
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
echo complete > "$SIXDB_RESULTS/status.txt"
