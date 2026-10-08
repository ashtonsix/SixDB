#!/usr/bin/env python3
"""Apply one reuse candidate to a private copy of matching allocator sources."""
import argparse
from pathlib import Path

POLICIES = ('segmented', 'cascade-mru', 'cascade-fifo', 'cached',
            'window-first-ready', 'finite-two-pass', 'combined')


def replace(path, old, new):
    text = path.read_text()
    assert text.count(old) == 1, (path, old[:100])
    path.write_text(text.replace(old, new))


def window_first_ready(root):
    path = root / 'orbital/src/allocation/window.cpp'
    text = path.read_text()
    start = text.index('Window* select_idle_window(')
    end = text.index('\nResult<void> window_allocate(', start)
    original = text[start:end]
    # Match the baseline selector before replacing it; another source policy is
    # not a valid "before" for this isolated ablation.
    assert 'unsigned hint_budget = 4;' in original
    assert 'window->hint.temperature.value_or(Temperature::cold)' in original
    selector = '''Window* select_idle_window(Pool& pool, std::size_t reserved, std::size_t prepared,
    std::size_t alignment, Temperature, int) noexcept {
  Window* selected = nullptr;
  std::size_t best_extra = maximum;
  bool best_prepared = false;
  for (auto* window = pool.internal->idle_windows; window; window = window->next_idle) {
    if (window->reserved < reserved ||
        reinterpret_cast<std::uintptr_t>(window->address) % alignment) continue;
    const auto extra = prepared > window->charged ? prepared - window->charged : 0;
    const bool ready = prepared <= window->prepared;
    if (!extra && ready) return window;
    if (extra < best_extra || (extra == best_extra && ready && !best_prepared)) {
      selected = window; best_extra = extra; best_prepared = ready;
    }
  }
  return selected;
}
'''
    path.write_text(text[:start] + selector + text[end:])


def finite_two_pass(root):
    path = root / 'orbital/src/allocation/group.cpp'
    replace(path,
            '  // Same default readiness and advisory preference ordering as allocate.\n'
            '  for (unsigned pass = 0; pass < 4 && !record.group && refusal == Error::capacity; ++pass) {',
            '  // Try prepared backing before paying for new pages.\n'
            '  for (unsigned pass = 0; pass < 2 && !record.group && refusal == Error::capacity; ++pass) {')
    replace(path,
            '      const bool match = group->last_use.temperature.value_or(Temperature::cold) == Temperature::warm;\n'
            '      if ((pass % 2 == 0) != match) continue;\n', '')
    replace(path, '      auto selected = select_preparation_home(record, pass < 2);',
            '      auto selected = select_preparation_home(record, pass == 0);')
    replace(path,
            'Result<void> finite_allocate(Pool& pool, const AllocationRequest& request, std::size_t size,\n'
            '    Temperature preferred, int cpu, Allocation& result) {',
            'Result<void> finite_allocate(Pool& pool, const AllocationRequest& request, std::size_t size,\n'
            '    Temperature, int, Allocation& result) {')
    replace(path,
            '  // Reuse prepared backing before paying for new pages. Temperature and CPU\n'
            '  // remain advisory tie-breakers; they cannot hide a feasible allocation.\n'
            '  for (unsigned pass = 0; pass < 4 && !selected; ++pass) {',
            '  // Reuse prepared backing before paying for new pages.\n'
            '  for (unsigned pass = 0; pass < 2 && !selected; ++pass) {')
    replace(path,
            '      const bool match = group->last_use.temperature.value_or(Temperature::cold) == preferred &&\n'
            '          (preferred != Temperature::hot || group->last_use.cpu == cpu);\n'
            '      if ((pass % 2 == 0) != match) continue;\n', '')
    replace(path,
            '        if (ready != (pass < 2)) { release_finite_candidate(*group, *candidate, excluded); continue; }',
            '        if (ready != (pass == 0)) { release_finite_candidate(*group, *candidate, excluded); continue; }')


