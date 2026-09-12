#!/usr/bin/env python3
"""Render Ikea's documentation figures. Run on Linux with the pinned toolchain.

From the repository root: python3 ikea/docs/images/generate.py
On macOS/OrbStack, prefix that command with orb -m ubuntu.
Wire data is read from the current C++ implementation; generated SVGs are committed.
"""

from html import escape
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "ikea/docs/seriespack/images"
BUILD = ROOT / "build/diagrams"
INK = "#233047"
MUTED = "#586579"
RULE = "#d9e0e9"
PALETTE = ["#98c9f3", "#ffdb79", "#a9e8a2", "#c7b9f3",
           "#f4aecb", "#97e2df", "#ffbd87", "#a7b5ed"]
BLUE, AMBER, GREEN, PURPLE = PALETTE[:4]


def wire_data():
    """Use the actual v1 maps and point writer, not a second Python wire law."""
    BUILD.mkdir(parents=True, exist_ok=True)
    source = BUILD / "wire_data.cpp"
    source.write_text(r'''
#include <ikea/seriespack/detail/point.h>
#include <iostream>
namespace sp = ikea::seriespack;
template<unsigned R> void emit() {
    using F = sp::format<R, sp::geometry::striped>;
    std::cout << R << ' ' << F::tile_rows << ' ' << F::tile_bytes;
    std::array<std::string, F::tile_bytes / 4> bits;
    for (unsigned g = 0; g < F::tile_rows / 32; ++g)
        for (unsigned b = 0; b < R; ++b)
            bits[sp::detail::tail_bit<R>(g, b)] =
                std::string(1, 'A' + g) + std::to_string(b);
    for (auto& bit : bits) std::cout << ' ' << bit;
    std::cout << '\n';
}
int main() {
    emit<1>(); emit<2>(); emit<3>(); emit<4>(); emit<5>(); emit<6>(); emit<7>();
    using L = sp::format<11, sp::geometry::local>;
    std::array<std::uint8_t, L::tile_bytes> bytes{};
    for (unsigned i = 0; i < 8; ++i) sp::detail::put_payload<L>(bytes.data(), i, i + 8);
    std::cout << "local";
    for (auto b : bytes) std::cout << ' ' << unsigned(b);
    std::cout << '\n';
    using P = sp::format<20, sp::geometry::striped, 8>;
    using Parent = sp::format<12, sp::geometry::local, 8>;
    using Child = sp::format<4, sp::geometry::striped>;
    static_assert(P::tile_rows == 64 && P::tile_bytes == 96);
    static_assert(Parent::tile_rows == 8 && Child::tile_rows == 64);
}
''')
    compiler = "clang++-21"
    version = subprocess.check_output([compiler, "--version"], text=True)
    if "version 21.1.8" not in version:
        raise RuntimeError("Use the Clang 21.1.8 toolchain pinned in BUILDING.md")
    binary = BUILD / "wire_data"
    subprocess.run([compiler, "-std=c++23", "-O0", "-I", str(ROOT / "ikea/include"),
                    str(source), "-o", str(binary)], check=True)
    lines = subprocess.check_output([str(binary)], text=True).splitlines()
    tables = []
    for line in lines[:7]:
        width, rows, size, *bits = line.split()
        tables.append((int(width), int(rows), int(size), bits))
    local = [int(value) for value in lines[7].split()[1:]]
    if local != [1] * 8 + [0xaa, 0xcc, 0xf0]:
        raise RuntimeError("Local11 example changed; revisit the figure and its prose")
    return tables, local


