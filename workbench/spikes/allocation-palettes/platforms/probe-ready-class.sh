#!/usr/bin/env bash
# Isolate a ready-hit class hint against the unmodified worker source capture.
set -euo pipefail
source_root="${SIXDB_SOURCE:-$PWD}"
source_root="$(cd "$source_root" && pwd)"
results="${SIXDB_RESULTS:-$source_root/build/allocation-palettes}"
mkdir -p "$results"
results="$(cd "$results" && pwd)"
clone="$source_root/build/allocation-palettes/ready-class/${SIXDB_JOB:-local-$$}/source"
cd "$source_root"
python3 - "$source_root" "$clone" "$results/ready-class-patch.json" <<'PY'
import hashlib, json, shutil, sys
from pathlib import Path
source, clone, receipt = map(Path, sys.argv[1:])
assert not clone.exists(), f'clone already exists: {clone}'
shutil.copytree(source, clone, symlinks=True,
                ignore=shutil.ignore_patterns('build', '.git', '__pycache__'))
changes = {
    'orbital/src/allocation/internal.h': [
        ('  std::uint8_t grid{};\n};\n\nstruct PoolState {',
         '  std::uint8_t grid{}, cls{};\n};\n\nstruct PoolState {', 1)],
    'orbital/src/allocation/group.cpp': [
        ('auto home = group.geometry->claim_home(*identity, size, alignment);',
         'auto home = group.geometry->claim_home_class(*identity, cached.cls, alignment);', 1),
        ('static_cast<std::uint8_t>(home.grid)};',
         'static_cast<std::uint8_t>(home.grid), home.cls};', 2)]}
sha = lambda data: hashlib.sha256(data).hexdigest()
record = {'kind': 'Ready-candidate class hint only', 'baseline': str(source),
          'candidate': str(clone), 'changes': {}}
for relative, patches in changes.items():
    path = clone / relative
    before = path.read_bytes()
    text = before.decode()
    for old, new, count in patches:
        assert text.count(old) == count, f'unexpected source shape: {relative}: {old}'
        text = text.replace(old, new)
    path.write_text(text)
    record['changes'][relative] = {'before_sha256': sha(before),
                                  'after_sha256': sha(path.read_bytes())}
receipt.write_text(json.dumps(record, indent=2) + '\n')
print(f'Ready-class probe source: {clone}', flush=True)
PY
if [[ "${PALETTE_VALIDATE:-0}" == 1 ]]; then
  (cd "$clone"; SIXDB_RESULTS="$results/ready-class-validation" \
    PALETTE_EXPECTED_PAGE="${PALETTE_PUBLIC_EXPECTED_PAGE:-$(getconf PAGESIZE)}" \
    bash workbench/spikes/allocation-palettes/platforms/validate.sh)
fi
PALETTE_PUBLIC_TRIALS="${PALETTE_PUBLIC_TRIALS:-8}" \
PALETTE_PUBLIC_WORKLOADS="${PALETTE_PUBLIC_WORKLOADS:-finite-control,finite-fallback,hot-loop}" \
PALETTE_PUBLIC_TOUCH="${PALETTE_PUBLIC_TOUCH:-64,full}" \
SIXDB_RESULTS="$results/ready-class" \
  bash "$source_root/workbench/spikes/allocation-palettes/platforms/run-public.sh" \
    "$source_root" "$clone" "$results/ready-class"