def apply(root, policy, hot, warm):
    if policy in ('window-first-ready', 'finite-two-pass', 'combined'):
        if policy != 'finite-two-pass':
            window_first_ready(root)
        if policy != 'window-first-ready':
            finite_two_pass(root)
        return
    internal = root / 'orbital/src/allocation/internal.h'
    window = root / 'orbital/src/allocation/window.cpp'
    common = root / 'orbital/src/allocation/allocation.cpp'
    if policy == 4:
        replace(internal, '  Window* idle_windows{};',
                '  Window* idle_windows{};\n  std::array<Window*, 3> idle_candidates{};')
        replace(internal, '  head = &window;\n}',
                '  head = &window;\n  const auto t = window.hint.temperature.value_or(Temperature::cold);\n'
                '  window.domain.pool->internal->idle_candidates[t == Temperature::hot ? 0 : t == Temperature::warm ? 1 : 2] = &window;\n}')
        replace(internal, '  if (!window.previous_idle) return;\n  *window.previous_idle',
                '  if (!window.previous_idle) return;\n'
                '  const auto t = window.hint.temperature.value_or(Temperature::cold);\n'
                '  auto& cached = window.domain.pool->internal->idle_candidates[t == Temperature::hot ? 0 : t == Temperature::warm ? 1 : 2];\n'
                '  if (cached == &window) cached = nullptr;\n  *window.previous_idle')
        replace(window, '  Window* selected = nullptr;\n  std::size_t best_extra',
                '  const auto tier = preferred == Temperature::hot ? 0 : preferred == Temperature::warm ? 1 : 2;\n'
                '  auto* cached = pool.internal->idle_candidates[tier];\n'
                '  if (cached && cached->reserved >= reserved && cached->prepared >= prepared &&\n'
                '      reinterpret_cast<std::uintptr_t>(cached->address) % alignment == 0 &&\n'
                '      cached->hint.temperature.value_or(Temperature::cold) == preferred &&\n'
                '      (preferred != Temperature::hot || cached->hint.cpu == cpu)) return cached;\n'
                '  Window* selected = nullptr;\n  std::size_t best_extra')
        return
    replace(internal, '  Window** previous_idle{};',
            '  Window* previous_idle{};\n  std::uint64_t idle_charge{};\n'
            '  unsigned idle_tier{2};\n  bool idle_linked{};')
    replace(internal, 'struct ReadyCandidate {',
            'struct IdleQueue {\n  Window* first{};\n  Window* last{};\n'
            '  std::uint64_t bytes{};\n};\n\nstruct ReadyCandidate {')
    replace(internal, '  Window* idle_windows{};', '  std::array<IdleQueue, 3> idle_windows;')
    text = internal.read_text()
    start = text.index('inline void add_idle(Window& window) noexcept {')
    end = text.index('inline void add_available(', start)
    leaves = r'''inline void remove_idle(Window& window) noexcept {
  if (!window.idle_linked) return;
  auto& queue = window.domain.pool->internal->idle_windows[window.idle_tier];
  if (window.previous_idle) window.previous_idle->next_idle = window.next_idle;
  else queue.first = window.next_idle;
  if (window.next_idle) window.next_idle->previous_idle = window.previous_idle;
  else queue.last = window.previous_idle;
  queue.bytes -= window.idle_charge;
  window.next_idle = window.previous_idle = nullptr;
  window.idle_linked = false;
}

inline void link_idle(Window& window) noexcept {
  auto& queue = window.domain.pool->internal->idle_windows[window.idle_tier];
  window.next_idle = queue.first;
  window.previous_idle = nullptr;
  if (queue.first) queue.first->previous_idle = &window;
  else queue.last = &window;
  queue.first = &window;
  window.idle_charge = window.charged;
  queue.bytes += window.idle_charge;
  window.idle_linked = true;
}

inline void add_idle(Window& window, bool fresh = false) noexcept {
  if (fresh) window.idle_tier = unsigned(window.hint.temperature.value_or(Temperature::cold));
  if (window.idle_linked || window.trim_pending || window.use.users.load(std::memory_order_acquire) ||
      window.domain.queued.load(std::memory_order_acquire)) return;
  constexpr std::array<std::uint64_t, 2> limits{HOT_BYTES, WARM_BYTES};
  if constexpr (POLICY != 1) {
    while (window.idle_tier != 2 && window.charged > limits[window.idle_tier]) ++window.idle_tier;
  }
  link_idle(window);
  if constexpr (POLICY != 1) {
    auto& queues = window.domain.pool->internal->idle_windows;
    for (unsigned tier = 0; tier != 2; ++tier) {
      while (queues[tier].bytes > limits[tier]) {
        auto& oldest = *queues[tier].last;
        remove_idle(oldest);
        oldest.idle_tier = tier + 1;
        link_idle(oldest);
      }
    }
  }
}

inline void refresh_idle(Window& window) noexcept {
  if (!window.idle_linked) return;
  remove_idle(window);
  add_idle(window);
}

'''.replace('HOT_BYTES', str(hot)).replace('WARM_BYTES', str(warm)).replace('POLICY', str(policy))
    internal.write_text(text[:start] + leaves + text[end:])
    replace(common, '  } else add_idle(*static_cast<Window*>(domain.context));',
            '  } else add_idle(*static_cast<Window*>(domain.context), true);')
    replace(common,
            '      const bool cold = window->hint.temperature.value_or(Temperature::cold) == Temperature::cold;',
            '      const bool cold = window->idle_tier == 2;')
    text = window.read_text()
    start = text.index('  for (auto* window = pool.internal->idle_windows;')
    end = text.index('  return selected;', start)
    loop = r'''  const auto wanted = unsigned(preferred);
  const std::array<unsigned, 4> order{wanted, 0, 1, 2};
  for (unsigned visit = 0; visit != order.size(); ++visit) {
    const auto tier = order[visit];
    if (visit && tier == wanted) continue;
    auto& queue = pool.internal->idle_windows[tier];
    for (auto* window = POLICY == 3 ? queue.last : queue.first; window;
         window = POLICY == 3 ? window->previous_idle : window->next_idle) {
      if (ready) {
        if (!hint_budget) return selected;
        --hint_budget;
      }
      if (window->reserved < reserved ||
          reinterpret_cast<std::uintptr_t>(window->address) % alignment) continue;
      const auto extra = prepared > window->charged ? prepared - window->charged : 0;
      auto rank = prepared > window->prepared ? 4u : 0u;
      if (tier != wanted) rank += 2;
      if (preferred == Temperature::hot && window->hint.cpu != cpu) ++rank;
      if (extra < best_extra || (extra == best_extra && rank < best)) {
        selected = window; best_extra = extra; best = rank;
      }
      if (!extra && prepared <= window->prepared) {
        if (!rank) return selected;
        ready = true;
      }
    }
  }
'''.replace('POLICY', str(policy))
    text = text[:start] + loop + text[end:]
    # Charge can grow before a failed syscall leaves the Window in idle inventory.
    text = text.replace('    selected->charged += extra;',
                        '    selected->charged += extra;\n    if (extra) refresh_idle(*selected);')
    window.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('policy', choices=POLICIES)
    parser.add_argument('--hot-bytes', type=int, default=256 * 1024)
    parser.add_argument('--warm-bytes', type=int, default=4 * 1024 * 1024)
    args = parser.parse_args()
    apply(args.source, {'segmented': 1, 'cascade-mru': 2, 'cascade-fifo': 3, 'cached': 4}.get(args.policy, args.policy),
          args.hot_bytes, args.warm_bytes)
