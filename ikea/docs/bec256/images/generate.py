#!/usr/bin/env python3
"""Generate Bec256's representation figures and check their worked examples.

From the repository root: orb -m ubuntu python3 ikea/docs/bec256/images/generate.py
Temporary readers and previews belong in ignored build/diagrams/bec256/.
"""

from pathlib import Path
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[4]
BUILD = ROOT / "build/diagrams/bec256"
OUT = Path(__file__).parent
STYLE = runpy.run_path(str(ROOT / "ikea/docs/images/generate.py"))
Figure = STYLE["Figure"]
BLUE, GREEN, MUTED, RULE = (STYLE[k] for k in ("BLUE", "GREEN", "MUTED", "RULE"))


def wire_examples():
    BUILD.mkdir(parents=True, exist_ok=True)
    source = BUILD / "wire_examples.cpp"
    source.write_text(r'''
#include <ikea/bec256/detail/scalar.h>
#include <iostream>
int main() {
    namespace detail = ikea::bec256::detail;
    std::array<std::uint8_t, 32> plain{}, decoded{};
    std::array<std::uint8_t, 64> body{};
    auto emit = [&](unsigned population) {
        const auto size = detail::encode_scalar(plain.data(), population, body.data());
        detail::decode_scalar(body.data(), population, decoded.data());
        if (decoded != plain) return false;
        std::cout << size;
        for (unsigned i = 0; i != size; ++i) std::cout << ' ' << unsigned(body[i]);
        std::cout << '\n';
        return true;
    };
    plain[3] = 0x20;
    if (!emit(1)) return 1;
    plain[2] = 0x24;
    if (!emit(3)) return 1;
    plain.fill(0x0f);
    if (!emit(128)) return 1;
    for (unsigned c = 0; c != 9; ++c) {
        std::cout << unsigned(detail::byte_width[c]);
        for (unsigned r = 0; r != detail::choices[c]; ++r) {
            const auto value = detail::codes.value[detail::byte_base[c] + r];
            if (detail::codes.rank[value] != r) return 1;
            std::cout << ' ' << unsigned(value);
        }
        std::cout << '\n';
    }
}
''')
    compiler = "clang++-21"
    version = subprocess.check_output([compiler, "--version"], text=True)
    if "version 21.1.8" not in version:
        raise RuntimeError("Use the compiler pinned in BUILDING.md")
    binary = BUILD / "wire_examples"
    subprocess.run([compiler, "-std=c++23", "-O0", "-I", str(ROOT / "ikea/include"),
                    str(source), "-o", str(binary)], check=True)
    lines = [list(map(int, line.split())) for line in
             subprocess.check_output([str(binary)], text=True).splitlines()]
    if lines[0] != [1, 0xa7] or lines[1] != [3, 0x3f, 0xb2, 0x02]:
        raise RuntimeError("A worked body changed; revisit the guide and figures")
    if lines[2][0] != 47 or lines[2][-1] & 0xc0:
        raise RuntimeError("The maximum-size example changed")
    widths = [row[0] for row in lines[3:]]
    patterns = [row[1:] for row in lines[3:]]
    if widths != [0, 3, 5, 6, 7, 6, 5, 3, 0]:
        raise RuntimeError("The byte rank widths changed")
    if [len(row) for row in patterns] != [1, 8, 28, 56, 70, 56, 28, 8, 1]:
        raise RuntimeError("The byte populations changed")
    if patterns[1].index(0x20) != 5 or patterns[2].index(0x24) != 12:
        raise RuntimeError("A worked rank changed")
    return lines[0][1], patterns[1]


