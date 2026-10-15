"""One selector-only change per alternative; no allocation or trim changes."""
from pathlib import Path


def write_variants(source: Path, output: Path):
    original = source.read_text()
    start = original.index('Window* select_idle_window(')
    end = original.index('\nResult<void> window_allocate(', start)
    selection = original[start:end]
    needle = '    if (!extra && ready) return window;'
    if selection.count(needle) != 1:
        raise ValueError('first-ready selector changed; inspect before adapting')
    prefer = selection.replace(needle, '''    // Accept modest slack immediately; continue past grossly oversized backing.
    // If no closer ready candidate exists, the original fallback still reuses it.
    if (!extra && ready && window->prepared - prepared <= prepared) return window;''')
    best = selection.replace('  bool best_prepared = false;', '''  bool best_prepared = false;
  Window* best_ready = nullptr;''').replace(needle, '''    if (!extra && ready) {
      if (window->prepared == prepared) return window;
      if (!best_ready || window->prepared < best_ready->prepared) best_ready = window;
      continue;
    }''').replace('  return selected;', '  return best_ready ? best_ready : selected;')
    output.mkdir(parents=True, exist_ok=False)
    for name, replacement in [('baseline', selection), ('prefer-2x', prefer), ('best-fit', best)]:
        (output / (name + '.cpp')).write_text(original[:start] + replacement + original[end:])
