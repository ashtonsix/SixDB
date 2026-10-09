#!/usr/bin/env bash
set -euo pipefail
: "${SIXDB_SOURCE:?}"
: "${SIXDB_RESULTS:?}"
cd "$SIXDB_SOURCE"
if ! pkg-config --exists lmdb; then sudo apt-get update -qq; sudo apt-get install -y -qq liblmdb-dev; fi
if [[ ! -x /usr/bin/time ]]; then sudo apt-get update -qq; sudo apt-get install -y -qq time; fi
performance=workbench/spikes/orbital-mvcc/performance
for treatment in baseline fused; do
  if [[ "$treatment" == fused ]]; then
    python3 - <<'CONTEXT'
from pathlib import Path
p=Path('workbench/spikes/orbital-mvcc/metadata/composed/backend.h')
s=p.read_text()
assert '#include <algorithm>\n#include <limits>\n' in s
p.write_text(s.replace('#include <algorithm>\n#include <limits>\n','#include <algorithm>\n',1))
CONTEXT
    git apply --check workbench/spikes/orbital-mvcc/metadata/composed/controls/read-fusion/read-fusion.patch
    git apply workbench/spikes/orbital-mvcc/metadata/composed/controls/read-fusion/read-fusion.patch
    python3 - <<'CONTEXT'
from pathlib import Path
p=Path('workbench/spikes/orbital-mvcc/metadata/composed/backend.h')
s=p.read_text();p.write_text(s.replace('#include <algorithm>\n','#include <algorithm>\n#include <limits>\n',1))
CONTEXT
  fi
  python3 "$performance/run.py" --output "$SIXDB_RESULTS/$treatment" \
    --profile fusion-quick --repetitions 2 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
done
