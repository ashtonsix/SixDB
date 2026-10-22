#!/usr/bin/env python3
"""Render Intervals teaching figures from the imagegen composition studies.

Keep the original concept's query overlay, interval axis, rank bracket and
local-window illustrations. Mathematical labels and eligibility semantics are
checked here rather than copied from raster text. Shared Figure supplies the
Ikea palette, typography and accessible SVG descriptions.
Run: orb -m ubuntu python3 ikea/docs/intervals/images/generate.py
Previews belong in ignored build/diagrams/intervals/.
"""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).parent
STYLE = runpy.run_path(str(ROOT / 'ikea/docs/images/generate.py'))
Figure = STYLE['Figure']
INK, MUTED, RULE = (STYLE[k] for k in ('INK', 'MUTED', 'RULE'))
BLUE = '#175ca7'
GREEN = '#168348'
AMBER = '#a96a12'
BLUE_FILL = '#e1effc'
GREEN_FILL = '#e5f3df'
AMBER_FILL = '#fff5df'


def circle(f, x, y, radius, fill, stroke='none', width=2):
    f.items.append(f'<circle cx="{x}" cy="{y}" r="{radius}" fill="{fill}" '
                   f'stroke="{stroke}" stroke-width="{width}"/>')


def line(f, x1, y1, x2, y2, color, width=2, dash=False):
    extra = ' stroke-dasharray="6 5"' if dash else ''
    f.items.append(f'<path d="M{x1},{y1} L{x2},{y2}" fill="none" '
                   f'stroke="{color}" stroke-width="{width}"{extra}/>')


