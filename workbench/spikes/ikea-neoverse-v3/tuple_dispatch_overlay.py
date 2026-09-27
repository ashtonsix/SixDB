"""Move the common NEON table-count branch outside the four packet parts."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
relative = Path('ikea/tuplepack/detail/native/shuffle.h')
text = (root / 'ikea/include' / relative).read_text()
text = text.replace('template <unsigned I>\n', 'template <unsigned I, unsigned Registers = 0>\n', 1)
text = text.replace('if (p.routes <= 1)', 'if (Registers == 1 || (Registers == 0 && p.routes <= 1))', 1)
text = text.replace('else if (p.routes <= 3)', 'else if (Registers == 2 || (Registers == 0 && p.routes <= 3))', 1)
text = text.replace('else if (p.routes <= 7)', 'else if (Registers == 3 || (Registers == 0 && p.routes <= 7))', 1)
old = '''    return {apply16<0>(source, p), apply16<1>(source, p), apply16<2>(source, p),
            apply16<3>(source, p)};'''
new = '''    auto parts = [&]<unsigned Registers>() -> native_packet {
        return {apply16<0, Registers>(source, p), apply16<1, Registers>(source, p),
                apply16<2, Registers>(source, p), apply16<3, Registers>(source, p)};
    };
    if (p.routes <= 1) return parts.template operator()<1>();
    if (p.routes <= 3) return parts.template operator()<2>();
    if (p.routes <= 7) return parts.template operator()<3>();
    return parts.template operator()<4>();'''
assert old in text
text = text.replace(old, new)
destination = output / relative
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(text)
