#!/usr/bin/env bash
set -euo pipefail
: "${SIXDB_SOURCE:?}"
: "${SIXDB_RESULTS:?}"
cd "$SIXDB_SOURCE"
if ! pkg-config --exists lmdb; then sudo apt-get update -qq; sudo apt-get install -y -qq liblmdb-dev; fi
if [[ ! -x /usr/bin/time ]]; then sudo apt-get update -qq; sudo apt-get install -y -qq time; fi
routes=workbench/spikes/orbital-mvcc/metadata/orbital/allocation/routes
performance=workbench/spikes/orbital-mvcc/performance
apply() { git apply --check "$1"; git apply "$1"; }
undo() { git apply -R --check "$1"; git apply -R "$1"; }
apply "$routes/accounting.patch"
for treatment in storage16 group-advice direct8 direct4; do
  case "$treatment" in
    group-advice) apply workbench/spikes/orbital-mvcc/metadata/orbital/heat/group-advice.patch ;;
    direct8)
      apply "$routes/storage8checked-ready.patch"
      apply "$routes/checked-owner.patch"
      apply "$routes/direct-domain/direct-domain8.patch" ;;
    direct4)
      undo "$routes/direct-domain/direct-domain8.patch"
      undo "$routes/checked-owner.patch"
      undo "$routes/storage8checked-ready.patch"
      apply "$routes/storage4-word-loop-ready.patch"
      apply "$routes/checked-owner.patch"
      apply "$routes/direct-domain/direct-domain4.patch" ;;
  esac
  python3 "$performance/run.py" --output "$SIXDB_RESULTS/$treatment" \
    --profile composed-quick --repetitions 1 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
  python3 "$performance/run.py" --output "$SIXDB_RESULTS/$treatment-context" \
    --profile composed-context --repetitions 1 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
done