class Figure:
    def __init__(self, height, title, description):
        self.height = height
        self.items = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1280" '
                      f'height="{height}" viewBox="0 0 1280 {height}" role="img" '
                      'aria-labelledby="title description">',
                      f'<title id="title">{escape(title)}</title>',
                      f'<desc id="description">{escape(description)}</desc>',
                      '<defs><marker id="arrow" markerWidth="8" markerHeight="8" '
                      'refX="7" refY="4" orient="auto" markerUnits="userSpaceOnUse">'
                      f'<path d="M0,0 L8,4 L0,8" fill="{MUTED}"/></marker></defs>',
                      '<rect width="1280" height="100%" fill="white"/>',
                      '<g font-family="Arial, Helvetica, sans-serif" '
                      f'fill="{INK}" font-size="21">']

    def text(self, x, y, value, size=21, weight="normal", color=INK, anchor="start", mono=False):
        family = ' font-family="DejaVu Sans Mono, monospace"' if mono else ""
        self.items.append(f'<text x="{x}" y="{y}" font-size="{size}" '
                          f'font-weight="{weight}" fill="{color}" text-anchor="{anchor}"'
                          f'{family}>{escape(str(value))}</text>')

    def lines(self, x, y, values, size=21, gap=28, **kwargs):
        for i, value in enumerate(values):
            self.text(x, y + i * gap, value, size=size, **kwargs)

    def rect(self, x, y, width, height, fill="white", stroke=RULE, radius=0, dash=False):
        extra = ' stroke-dasharray="6 5"' if dash else ""
        self.items.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" '
                          f'rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="1.5"{extra}/>')

    def path(self, d, arrow=False, dash=False, color=MUTED):
        extra = ' marker-end="url(#arrow)"' if arrow else ""
        if dash:
            extra += ' stroke-dasharray="6 5"'
        self.items.append(f'<path d="{d}" fill="none" stroke="{color}" '
                          f'stroke-width="1.8"{extra}/>')

    def heading(self, x, y, value):
        self.text(x, y, value, 26, "bold")

    def card(self, x, y, width, height, title, lines=(), fill="#f6f8fb", title_size=23):
        # Cards share horizontal padding and text baselines. Use 104/132/160
        # pixels for one/two/three body lines, keeping 24 pixels below the text.
        self.items.append('<g data-layout="card">')
        self.rect(x, y, width, height, fill, radius=8)
        self.text(x + 24, y + 40, title, title_size, "bold")
        self.lines(x + 24, y + 76, lines, 20, 28, color=MUTED)
        self.items.append('</g>')

    def save(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(self.items + ["</g></svg>"]) + "\n")


