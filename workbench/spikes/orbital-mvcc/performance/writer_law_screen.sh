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
allocation=workbench/spikes/orbital-mvcc/metadata/orbital/allocation
for patch in compact-controls storage16 growth; do
  git apply --check "$allocation/$patch.patch"
  git apply "$allocation/$patch.patch"
done
performance=workbench/spikes/orbital-mvcc/performance
for treatment in baseline writer-law; do
  if [[ "$treatment" == writer-law ]]; then
    git apply --check workbench/spikes/orbital-mvcc/metadata/consumer/shape-write-integration.patch
    git apply workbench/spikes/orbital-mvcc/metadata/consumer/shape-write-integration.patch
    git apply --check "$performance/controls/writer-law-build.patch"
    git apply "$performance/controls/writer-law-build.patch"
  fi
  python3 "$performance/run.py" \
    --output "$SIXDB_RESULTS/$treatment" --profile composed-quick --repetitions 2 \
    --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
  python3 "$performance/run.py" \
    --output "$SIXDB_RESULTS/$treatment-context" --profile composed-context --repetitions 1 \
    --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
done
