#!/usr/bin/env bash
set -euo pipefail
: "${SIXDB_SOURCE:?}"
: "${SIXDB_RESULTS:?}"
cd "$SIXDB_SOURCE"
if ! pkg-config --exists lmdb; then sudo apt-get update -qq; sudo apt-get install -y -qq liblmdb-dev; fi
if [[ ! -x /usr/bin/time ]]; then sudo apt-get update -qq; sudo apt-get install -y -qq time; fi
for treatment in fresh-reader renew-reader; do
 if [[ "$treatment" == renew-reader ]]; then
  python3 - <<'POLICY'
from pathlib import Path
p=Path('workbench/spikes/orbital-mvcc/performance/lmdb.h');s=p.read_text()
assert 'reuse_short_reader = false;' in s
p.write_text(s.replace('reuse_short_reader = false;','reuse_short_reader = true;',1))
POLICY
 fi
 python3 workbench/spikes/orbital-mvcc/performance/run.py --output "$SIXDB_RESULTS/$treatment" \
  --profile fusion-quick --repetitions 2 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
 done
