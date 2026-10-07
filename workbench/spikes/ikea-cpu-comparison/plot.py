#!/usr/bin/env python3
"""Render the anonymous October 2026 CPU snapshot and workload distributions."""
import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.lines import Line2D
from matplotlib.patches import Rectangle
import numpy as np

PAPER = '#f6f5ef'
INK = '#203138'
MUTED = '#66777c'
GRID = '#d7dfda'
COLORS = ['#477694', '#8166ab', '#bd7934', '#7b8b2d', '#b95164', '#087d70']
FIELDS = {'title', 'cases', 'processors', 'groups', 'case_core_indices',
          'case_dollar_indices', 'core_wins', 'dollar_wins', 'dollar_wins_4core_proxy',
          'pairwise_core_wins', 'pairwise_dollar_wins', 'pricing_date', 'region',
          'reference_date', 'new_date', 'sources'}
PROCESSOR_FIELDS = {'id', 'name', 'instance', 'core_index', 'core_p10', 'core_p90',
                    'dollar_index', 'usd_vcpu_hour', 'usd_core_hour', 'listed_vcpus',
                    'allocated_cores'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.data.read_text())
    catalog_clocks = json.loads(Path(__file__).with_name('catalog-clocks.json').read_text())
    if set(data) != FIELDS:
        raise ValueError('Unexpected public fields')
    if [p['id'] for p in data['processors']] != ['v2', 'v3', 'gnr', 'zen4', 'zen5', 'm1']:
        raise ValueError('Unexpected processor order')
    if set(catalog_clocks) != {p['id'] for p in data['processors']}:
        raise ValueError('Incomplete catalog clocks')
    if [p['name'] for p in data['processors']] != ['Neoverse V2', 'Neoverse V3', 'Granite Rapids', 'Zen 4', 'Zen 5', 'M1 Pro']:
        raise ValueError('Unexpected public processor labels')
    if [p['instance'] for p in data['processors']] != ['c8g.xlarge', 'c9g.xlarge', 'c8i.xlarge', 'c7a.xlarge', 'c8a.xlarge', 'mac2.metal']:
        raise ValueError('Unexpected public price labels')
    for p in data['processors']:
        allowed = PROCESSOR_FIELDS | ({'dollar_index_4core_proxy'} if p['id'] == 'm1' else set())
        if set(p) != allowed:
            raise ValueError('Unexpected public processor fields')
    if data['processors'][5]['allocated_cores'] != 8:
        raise ValueError('M1 primary allocation must use the original eight listed cores')
    for g in data['groups']:
        if set(g) != {'label', 'cases', 'core_indices', 'dollar_indices'}:
            raise ValueError('Unexpected anonymous group fields')
    if [g['label'] for g in data['groups']] != ['A', 'B', 'C', 'D']:
        raise ValueError('Unexpected anonymous group labels')
    core = np.asarray(data['case_core_indices'], dtype=float)
    dollar = np.asarray(data['case_dollar_indices'], dtype=float)
    if core.shape != (data['cases'], 6) or dollar.shape != core.shape:
        raise ValueError('Incomplete public case matrices')
    if not np.isfinite(core).all() or not np.isfinite(dollar).all() or min(core.min(), dollar.min()) <= 0:
        raise ValueError('Invalid public measurements')
    if sum(g['cases'] for g in data['groups']) != data['cases']:
        raise ValueError('Incomplete groups')
    for kind in ['core_wins', 'dollar_wins']:
        if sum(data[kind]['clear']) + data[kind]['no_clear'] != data['cases']:
            raise ValueError('Invalid win counts')

    plt.rcParams.update({'font.family': 'DejaVu Sans', 'text.color': INK,
                         'axes.labelcolor': MUTED, 'xtick.color': MUTED,
                         'ytick.color': MUTED, 'svg.fonttype': 'none', 'font.size': 13})
    width, height = 2400, 1960
    fig = plt.figure(figsize=(24, height / 100), facecolor=PAPER)
    canvas = fig.add_axes([0, 0, 1, 1])
    canvas.set_xlim(0, width); canvas.set_ylim(height, 0); canvas.axis('off')
    texts = []

    def text(x, y, value, size=24, color=INK, weight='normal', **kwargs):
        kwargs.setdefault('va', 'top')
        artist = canvas.text(x, y, value, fontsize=size * .72, color=color,
                             fontweight=weight, zorder=8, **kwargs)
        texts.append(artist)
        return artist

    def line(x1, y1, x2, y2, color=GRID, lw=1, **kwargs):
        canvas.add_line(Line2D([x1, x2], [y1, y2], color=color, linewidth=lw, **kwargs))

    def axes(x, y, w, h):
        return fig.add_axes([x / width, 1 - (y + h) / height, w / width, h / height], facecolor=PAPER)

    names = [p['name'] for p in data['processors']]
    short = ['V2', 'V3', 'Intel', 'Zen 4', 'Zen 5', 'M1']
    text(96, 56, 'SINGLE-THREAD PERFORMANCE  /  CLOUD PRICE MODEL', 22, MUTED, 'bold')
    text(96, 106, 'Six CPUs. October 2026 snapshot.', 56, weight='bold')
    text(96, 202, f"{data['cases']} matched cases · equal-weight geometric means · Neoverse V3 = 1.00×", 27, MUTED)
    line(96, 267, 2304, 267)

    # A fixed score strip makes the two aggregate measures easy to compare by CPU.
    column = 2208 / 6
    for i, p in enumerate(data['processors']):
        x = 96 + i * column
        text(x, 303, p['name'], 28, COLORS[i], 'bold')
        text(x, 357, 'Per core', 23, MUTED)
        text(x + 308, 345, f"{p['core_index']:.2f}×", 42, INK, 'bold', ha='right')
        text(x, 417, 'Per dollar', 23, MUTED)
        text(x + 308, 405, f"{p['dollar_index']:.2f}×", 42, COLORS[i], 'bold', ha='right')
        if i < 5:
            line(x + 336, 311, x + 336, 462)
    line(96, 497, 2304, 497)

    text(96, 535, 'PER CORE  /  Every individual case', 33, weight='bold')
    text(2304, 547, 'Each column is one case; darker means closer to its fastest CPU.', 22, MUTED, ha='right')
    heat_x, heat_y, heat_w, heat_h = 348, 650, 1956, 220
    heat = axes(heat_x, heat_y, heat_w, heat_h)
    relative = (core / core.max(axis=1)[:, None]).T
    cmap = LinearSegmentedColormap.from_list('relative_performance', ['#f1e0cd', '#dfe9df', '#80b8a9', '#075b58'])
    heat.imshow(relative, aspect='auto', cmap=cmap, vmin=0, vmax=1, interpolation='nearest',
                extent=[0, data['cases'], 5.5, -.5])
    heat.set_xticks([]); heat.set_yticks([])
    for spine in heat.spines.values(): spine.set_visible(False)
    for i in range(1, 6): heat.axhline(i - .5, color=PAPER, lw=5)
    for i, name in enumerate(names):
        text(96, heat_y + (i + .5) * heat_h / 6, name, 23, COLORS[i], 'bold', va='center')
    start = 0
    for group in data['groups']:
        middle = heat_x + heat_w * (start + group['cases'] / 2) / data['cases']
        text(middle, 609, f"GROUP {group['label']} · {group['cases']}", 23, MUTED, 'bold', ha='center')
        if start: heat.axvline(start, color=PAPER, lw=7)
        start += group['cases']
    text(96, 897, 'Anonymous A–D groups; cases ordered within each by relative Arm → x86 performance.', 21, MUTED)
    key = axes(1894, 903, 210, 13)
    key.imshow(np.linspace(0, 1, 256)[None, :], aspect='auto', cmap=cmap, vmin=0, vmax=1)
    key.axis('off')
    text(1875, 896, '0%', 19, MUTED, ha='right')
    text(2124, 896, '100% of fastest', 19, MUTED)
    line(96, 950, 2304, 950)

    text(96, 987, 'CLOUD PRICE & CLOCK', 33, weight='bold')
    text(96, 1042, f"AWS on-demand · {data['region']} · USD · {data['pricing_date']}", 22, MUTED)
    text(96, 1097, 'Processor', 23, MUTED, 'bold')
    text(440, 1097, 'Price basis', 23, MUTED, 'bold')
    text(1020, 1097, '$ / vCPU-hour', 23, MUTED, 'bold', ha='right')
    text(1380, 1097, 'Catalog GHz', 23, MUTED, 'bold', ha='right')
    for i, p in enumerate(data['processors']):
        y = 1150 + i * 40
        marker = '*' if p['id'] == 'gnr' else '†' if p['id'] == 'm1' else ''
        text(96, y, p['name'] + marker, 26, COLORS[i], 'bold')
        text(440, y, p['instance'] + ('†' if p['id'] == 'm1' else ''), 24, MUTED)
        text(1020, y, f"${p['usd_vcpu_hour']:.6f}" + marker, 27, INK, 'bold', ha='right')
        # The Mac catalog describes the rental proxy, not the measured local M1 Pro.
        clock = 'Unverified†' if p['id'] == 'm1' else catalog_clocks[p['id']].removesuffix(' GHz').lower()
        text(1380, y, clock, 27, INK, 'bold', ha='right')
    text(96, 1400, 'Catalog clocks; operating frequencies during the runs were not measured.', 22, MUTED)

    text(1540, 987, 'CLEAR CASE WINS', 30, weight='bold')
    text(1540, 1042, '>3% faster than the runner-up among all six', 21, MUTED)
    text(2155, 1097, 'Core', 22, MUTED, 'bold', ha='right')
    text(2304, 1097, 'Dollar', 22, MUTED, 'bold', ha='right')
    for i, p in enumerate(data['processors']):
        y = 1150 + i * 40
        text(1540, y, p['name'], 24, COLORS[i], 'bold')
        text(2155, y, str(data['core_wins']['clear'][i]), 28, INK, 'bold', ha='right')
        text(2304, y, str(data['dollar_wins']['clear'][i]), 28, COLORS[i], 'bold', ha='right')
    text(1540, 1400, 'No clear winner', 22, MUTED)
    text(2155, 1400, str(data['core_wins']['no_clear']), 24, MUTED, 'bold', ha='right')
    text(2304, 1400, str(data['dollar_wins']['no_clear']), 24, MUTED, 'bold', ha='right')
    notes = [
        '* Intel: 2 billed SMT vCPUs/core; SMT disabled. Per-dollar indices use $0.093710/core-hour; disabling SMT does not reduce the bill.',
        '† M1 Pro tested locally; core placement unverified. Price proxy: base M1, $0.65/host-hour ÷ 8 cores (4P + 4E); 24h minimum.',
        '27 Sep kernels · single-thread CPU time · ARM: identical V2-tuned binaries · x86: native tuning · 5 repeats, 1–2 trials · compute prices only',
    ]
    for y, note in zip([1458, 1496, 1534], notes):
        text(96, y, note, 22, MUTED)
    line(96, 1582, 2304, 1582)

    text(96, 1614, 'HEAD TO HEAD  /  Cases won by >3%', 30, weight='bold')
    text(2304, 1623, 'Read each row against the column · counts out of ' + str(data['cases']), 21, MUTED, ha='right')
    # The exact pairwise ledger preserves each neutral two-processor comparison.
    for x, kind, label in [(96, 'pairwise_core_wins', 'Per core'),
                           (1250, 'pairwise_dollar_wins', 'Per dollar')]:
        label_x, grid_x, cell_w, row_h = x, x + 300, 124, 33
        text(label_x, 1673, label, 23, INK, 'bold')
        for j, name in enumerate(short):
            text(grid_x + (j + .5) * cell_w, 1673, name, 22, COLORS[j], 'bold', ha='center')
        for i, name in enumerate(names):
            y = 1713 + i * row_h
            text(label_x, y + row_h / 2, name, 21, COLORS[i], 'bold', va='center')
            for j in range(6):
                value = data[kind][i][j]
                shade = '#e8eee8' if value is None else cmap(.13 + .87 * value / data['cases'])
                canvas.add_patch(Rectangle((grid_x + j * cell_w, y), cell_w - 6, row_h - 3,
                                           facecolor=shade, edgecolor='none', zorder=1))
                text(grid_x + (j + .5) * cell_w - 3, y + (row_h - 3) / 2,
                     '—' if value is None else str(value), 21,
                     color=PAPER if value is not None and value / data['cases'] > .69 else INK,
                     weight='bold', va='center', ha='center')

    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.canvas.draw()
    renderer = fig.canvas.get_renderer()
    for artist in texts:
        box = artist.get_window_extent(renderer)
        if box.x0 < 0 or box.y0 < 0 or box.x1 > fig.bbox.width or box.y1 > fig.bbox.height:
            raise ValueError('Text exceeds canvas: ' + artist.get_text())
    for extension in ['png', 'svg']:
        metadata = {'Title': 'Six CPUs. October 2026 snapshot.', 'Creator': 'Benchmark visualization'}
        if extension == 'svg': metadata['Date'] = None
        path = args.output.with_suffix('.' + extension)
        fig.savefig(path, dpi=144, facecolor=PAPER, metadata=metadata)
        if extension == 'svg':
            path.write_text('\n'.join(line.rstrip() for line in path.read_text().splitlines()) + '\n')
        print(path)
    plt.close(fig)


if __name__ == '__main__':
    main()
