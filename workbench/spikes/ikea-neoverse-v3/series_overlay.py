"""Build-private mixed Local residual transpose variant."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
relative=Path('ikea/seriespack/detail/native/neon/read.h')
text=(root/'ikea/include'/relative).read_text()
start=text.index('        } else if constexpr (F::body != 0) {')
end=text.index('        } else {', start+1)
text=text[:start]+text[end:]
destination=output/relative
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(text)