def stripes(tables):
    f = Figure(804, "SeriesPack striped residuals, R = 1 through 7",
               "Each colored cell names a value bit from a 32-row group. Each grid row is one "
               "byte lane of a 32-byte stripe. Columns are stored-byte bits zero through seven. "
               "The exact v1 maps are generated from tail_bit<R>.")
    f.heading(32, 42, "Striped residuals")
    f.text(1248, 42, "k_tail = R = (K − H) mod 8", 22, anchor="end", color=MUTED)
    f.text(32, 80, "One byte lane shown · each row is a 32-byte stripe · stored bits run 0 → 7", 22, color=MUTED)

    def panel(data, x, y):
        width, rows, size, bits = data
        f.text(x, y, f"R = {width}", 25, "bold")
        f.text(x, y + 32, f"{rows} rows · {size} bytes", 20, color=MUTED)
        step = 32
        grid_x, grid_y = x + 32, y + 80
        for b in range(8):
            f.text(grid_x + step * (b + .5), grid_y - 16, b, 18, color=MUTED, anchor="middle")
        for s in range(len(bits) // 8):
            row_y = grid_y + s * 36
            f.text(x + 20, row_y + 23, f"S{s}", 18, anchor="end", mono=True)
            for b, token in enumerate(bits[8 * s:8 * s + 8]):
                f.rect(grid_x + b * step, row_y, step, 32, PALETTE[ord(token[0]) - 65], "white")
                f.text(grid_x + step * (b + .5), row_y + 23, token, 20, anchor="middle", mono=True)
    for i, data in enumerate(tables[:4]):
        panel(data, 32 + i * 312, 124)
    f.path("M32,336 H1248", color=RULE)
    for i, data in enumerate(tables[4:]):
        panel(data, 32 + i * 312, 380)
    f.text(968, 380, "Read a cell", 23, "bold")
    f.rect(968, 404, 32, 32, BLUE, "white")
    f.text(984, 427, "A0", 20, anchor="middle", mono=True)
    f.text(1016, 427, "group A, bit 0", 20)
    f.text(968, 476, "Original row groups", 19, color=MUTED)
    for g in range(8):
        x, y = 968 + (g // 4) * 144, 496 + (g % 4) * 40
        f.rect(x, y, 26, 26, PALETTE[g], "white")
        f.text(x + 13, y + 20, chr(65 + g), 18, anchor="middle")
        f.text(x + 38, y + 20, f"{32 * g}–{32 * g + 31}", 18, color=MUTED)
    f.path("M32,736 H1248", color=RULE)
    f.text(32, 772, "For lane l, A…H mean rows l, 32+l, …, 224+l. Byte offset = 32s+l. Sizes shown are tail-only.", 21, color=MUTED)
    f.save(OUT / "striped-tiles.svg")


def local_and_placement(local):
    f = Figure(704, "Local residual transpose and separated-head placement",
               "Local11 values eight through fifteen store eight body bytes 01, then bitplane bytes aa cc f0. "
               "For the ordinary twenty-bit example, an eight-bit head is separate from the twelve-bit payload. "
               "Each 64-row placement has 64 body bytes, 32 stripe bytes, 64 head bytes and a 32-byte gap; "
               "both plane strides are 192 bytes.")
    f.path("M640,32 V672", color=RULE)
    f.heading(32, 42, "Local: transpose the low bits")
    f.text(32, 80, "K = 11, H = 0 · 8 values: 8…15", 22, color=MUTED)
    f.text(32, 112, "payload = 8 body bits + 3 residual bits", 21)
    f.text(72, 160, "row", 18, color=MUTED, anchor="end")
    for b in range(3):
        f.text(104 + b * 32, 160, b, 18, color=MUTED, anchor="middle")
    for row in range(8):
        f.text(72, 199 + row * 32, row, 20, anchor="end", mono=True)
        for b in range(3):
            f.rect(88 + b * 32, 176 + row * 32, 32, 32, PALETTE[row], "white")
            f.text(104 + b * 32, 199 + row * 32, (row >> b) & 1, 20, anchor="middle", mono=True)
    f.text(236, 280, "transpose", 18, anchor="middle", color=MUTED)
    f.path("M200,304 H272", arrow=True)
    f.text(448, 208, "original row → bit in byte", 19, anchor="middle", color=MUTED)
    for row in range(8):
        f.text(336 + 32 * row, 240, row, 18, anchor="middle", color=MUTED)
    for b in range(3):
        f.text(304, 279 + b * 32, b, 20, anchor="end", color=MUTED)
        for row in range(8):
            f.rect(320 + row * 32, 256 + b * 32, 32, 32, PALETTE[row], "white")
            f.text(336 + row * 32, 279 + b * 32, (row >> b) & 1, 20, anchor="middle", mono=True)
        f.text(592, 279 + b * 32, f"{local[8 + b]:02x}", 22, mono=True)
    f.text(448, 392, "One bitplane byte per output row.", 19, anchor="middle", color=MUTED)
    f.text(32, 480, "The 11-byte tile, in memory order", 23, "bold")
    for i, byte in enumerate(local):
        x = 32 + i * 52
        f.rect(x, 512, 52, 48, PALETTE[i] if i < 8 else "#e8edf5", "white")
        f.text(x + 26, 543, f"{byte:02x}", 22, anchor="middle", mono=True)
    f.text(240, 600, "8 row-ordered body bytes", 20, anchor="middle", color=MUTED)
    f.lines(526, 600, ["3 residual", "bitplane bytes"], 20, 28, anchor="middle", color=MUTED)
    f.text(32, 672, "Bit 0 is shown first; byte values are hexadecimal.", 19, color=MUTED)

    f.heading(672, 42, "Separate the head, place the planes")
    f.text(672, 80, "K = 20, H = 8 · payload = 12 bits", 22, color=MUTED)
    logical = [("head", "bits 19…12", 230.4, BLUE), ("body", "bits 11…4", 230.4, GREEN),
               ("tail", "bits 3…0", 115.2, AMBER)]
    x = 672
    for title, bits, width, color in logical:
        f.rect(x, 120, width, 80, color, "white")
        f.text(x + width / 2, 152, title, 22, "bold", anchor="middle")
        f.text(x + width / 2, 180, bits, 18, anchor="middle")
        x += width
    f.path("M902.4,216 V224 H1248 V216", color=MUTED)
    f.text(1075.2, 256, "12-bit striped payload", 21, anchor="middle", color=MUTED)
    f.text(672, 304, "64-row tiles · byte offsets in a shared partition", 21, "bold")
    blocks = [(64, "body", GREEN), (32, "tail", AMBER), (64, "head", BLUE), (32, "gap", "#eef0f4")]
    for tile, y in enumerate((360, 488)):
        x = 672
        offset = 192 * tile
        for size, title, color in blocks:
            width = size * 3
            f.text(x, y - 16, offset, 18, color=MUTED)
            f.rect(x, y, width, 80, color, "white")
            f.text(x + width / 2, y + 32, title, 22, "bold", anchor="middle")
            f.text(x + width / 2, y + 60, f"{size} B", 20, anchor="middle")
            x += width
            offset += size
        f.text(x, y - 16, offset, 18, anchor="end", color=MUTED)
    f.lines(672, 608, ["Payload origin: 0 · head origin: 96 bytes",
                        "Both plane strides: 192 bytes",
                        "The gap remains available to the owner or a sibling."], 21, 32, color=MUTED)
    f.save(OUT / "local-and-placement.svg")


def architecture():
    f = Figure(664, "Ikea: independent choices and prepared operations",
               "Under a fixed logical container contract, admissible logical expressions, physical placement "
               "and execution recipes are prepared once. Ordinary read and mutation calls reuse that binding. "
               "The integration model allows different segments to keep different representations.")
    f.heading(32, 42, "Ikea · independent choices, ordinary calls")
    f.text(32, 80, "SeriesPack example", 22, color=MUTED)
    f.text(900, 80, "REPEATED EXECUTION", 18, "bold", color=MUTED)
    f.rect(32, 104, 808, 56, "#f0f4f8", radius=6)
    f.text(436, 140, "Admissible choices under one logical contract", 25, "bold", anchor="middle")
    f.card(32, 208, 248, 132, "Logical expression", ["Fields and bit joins", "Nested substitution"], "#edf8f6", 22)
    f.card(312, 208, 248, 132, "Physical placement", ["Geometry and planes", "Strides and owners"], "#eef5fc", 22)
    f.card(592, 208, 248, 132, "Execution recipe", ["Fused · inline · CPS", "ISA, carrier and grain"], "#f3effc", 22)
    for x in (156, 436, 716):
        f.path(f"M{x},160 V200", arrow=True, color=RULE)
    f.path("M156,340 V376 H716 V340")
    f.path("M436,340 V400", arrow=True)
    f.card(288, 408, 296, 104, "Admit + bind once", ["Selected realization"], title_size=23)
    f.path("M868,104 V512", dash=True, color=RULE)
    f.card(900, 208, 348, 132, "Read or mutate", ["Ordinary operation call", "Borrowed data and local outputs"], title_size=23)
    f.path("M584,460 H884 V274 H892", arrow=True)
    f.text(728, 442, "prepared operation", 20, anchor="middle", color=MUTED)
    f.path("M1248,274 H1264 V372 H932 V348", arrow=True)
    f.lines(1072, 402, ["Reuse the admitted binding", "while its mapping remains valid"],
            20, 27, anchor="middle", color=MUTED)
    f.rect(32, 552, 1216, 80, "#fafbfc", radius=6)
    f.text(56, 584, "Integration model", 21, "bold")
    f.text(56, 612, "Same field contract across segments", 20, color=MUTED)
    f.rect(432, 570, 384, 44, "#eef5fc", radius=4)
    f.text(624, 599, "Segment A: field X · Local", 20, anchor="middle")
    f.rect(840, 570, 384, 44, "#edf8f6", radius=4)
    f.text(1032, 599, "Segment B: field X · striped", 20, anchor="middle")
    f.save(ROOT / "ikea/docs/images/architecture.svg")


def substitution():
    f = Figure(624, "Substitute a nested child while preserving the parent contract",
               "A 12-bit value keeps its eight-bit head from the parent source while its four-bit residual "
               "is replaced by a complete child expression backed by Striped64. The parent head still tiles "
               "eight rows at a time. The retired residual is not used by reads, writes or effects.")
    f.heading(32, 42, "A different child, the same 12-bit contract")
    f.path("M640,88 V592", color=RULE)
    f.text(32, 96, "BEFORE", 18, "bold", color=MUTED)
    f.text(672, 96, "AFTER SUBSTITUTION", 18, "bold", color=MUTED)
    for offset in (0, 640):
        f.rect(104 + offset, 120, 288, 80, BLUE, "white")
        f.rect(392 + offset, 120, 144, 80, AMBER, "white")
        f.text(248 + offset, 152, "8-bit head", 24, "bold", anchor="middle")
        f.text(248 + offset, 180, "bits 4…11", 19, anchor="middle")
        f.text(464 + offset, 152, "4-bit tail", 22, "bold", anchor="middle")
        f.text(464 + offset, 180, "bits 0…3", 19, anchor="middle")
    f.card(32, 248, 272, 132, "Parent head", ["Original source", "8-row tiles"], "#eef5fc")
    f.card(336, 248, 272, 132, "Parent residual", ["Original source", "Local8"], "#fff8e8")
    f.path("M248,200 V224 H168 V240", arrow=True)
    f.path("M464,200 V224 H472 V240", arrow=True)
    f.card(672, 248, 272, 132, "Parent head", ["Same source", "8-row tiles"], "#eef5fc")
    f.card(976, 248, 272, 132, "Child expression", ["New source · Striped64", "Complete 4-bit value"], "#fff8e8", 23)
    f.path("M888,200 V224 H808 V240", arrow=True)
    f.path("M1104,200 V224 H1112 V240", arrow=True)
    f.text(32, 428, "Storage over 64 original rows", 22, "bold")
    f.text(672, 428, "Storage over the same 64 rows", 22, "bold")
    for offset in (0, 640):
        f.text(32 + offset, 475, "head", 20, color=MUTED)
        f.text(32 + offset, 531, "tail", 20, color=MUTED)
        for tile in range(8):
            x = 100 + offset + tile * 64
            f.rect(x, 450, 60, 36, BLUE, "white")
            f.text(x + 30, 475, "8", 20, anchor="middle")
        if offset == 0:
            for tile in range(8):
                x = 100 + tile * 64
                f.rect(x, 506, 60, 36, AMBER, "white")
                f.text(x + 30, 531, "8", 20, anchor="middle")
        else:
            f.rect(100 + offset, 506, 508, 36, AMBER, "white")
            f.text(354 + offset, 531, "64 rows · one child tile", 20, anchor="middle")
    f.text(32, 592, "Numbers in tiles are row counts, not byte sizes.", 20, color=MUTED)
    f.text(672, 592, "Retired residual: no reads, writes or effects.", 20, color=MUTED)
    f.save(OUT / "substitution.svg")


def execution():
    f = Figure(552, "Shared native bodies, inline or CPS execution",
               "The rank_signed and selected_rank_sum bodies from pipeline.cpp are reused in inline and CPS "
               "execution. Native values travel with original coordinates and activity. "
               "CPS completion returns to the owning driver; scheduler suspension is outside the stages.")
    f.heading(32, 42, "Shared native bodies, different invocation")
    f.text(32, 80, "pipeline.cpp · raw 8-bit patterns → signed-order ranks → selected sum of ranks", 22, color=MUTED)
    for name, y, descriptions in [("INLINE", 120, ["Native rank transform", "Cutoff and rank accumulation"]),
                                  ("CPS", 324, ["stage wrapper", "stage wrapper"])]:
        f.text(32, y + 60, name, 18, "bold", color=MUTED)
        f.card(200, y, 352, 104, "rank_signed", [descriptions[0]], "#eef5fc")
        f.card(624, y, 352, 104, "selected_rank_sum", [descriptions[1]], "#edf8f6")
        f.path(f"M128,{y + 52} H192", arrow=True)
        f.path(f"M552,{y + 52} H616", arrow=True)
        f.path(f"M976,{y + 52} H1032", arrow=True)
    f.text(1050, 180, "return to caller", 22)
    f.path("M32,248 H1248", color=RULE)
    f.text(200, 292, "Same authored bodies inside the wrappers", 20, color=MUTED)
    f.lines(1050, 368, ["completion", "returns to driver"], 22, 30)
    f.path("M32,460 H1248", color=RULE)
    f.lines(32, 496, ["Native values travel with original row coordinates and activity.",
                     "The owner may suspend after return to the driver."], 21, 30, color=MUTED)
    f.save(OUT / "execution.svg")


def lifecycle():
    f = Figure(768, "Retained-lease mutation: local work and owner progress",
               "The owner acquires a lease, admits and reserves the operation, invokes bounded work and "
               "receives a completed work frontier. Ikea's invocation performs local writes, contributions "
               "and issued-byte coverage. The owner either retains live state and validates before resuming "
               "or seals data, effects and summary validity and coordinates visibility. Dirty cancellation "
               "requires owner resolution.")
    f.heading(32, 42, "Retained-lease teaching adapter")
    f.text(32, 80, "Owner progress surrounds complete Ikea invocations", 22, color=MUTED)
    f.text(32, 128, "OWNER", 18, "bold", color=MUTED)
    f.card(32, 152, 268, 132, "Acquire + prepare", ["Lease · admission", "Effect capacity reserved"], title_size=23)
    f.card(348, 152, 268, 132, "Invoke", ["Bounded local", "work unit"], title_size=23)
    f.card(664, 152, 268, 132, "Returned frontier", ["Completed work", "and effects recorded"], title_size=23)
    f.path("M300,218 H340", arrow=True)
    f.card(980, 152, 268, 132, "Seal + coordinate", ["Data + summary validity", "Effects + publication"], title_size=23)
    f.path("M798,152 V112 H1114 V144", arrow=True)
    f.text(956, 100, "operation complete", 19, anchor="middle", color=MUTED)
    f.card(348, 332, 584, 104, "IKEA INVOCATION", ["Local writes · contributions · byte coverage"], "#eef5fc", 18)
    f.path("M482,284 V324", arrow=True)
    f.path("M798,332 V292", arrow=True)
    f.text(32, 372, "Native kernels", 23, "bold")
    f.lines(32, 408, ["No scheduler boundary", "inside the work unit"], 20, 28, color=MUTED)
    f.card(664, 500, 584, 160, "Retain across wait or rotation", [
        "Lease · views/binding · mapping generation",
        "Input/selection · plan · next row",
        "Unfinished summary · effects"], "#f3effc", 23)
    f.path("M932,248 H956 V476 H956 V492", arrow=True)
    f.text(980, 476, "after return", 20, color=MUTED)
    f.path("M664,580 H324 V308 H372 V292", arrow=True)
    f.lines(32, 560, ["Validate mapping/generation", "and cancellation, then resume"], 20, 28, color=MUTED)
    f.lines(32, 704, ["Cancellation after writes needs owner resolution.",
                     "Rejected calls leave their data, summary and effects unchanged; earlier chunks remain."],
            21, 32, color=MUTED)
    f.save(ROOT / "ikea/docs/images/mutation-lifetime.svg")


if __name__ == "__main__":
    tables, local = wire_data()
    stripes(tables)
    local_and_placement(local)
    architecture()
    substitution()
    execution()
    lifecycle()
    print("Rendered six documentation figures from the current SeriesPack wire data.")
