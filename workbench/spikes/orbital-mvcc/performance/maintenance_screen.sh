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
performance=workbench/spikes/orbital-mvcc/performance
git apply --check "$performance/controls/maintenance-caller.patch"
git apply "$performance/controls/maintenance-caller.patch"
for treatment in original new-full new-bounded32; do
  case "$treatment" in
    new-full) git apply --check "$performance/controls/maintenance-metadata.patch"; git apply "$performance/controls/maintenance-metadata.patch" ;;
    new-bounded32) git apply --check "$performance/controls/maintenance-budget32.patch"; git apply "$performance/controls/maintenance-budget32.patch" ;;
  esac
  python3 "$performance/run.py" --output "$SIXDB_RESULTS/$treatment" \
    --profile composed-quick --repetitions 1 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
  python3 "$performance/run.py" --output "$SIXDB_RESULTS/$treatment-context" \
    --profile composed-context --repetitions 1 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
done