def structure():
    f = Figure(816, "Bec256's fixed division of bit positions",
               "The whole block covers positions zero through 255 and has supplied population p. "
               "Its left half covers zero through 127 with encoded count q; its right half covers "
               "128 through 255 with inferred count p minus q. Repeating the same division in both "
               "halves produces four regions of 64 positions, eight of 32, sixteen of 16 and "
               "thirty-two byte-sized leaves of eight positions. The tree boundaries are fixed. "
               "The split fields recover every byte's population; byte ranks then select its pattern.")
    f.heading(32, 42, "One fixed tree, with counts recovered from parent to child")
    f.text(32, 80, "Horizontal position follows bit index throughout · lower positions are always on the left",
           22, color=MUTED)
    origin, extent = 224, 1024
    rows = [(128, 64), (248, 80), (404, 48), (496, 48), (588, 40), (672, 48)]
    labels = [("Whole block", "256 positions"), ("Two halves", "128 each"),
              ("Four regions", "64 each"), ("Eight regions", "32 each"),
              ("16 regions", "16 each"), ("32 byte leaves", "8 each")]
    for depth, (y, height) in enumerate(rows):
        count = 1 << depth
        step = extent / count
        f.text(32, y + 26, labels[depth][0], 23, "bold")
        f.text(32, y + 54, labels[depth][1], 20, color=MUTED)
        if depth:
            previous_y, previous_height = rows[depth - 1]
            # The note between rows one and two has its own clear horizontal band.
            join_y = y - 24 if depth == 2 else (previous_y + previous_height + y) / 2
            for parent in range(count // 2):
                centre = origin + (2 * parent + 1) * step
                left, right = centre - step / 2, centre + step / 2
                if depth == 2:
                    start = 382
                    f.path(f"M{centre},{start} V{join_y}")
                else:
                    f.path(f"M{centre},{previous_y + previous_height} V{join_y}")
                f.path(f"M{left},{y - 3} V{join_y} H{right} V{y - 3}")
        for region in range(count):
            x = origin + region * step + 2
            f.rect(x, y, step - 4, height,
                   GREEN if depth == 5 else (BLUE if depth == 1 else "#eef5fc"),
                   "white", radius=3)
            centre = x + (step - 4) / 2
            n = 256 // count
            if depth == 0:
                f.text(centre, y + 26, "Positions 0–255", 23, "bold", anchor="middle")
                f.text(centre, y + 52, "population p · supplied separately", 22, anchor="middle")
            elif depth == 1:
                f.text(centre, y + 28, f"Positions {region * n}–{(region + 1) * n - 1}",
                       23, "bold", anchor="middle")
                f.text(centre, y + 60, "q · encode left count" if region == 0 else
                       "p − q · infer right count", 24, anchor="middle")
            elif depth in (2, 3):
                f.text(centre, y + 31, f"{region * n}–{(region + 1) * n - 1}",
                       21, anchor="middle")
            elif depth == 5:
                f.text(centre, y + 31, region, 19, anchor="middle")
    f.text(736, 366, "Repeat in each half, using that region's population as p.", 22,
           anchor="middle", color=MUTED)
    f.text(736, 748, "Leaf labels are plain byte indices 0..31; their populations are now known.",
           22, anchor="middle")
    f.text(736, 782, "Next: a rank selects the exact bit pattern within each byte.", 22,
           anchor="middle", color=MUTED)
    f.save(OUT / "structure.svg")


def singleton(body, patterns):
    f = Figure(772, "Bec256 encodes position 29 as body byte a7",
               "With population one, divide regions zero to 255, zero to 127, zero to 63, "
               "zero to 31, then 16 to 31. Their left and right counts are respectively "
               "one and zero, one and zero, one and zero, zero and one, zero and one. "
               "The stored left counts are one, one, one, zero, zero, locating byte three. "
               "The eight one-bit byte values are 01, 02, 04, 08, 10, 20, 40 and 80. "
               "Rank five selects 20. Its bits one, zero, one follow the five split bits, "
               "giving a7 in least-significant-bit-first stream order.")
    f.heading(32, 42, "Position 29 · external population 1")
    f.text(32, 80, "First locate its byte; then select the byte's one-bit pattern.", 22, color=MUTED)
    f.heading(32, 136, "1. Recover the byte's population")
    f.text(32, 180, "Region split", 21, "bold")
    f.text(252, 180, "Left / right", 21, "bold")
    f.text(452, 180, "Occupied child", 21, "bold")
    f.text(708, 180, "Emit", 21, "bold", anchor="end")
    trace = [("0–255", "1 / 0", "0–127", 1),
             ("0–127", "1 / 0", "0–63", 1),
             ("0–63", "1 / 0", "0–31", 1),
             ("0–31", "0 / 1", "16–31", 0),
             ("16–31", "0 / 1", "24–31", 0)]
    for row, (region, counts, child, field) in enumerate(trace):
        y = 204 + row * 52
        f.rect(32, y, 688, 48, "#f6f8fb", "white", radius=3)
        f.text(48, y + 32, region, 23)
        f.text(276, y + 32, counts, 23)
        f.text(452, y + 32, child, 23)
        f.rect(664, y, 56, 48, BLUE, "white", radius=3)
        f.text(692, y + 32, field, 25, anchor="middle", mono=True)
    f.lines(32, 500, ["Positions 24–31 are byte 3: population 1.",
                     "Every empty sibling is determined and emits no bits."],
            22, 32, color=MUTED)

    f.heading(792, 136, "2. Select byte 3's pattern")
    f.text(792, 180, "All eight bytes with population 1", 22, color=MUTED)
    f.text(792, 230, "Rank (index)", 21, "bold")
    for i, value in enumerate(patterns):
        x = 792 + 56 * i
        f.rect(x, 250, 54, 42, GREEN if i == 5 else "#f6f8fb", "white")
        f.text(x + 27, 279, i, 24, anchor="middle", mono=True)
    f.text(792, 330, "Byte value (hex)", 21, "bold")
    for i, value in enumerate(patterns):
        x = 792 + 56 * i
        f.rect(x, 350, 54, 42, GREEN if i == 5 else "#f6f8fb", "white")
        f.text(x + 27, 379, f"{value:02x}", 24, anchor="middle", mono=True)
    f.path("M1099,300 V340", arrow=True)
    f.lines(792, 438, ["Rank 5 selects 20 hex.",
                      "It sets bit 5 of byte 3: 8×3+5 = 29.",
                      "Eight patterns need a three-bit index."],
            22, 32)
    f.path("M32,564 H1248", color=RULE)
    f.text(32, 604, "Append fields", 24, "bold")
    f.text(32, 638, "Stream / body bit", 20, color=MUTED)
    f.text(32, 674, "Low → high", 22, color=MUTED)
    for bit in range(8):
        x = 256 + bit * 80
        f.text(x + 40, 616, bit, 20, anchor="middle", color=MUTED)
        f.rect(x, 630, 78, 54, BLUE if bit < 5 else GREEN, "white", radius=3)
        f.text(x + 39, 668, (body >> bit) & 1, 27, anchor="middle", mono=True)
    f.path("M256,696 V710 H654 V696")
    f.path("M656,696 V710 H894 V696")
    f.text(456, 745, "Five left counts", 22, anchor="middle")
    f.text(776, 745, "Rank 5: bits 1,0,1", 22, anchor="middle")
    f.path("M918,658 H976", arrow=True)
    f.text(1096, 656, f"{body:02x}", 40, "bold", anchor="middle", mono=True)
    f.text(1096, 690, "one body byte", 22, anchor="middle", color=MUTED)
    f.save(OUT / "singleton.svg")


if __name__ == "__main__":
    data = wire_examples()
    structure()
    singleton(*data)
    print("Generated structure.svg and singleton.svg; checked both worked bodies, ranks and size bound.")
