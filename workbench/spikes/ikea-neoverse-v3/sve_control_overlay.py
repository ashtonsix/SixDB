"""Disable only the candidate SVE source-access branch at fixed compiler ISA/tune."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
relative = Path('ikea/bec256/author/native.h')
text = (root / 'ikea/include' / relative).read_text()
assert 'defined(__ARM_FEATURE_SVE)' in text, 'Replay this rejected candidate from the promotion-screen source capture'
text = text.replace('defined(__ARM_FEATURE_SVE)', '0')
destination = output / relative
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(text)
