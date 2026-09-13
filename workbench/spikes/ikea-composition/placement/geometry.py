"""Required-byte geometry, independently of timing and native load widths."""
from collections import Counter

for stride in (96, 128):
    for grain in (1, 16):
        for centred in (False, True):
            counts = Counter()
            for tile in range(2):
                for first in range(0, 64, grain):
                    touched = set()
                    for row in range(first, first + grain):
                        body = row + (32 if centred and row >= 32 else 0)
                        tail = (32 if centred else 64) + row % 32
                        touched.update(((tile * stride + body) // 64,
                                        (tile * stride + tail) // 64))
                    assert max(touched) - min(touched) <= 1
                    counts[len(touched)] += 1
            print(f"stride={stride} grain={grain} centred={centred}: "
                  f"one-line={counts[1] / counts.total():.0%} max-lines={max(counts)}")
