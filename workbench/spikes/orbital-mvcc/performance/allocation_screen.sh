#!/usr/bin/env bash
set -euo pipefail
: "${SIXDB_SOURCE:?}"
: "${SIXDB_RESULTS:?}"
cd "$SIXDB_SOURCE"
if ! pkg-config --exists lmdb; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq liblmdb-dev
fi
if [[ ! -x /usr/bin/time ]]; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq time
fi
patches=workbench/spikes/orbital-mvcc/metadata/orbital/allocation
for treatment in current compact storage16 growth; do
  case "$treatment" in
    compact) git apply --check "$patches/compact-controls.patch"; git apply "$patches/compact-controls.patch" ;;
    storage16) git apply --check "$patches/storage16.patch"; git apply "$patches/storage16.patch" ;;
    growth) git apply --check "$patches/growth.patch"; git apply "$patches/growth.patch" ;;
  esac
  python3 workbench/spikes/orbital-mvcc/performance/run.py \
    --output "$SIXDB_RESULTS/$treatment" --profile quick --repetitions 2 \
    --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
done