def encodings():
    sizes = [2] * 15 + [4, 3, 0, 5]
    ends = []
    for size in sizes:
        ends.append((ends[-1] if ends else 0) + size)
    assert ends[13:] == [28, 30, 34, 37, 37, 42]
    relative = [end - (ends[r // 16 * 16 - 1] if r >= 16 else 0)
                for r, end in enumerate(ends)]
    assert relative[14:] == [30, 34, 3, 3, 8]
    f = Figure(640, 'Three ways to store the same boundaries',
               'Excerpt at ranks 14 through 18. The G16 boundary lies between 15 and 16. '
               'Absolute ends 30,34,37,37,42; sizes 2,4,3,0,5; relative ends 30,34,3,3,8. '
               'Group 1 starts at 34. All three locate rank 18 at 37 with size 5.')
    f.heading(32, 42, 'Three ways to store the same boundaries')
    f.text(32, 77, 'One excerpt across a 16-rank group boundary. All numbers are decimal.',
           20, color=MUTED)
    f.text(394, 109, 'group 0', 17, color=MUTED, anchor='middle')
    f.text(614, 109, 'group 1', 17, color=MUTED, anchor='middle')
    line(f, 312, 119, 476, 119, RULE)
    line(f, 488, 119, 740, 119, RULE)
    for i, rank in enumerate(range(14, 19)):
        x = 350 + i * 88
        f.text(x, 146, f'rank {rank}', 18, 'bold' if rank == 18 else 'normal',
               color=GREEN if rank == 18 else INK, anchor='middle')
        f.text(x, 171, f'size {sizes[rank]}', 17, color=MUTED, anchor='middle')
    f.lines(819, 141, ['group 1', 'checkpoint'], 17, 23, color=MUTED, anchor='middle')
    for y, title, subtitle, values in (
        (190, 'Absolute Ends', 'Global end per rank', ends[14:]),
        (318, 'Sizes', 'Size per rank + group origin', sizes[14:]),
        (446, 'Relative Ends', 'Local end + group origin', relative[14:])):
        f.rect(32, y, 730, 100, '#f7f9fc', radius=6)
        f.text(50, y + 37, title, 23, 'bold')
        f.text(50, y + 70, subtitle, 17, color=MUTED)
        for i, value in enumerate(values):
            f.rect(312 + i * 88, y + 24, 76, 52, GREEN_FILL,
                   stroke=GREEN if i == 4 else '#a5cd9c', radius=4)
            f.text(350 + i * 88, y + 59, value, 25, anchor='middle', mono=True)
        if y != 190:
            f.rect(784, y + 24, 70, 52, BLUE_FILL, stroke='#8dbce7', radius=4)
            f.text(819, y + 59, 34, 25, anchor='middle', mono=True)
    line(f, 482, 125, 482, 558, MUTED, dash=True)
    f.rect(883, 190, 365, 356, '#fff8e9', stroke='#ead5a4', radius=8)
    f.heading(905, 228, 'Locate rank 18')
    for y, title, equations in (
        (245, 'Absolute Ends', ['start = previous end = 37', 'end = current end = 42']),
        (341, 'Sizes', ['start = 34 + 3 + 0 = 37', 'end = 37 + 5 = 42']),
        (437, 'Relative Ends', ['start = 34 + 3 = 37', 'end = 34 + 8 = 42'])):
        f.rect(897, y, 337, 82, 'white', stroke='#f0e4c9', radius=5)
        f.text(911, y + 25, title, 20, 'bold')
        f.lines(911, y + 49, equations, 17, 24)
    line(f, 32, 576, 1248, 576, RULE)
    f.text(32, 611, 'Stored widths follow admitted bounds. Zero-size ranks remain assigned.',
           20, color=MUTED)
    f.save(OUT / 'encodings.svg')


def ranges():
    cuts = [0, 8, 18, 26, 35, 46, 56]
    bounds = list(zip(cuts, cuts[1:]))
    assert [hi - lo for lo, hi in bounds] == [8, 10, 8, 9, 11, 10]
    assert all(bounds[r][1] == bounds[r + 1][0] for r in range(len(bounds) - 1))
    eligible = [1, 1, 0, 1, 1, 1]
    low, high = 9, 36
    overlapping = [r for r, (lo, hi) in enumerate(bounds)
                   if eligible[r] and hi > low and lo < high]
    assert overlapping == [1, 3, 4]
    assert (overlapping[0], overlapping[-1] + 1) == (1, 5)
    assert [r for r, (lo, hi) in enumerate(bounds) if hi > low and lo < high] == [1, 2, 3, 4]
    f = Figure(744, 'Consecutive cuts define contiguous intervals',
               'Cuts 0,8,18,26,35,46,56 define six adjacent half-open intervals. '
               'Each rank ends exactly where the next starts. Rank 2 is excluded but retains '
               'its interval 18 inclusive to 26 exclusive. Query 9 inclusive to 36 exclusive '
               'overlaps ranks 1 through 4, giving rank window 1 inclusive to 5 exclusive. '
               'Selected traversal skips rank 2. The green query overlay crosses amber result rows.')
    f.heading(32, 42, 'Consecutive cuts define contiguous intervals')
    f.text(32, 78, 'Cuts: 0, 8, 18, 26, 35, 46, 56. Each rank ends where the next starts.',
           20, color=MUTED)
    x = lambda key: 176 + key * 14
    f.text(50, 126, 'Key', 20, 'bold')
    f.path('M176,121 H976', arrow=True, color=INK)
    for key in range(0, 56, 5):
        line(f, x(key), 115, x(key), 127, MUTED, width=1.3)
        f.text(x(key), 108, key, 16, color=MUTED, anchor='middle')
    # Candidate rows first. The query overlay must cross them, not be hidden
    # behind their opaque fill: its x axis and their rank axis are independent.
    for rank in range(1, 5):
        f.rect(32, 222 + rank * 62 - 26, 960, 52, AMBER_FILL, stroke='none', radius=4)
    # Shared-cut guides align each interval's open end with the next start.
    for rank, cut in enumerate(cuts[1:-1]):
        line(f, x(cut), 222 + rank * 62, x(cut), 222 + (rank + 1) * 62,
             '#b7c3d2', width=1.3, dash=True)
    f.items.append(f'<rect x="{x(low)}" y="138" width="{x(high)-x(low)}" '
                   'height="428" fill="#69bd80" fill-opacity="0.13"/>')
    line(f, x(low), 138, x(low), 568, GREEN, dash=True)
    line(f, x(high), 138, x(high), 568, GREEN, dash=True)
    f.text((x(low) + x(high)) / 2, 155, 'query [9, 36)', 21, 'bold', GREEN, anchor='middle')
    line(f, x(low), 174, x(high), 174, GREEN, width=2.4)
    circle(f, x(low), 174, 5, GREEN)
    circle(f, x(high), 174, 5, 'white', GREEN)
    for rank, (lo, hi) in enumerate(bounds):
        y = 222 + rank * 62
        f.text(50, y + 6, f'rank {rank}', 21, 'bold')
        line(f, 176, y, 976, y, RULE, width=1.4)
        color = '#8d99a8' if not eligible[rank] else BLUE
        line(f, x(lo), y, x(hi), y, color, width=4, dash=not eligible[rank])
        circle(f, x(lo), y, 6, color)
        circle(f, x(hi), y, 6, 'white', color)
        f.text(x(lo), y - 15, f'[{lo}, {hi})', 18, 'bold', color)
        if not eligible[rank]:
            f.text(x(lo), y + 22, 'excluded; cuts unchanged', 17, color=MUTED)
    f.path('M1002,258 H1012 Q1020,258 1020,266 V369 Q1020,377 1028,377 '
           'Q1020,377 1020,385 V488 Q1020,496 1012,496 H1002', color=AMBER)
    f.text(1044, 337, 'Overlapping ranks', 22, 'bold', AMBER)
    f.text(1044, 374, '[1, 5)', 27, 'bold', AMBER)
    f.lines(1044, 411, ['ranks 1–4', 'rank 2 excluded'], 18, 27, color=MUTED)
    for key in (low, high):
        f.text(x(key), 592, key, 17, 'bold', GREEN, anchor='middle')
    f.text(32, 650, 'Eligibility', 20, 'bold')
    for rank, bit in enumerate(eligible):
        left = 200 + rank * 94
        f.text(left + 42, 617, f'rank {rank}', 16, color=MUTED, anchor='middle')
        f.rect(left, 628, 84, 38, '#f1f4f8' if bit else '#e8edf2', radius=3)
        f.text(left + 42, 654, bit, 21, color=INK if bit else MUTED, anchor='middle', mono=True)
    line(f, 32, 689, 1248, 689, RULE)
    f.text(32, 724, 'Selection skips a rank without moving its cuts.',
           20, color=MUTED)
    f.save(OUT / 'ranges.svg')


def phases():
    f = Figure(650, 'Expose a dependency while retaining one version',
               'Checkpoint search identifies sixteen local fields; a bound numeric source proposes '
               'an endpoint bracket using once-per-version derived state. The owner may prefetch '
               'or wait before finishing. Metadata, eligibility, backing and derived state remain '
               'one version. Local windows are hints, not complete read grants.')
    f.heading(32, 42, 'Expose the next dependency; retain one data version')
    f.text(32, 82, 'Ready arithmetic stays inline. The caller decides whether an unresolved read warrants a Loom wait.',
           20, color=MUTED)
    for y, labels, prepare, finish in (
        (146, ['Checkpointed', 'G16 source'], 'search checkpoints', ['compare or prefix', 'then select']),
        (346, ['Bound numeric', 'source'], 'propose bracket', ['verify bracket', 'repair if needed'])):
        f.lines(32, y + 49, labels, 21, 27, weight='bold')
        f.rect(236, y, 202, 124, BLUE_FILL, stroke='#8dbce7', radius=6)
        f.text(337, y + 34, 'prepare', 22, 'bold', anchor='middle')
        f.text(337, y + 76, prepare, 17, anchor='middle')
        f.rect(488, y, 285, 124, '#f1f4f8', radius=6)
        f.text(630.5, y + 30, 'local_window', 19, 'bold', anchor='middle', mono=True)
        if y == 146:
            for lane in range(16):
                f.rect(527 + lane * 13, y + 49, 12, 28, BLUE_FILL, stroke='#8dbce7')
            for at in (511, 750):
                f.text(at, y + 70, '…', 20, color=MUTED, anchor='middle')
            f.text(630.5, y + 107, '16 local fields', 17, color=MUTED, anchor='middle')
        else:
            for lane, label in enumerate(['…', 'E0', 'E1', 'E2', 'En', '…']):
                f.rect(511 + lane * 40, y + 49, 38, 28, BLUE_FILL, stroke='#8dbce7')
                f.text(530 + lane * 40, y + 69, label, 17, anchor='middle')
            f.text(630.5, y + 107, 'endpoint bracket', 17, color=MUTED, anchor='middle')
        f.rect(820, y, 180, 124, AMBER_FILL, stroke='#c49b49', radius=6, dash=True)
        f.text(910, y + 34, 'caller policy', 20, 'bold', anchor='middle')
        f.lines(910, y + 73, ['prefetch', 'optional Loom wait'], 17, 25, anchor='middle')
        f.rect(1048, y, 200, 124, GREEN_FILL, stroke='#a5cd9c', radius=6)
        f.text(1148, y + 34, 'finish', 22, 'bold', anchor='middle')
        f.lines(1148, y + 73, finish, 17, 25, anchor='middle')
        for start, stop in ((438, 488), (773, 820), (1000, 1048)):
            f.path(f'M{start+5},{y+62} H{stop-7}', arrow=True)
    f.rect(236, 292, 202, 36, BLUE_FILL, stroke='#8dbce7', radius=5)
    f.text(337, 316, 'bind once per version', 16, anchor='middle')
    f.path('M337,330 V342', arrow=True, dash=True, color=BLUE)
    f.rect(32, 530, 1216, 60, '#f1f4f8', radius=6)
    f.text(56, 567, 'One retained version', 21, 'bold')
    f.text(320, 567, 'boundaries + eligibility + backing + derived search state', 20)
    f.text(32, 625, 'Hint windows are not read grants; selection and bracket repair can read elsewhere.',
           20, color=MUTED)
    f.save(OUT / 'phases.svg')


def sparse():
    f = Figure(560, 'Checkpoint spacing differs from native read width',
               'Both rows contain 64 size fields in four native 16-field windows. '
               'G16 stores region checkpoints at ranks 0,16,32,48; G64 stores one at rank 0. '
               'The start of rank 47 uses checkpoint 32 plus 15 fields at G16, '
               'or checkpoint 0 plus 16 plus 16 plus 15 fields at G64. '
               'Larger spacing saves checkpoint bytes and writes but needs more prefix work. '
               'The independent G64 option can read all four admitted native windows.')
    f.heading(32, 42, 'Checkpoint spacing differs from native read width')
    f.text(32, 80, 'Same 64 sizes. Four native windows of 16 fields; the owner chooses checkpoint spacing.',
           20, color=MUTED)
    for y, spacing, marks in ((150, 16, [0, 16, 32, 48]), (318, 64, [0])):
        f.text(32, y + 42, f'G{spacing}', 26, 'bold')
        for i in range(4):
            x = 210 + i * 250
            f.rect(x, y + 20, 226, 62, GREEN_FILL, stroke='#a5cd9c', radius=5)
            f.text(x + 113, y + 45, f'ranks {i*16}..{i*16+15}', 20, anchor='middle')
            f.text(x + 113, y + 69, '16 readable fields', 17, color=MUTED, anchor='middle')
            if i * 16 in marks:
                f.rect(x, y - 27, 226, 32, BLUE_FILL, stroke='#8dbce7', radius=4)
                f.text(x + 113, y - 4, f'checkpoint at {i*16}', 18, anchor='middle')
        equation = ('start(47) = C[2] + prefix of 15 fields' if spacing == 16 else
                    'start(47) = C[0] + 16 fields + 16 fields + 15 fields')
        f.text(210, y + 116, equation, 20, color=INK)
    line(f, 32, 474, 1248, 474, RULE)
    f.text(32, 510, 'Fewer checkpoints save bytes and writes; point reads can need more local work.', 20, color=MUTED)
    f.text(32, 541, 'G64 independent execution may read all four admitted windows. It is a workload choice.', 20, color=MUTED)
    f.save(OUT / 'sparse.svg')


if __name__ == '__main__':
    encodings()
    ranges()
    phases()
    sparse()
