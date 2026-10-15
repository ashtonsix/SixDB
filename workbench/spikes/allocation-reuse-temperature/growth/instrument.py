"""Count selector work in a separate TU; never use this build for timing."""
from pathlib import Path


def write_counter(source: Path, destination: Path, indexed: bool):
    text = source.read_text()
    start = text.index('Window* select_idle_window(')
    end = text.index('\nResult<void> window_allocate(', start)
    selector = text[start:end]
    loop = ('    for (auto* window = entry->second.head; window; window = window->next_size) {\n'
            if indexed else
            '  for (auto* window = pool.internal->idle_windows; window; window = window->next_idle) {\n')
    if selector.count(loop) != 1:
        raise ValueError('selector loop changed; review diagnostic instrumentation')
    selector = selector.replace(loop, loop + '''      ++growth_list_visits;
      if (window->reserved < reserved) ++growth_ineligible_visits;
''')
    if indexed:
        head = '  auto* head = state.idle_windows;\n'
        entries = '  for (auto entry = state.window_sizes.lower_bound(reserved); entry != state.window_sizes.end(); ++entry) {\n'
        if selector.count(head) != 1 or selector.count(entries) != 1:
            raise ValueError('indexed selector changed; review diagnostic instrumentation')
        selector = selector.replace(head, head + '  if (head) ++growth_head_probes;\n')
        selector = selector.replace(entries, entries + '    ++growth_map_entries;\n')
    globals_ = '''extern "C" {
std::uint64_t growth_head_probes{}, growth_list_visits{};
std::uint64_t growth_ineligible_visits{}, growth_map_entries{};
}
'''
    text = text[:start] + selector + text[end:]
    text = text.replace('namespace orbital::allocation::detail {',
                        'namespace orbital::allocation::detail {\n' + globals_, 1)
    destination.write_text(text)
