#!/usr/bin/env python3
"""Check worked examples and render the four hashtable teaching figures.

Build ikea_example_hashtable_accumulation with the pinned Linux toolchain, then:
  orb -m ubuntu python3 ikea/docs/hashtable/images/generate.py --build-dir build/clang/dev

Before drawing SVGs, the visual approach was explored and refined with imagegen,
using Bec256's structure/singleton figures as style references. Each figure
teaches one mechanism: address-carried bits, physical layout, stable split rows,
or caller-scheduled memory dependencies. Temporary readers/previews stay in
ignored build/diagrams/hashtable/. Shared Figure supplies accessible descriptions.
"""
import argparse
from pathlib import Path
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).parent
BUILD = ROOT / "build/diagrams/hashtable"
STYLE = runpy.run_path(str(ROOT / "ikea/docs/images/generate.py"))
Figure = STYLE["Figure"]
BLUE, GREEN, AMBER, MUTED, RULE = (STYLE[k] for k in ("BLUE", "GREEN", "AMBER", "MUTED", "RULE"))
EMPTY = "#f0f3f7"


def checked_examples(build_dir):
    """Read layout constants and an actual image split from the current C++."""
    BUILD.mkdir(parents=True, exist_ok=True)
    source = BUILD / "figure_examples.cpp"
    source.write_text(r'''
#include <ikea/hashtable.h>
#include <cassert>
#include <iostream>
namespace ht = ikea::hashtable;
using TableAccess = ht::author::table_access;
template<unsigned K> void geometry() {
    constexpr auto g = ht::geometry::at(64,64-K);
    std::cout << "geometry " << K << ' ' << g.bytes << ' ' << g.tiles << ' '
              << g.slots << ' ' << g.maximum_encoding << '\n';
}
int main() {
    const ht::permutation<16,12> route{713};
    const auto key = route.recover(0xa,0x35a);
    const auto h = route(key);
    assert(h.bucket==0xa && h.quotient==0x35a);
    std::cout << "route " << key << ' ' << h.bucket << ' ' << h.quotient << '\n';
    geometry<9>(); geometry<12>(); geometry<14>(); geometry<30>(); geometry<31>(); geometry<47>();
    using Layout = ht::table<40,10,8,30>;
    static_assert(Layout::B==64 && Layout::N==16 && Layout::T==2);
    static_assert(Layout::flags_offset==60 && Layout::overflow_offset==63);
    static_assert(Layout::value_offset==64 && Layout::stride==192);
    static_assert(ht::table<40,10,9,30>::stride==208);
    Layout empty(713);
    const auto image = TableAccess::get(empty);
    assert(image.primary.data[60]==255 && image.primary.data[61]==255);
    assert(image.primary.data[62]==0 && image.primary.data[63]==0);
    using Parent = ht::table<24,10,8,14>;
    using Child = ht::table<24,11,8,14>;
    Parent parent(713);
    const ht::word qs[8] = {17,8192+34,51,68,85,8192+102,119,136};
    const ht::permutation<24,14> mix{713};
    ht::word keys[8];
    for(unsigned row=0; row<8; ++row) {
        keys[row]=mix.recover(7,qs[row]);
        ht::bytes<8> value{};
        value[0]=row+1;
        const bool inserted=parent.insert(keys[row],value);
        assert(inserted);
    }
    for(auto row : {2u,4u,7u}) {
        const bool erased=parent.erase(keys[row]);
        assert(erased);
    }
    auto children=ht::grow<Child>(parent);
    parent.validate(); children->validate();
    const auto from=TableAccess::get(parent);
    const auto to=TableAccess::get(*children);
    const auto* p=from.primary.data+7*Parent::stride;
    const auto* low=to.primary.data+14*Child::stride;
    const auto* high=to.primary.data+15*Child::stride;
    assert(p[Parent::flags_offset]==0x94);
    assert(low[Child::flags_offset]==0xb6 && high[Child::flags_offset]==0xdd);
    for(unsigned row=0; row<8; ++row) {
        const bool live=!(p[Parent::flags_offset]&(1u<<row));
        const bool a=!(low[Child::flags_offset]&(1u<<row));
        const bool b=!(high[Child::flags_offset]&(1u<<row));
        assert(a==(live && !(qs[row]&8192)));
        assert(b==(live && (qs[row]&8192)));
        for(auto* target : {low,high}) {
            const auto actual=ht::point<14>(target,row,Child::N);
            const bool selected=target==low ? a : b;
            assert(actual==(selected ? (qs[row]&8191) : 16383));
            if(selected) assert(TableAccess::primary_value<Child>(target,row)[0]==row+1);
        }
        std::cout << "row " << row << ' ' << live << ' ' << (qs[row]>>13)
                  << ' ' << (qs[row]&8191) << ' ' << a << ' ' << b << '\n';
    }
    assert(parent.size()==5 && children->size()==5);
}
''')
    version = subprocess.check_output(["clang++-21", "--version"], text=True)
    if "version 21.1.8" not in version:
        raise RuntimeError("Use the compiler pinned in BUILDING.md")
    binary = BUILD / "figure_examples"
    libraries = [build_dir / "ikea" / f"libikea_{name}.a" for name in ("seriespack", "tuplepack", "core")]
    subprocess.run(["clang++-21", "-std=c++23", "-O1", "-I", str(ROOT / "ikea/include"),
                    str(source), *map(str, libraries), "-o", str(binary)], check=True)
    data = [line.split() for line in subprocess.check_output([str(binary)], text=True).splitlines()]
    geometries = {int(row[1]): list(map(int,row[2:])) for row in data if row[0]=="geometry"}
    if geometries != {9:[64,0,512,9],12:[64,4,32,14],14:[64,4,32,14],
                      30:[64,2,16,30],31:[96,2,16,46],47:[96,1,8,64]}:
        raise RuntimeError("Illustrated geometry changed; update figures and rationale together")
    route = list(map(int,next(row[1:] for row in data if row[0]=="route")))
    rows = [list(map(int,row[1:])) for row in data if row[0]=="row"]
    return route, rows


def routing(route):
    key, bucket, q = route
    f = Figure(404, "The bucket address carries part of the key",
               "A reversible permutation maps a 16-bit key to 0xa35a. Its high four "
               "bits, 1010, select bucket ten. Its low twelve bits, 0011 0101 1010, are stored "
               "as quotient 0x35a. The bucket and quotient reconstruct the complete permuted key, "
               "which the inverse permutation recovers using the same seed.")
    f.heading(32,42,"The bucket address carries part of the key")
    f.text(32,80,"Worked example · W = 16 · b = 4 · K = 12 · seed = 713",22,color=MUTED)
    f.lines(32,154,[f"Key 0x{key:04x}","reversibly permuted"],22,32)
    f.path("M252,172 H332",arrow=True)
    bits = f"{(bucket<<12)|q:016b}"
    for i, bit in enumerate(bits):
        x = 352+i*56
        f.rect(x,136,54,72,BLUE if i<4 else GREEN,"white",radius=3)
        f.text(x+27,182,bit,27,anchor="middle",mono=True)
    for start,end,label,note in [(352,574,"Bucket 1010","4 bits select bucket ten"),
                                 (576,1246,"Quotient 0011 0101 1010","Only these 12 bits are stored in the bucket")]:
        f.path(f"M{start},218 V232 H{end} V218")
        f.text((start+end)/2,270,label,24,"bold",anchor="middle")
        f.text((start+end)/2,306,note,21,anchor="middle",color=MUTED)
    f.path("M32,336 H1248",color=RULE)
    f.text(640,378,"Address + quotient preserve the full permuted key; the same seed recovers the original.",22,
           anchor="middle")
    f.save(OUT/"routing.svg")


def layout():
    f = Figure(656, "A K30 bucket: key tiles, empty flags and map values",
               "The 64-byte key region has tile zero at bytes zero to 29, tile one at 30 to 59, "
               "empty flags at 60 and 61, zero padding at 62, and overflow count at 63. Each tile "
               "contains eight 30-bit quotients in Local geometry. A set stops at byte 64; an "
               "eight-byte-value map appends sixteen values, making a 192-byte bucket stride.")
    f.heading(32,42,"Sixteen exact quotients in a 64-byte key region")
    f.text(32,80,"K = E = 30 · two eight-row tiles · byte offsets increase left to right",22,color=MUTED)
    for offset in (0,30,60,64):
        f.text(32+offset*19,126,offset,20,anchor="end" if offset==64 else "start",color=MUTED)
    for offset in (0,30):
        x = 32+offset*19
        f.rect(x,140,570,80,BLUE,"white",radius=2)
        f.text(x+285,175,f"Tile {offset//30} · positions {offset//30*8}–{offset//30*8+7}",24,"bold",anchor="middle")
        f.text(x+285,205,"8 × 30 bits = 30 bytes",22,anchor="middle")
    for i,color in enumerate((GREEN,GREEN,EMPTY,AMBER)):
        f.rect(1172+i*19,140,19,80,color,"white")
    f.path("M1172,228 V242 H1248 V228")
    f.path("M1210,242 V256 H976 V280",arrow=True)
    f.heading(32,296,"Inside each Local tile")
    f.lines(32,336,["24 bytes: eight high 24-bit bodies","6 bytes: low bits in six eight-row bit planes"],22,34)
    for i,(name,note,color) in enumerate((("Flags 0","byte 60",GREEN),("Flags 1","byte 61",GREEN),
                                          ("Zero","byte 62",EMPTY),("Overflow","byte 63",AMBER))):
        x=704+i*136
        f.text(x+66,300,note,20,anchor="middle",color=MUTED)
        f.rect(x,314,132,64,color,"white",radius=3)
        f.text(x+66,354,name,22,"bold",anchor="middle")
    f.text(976,410,"Flag bit: 1 = empty, 0 = occupied",21,anchor="middle",color=MUTED)
    f.path("M32,446 H1248",color=RULE)
    f.heading(32,488,"Sets stop at byte 64; maps append value storage")
    for offset in (0,64,192):
        f.text(32+offset*(1216/192),534,offset,20,anchor="end" if offset==192 else "start",color=MUTED)
    x=32+64*(1216/192)
    f.rect(32,548,x-32,60,BLUE,"white",radius=2)
    f.text((32+x)/2,586,"Key region · 64 B",22,"bold",anchor="middle")
    width=8*(1216/192)
    for row in range(16):
        f.rect(x+row*width,548,width-1,60,GREEN,"white",radius=2)
        f.text(x+(row+.5)*width,586,row,21,anchor="middle",mono=True)
    f.text(640,642,"V = 8 example · 16 value rows × 8 B = 128 B · complete bucket stride = 192 B",21,
           anchor="middle",color=MUTED)
    f.save(OUT/"layout.svg")


def growth(rows):
    f = Figure(624, "Same-seed growth preserves row positions in compatible layouts",
               "The displayed parent tile has live rows zero, one, three, five and six. Their "
               "top quotient bits are zero, one, zero, one and zero. Child 2i keeps rows zero, "
               "three and six; child 2i plus one keeps rows one and five. The top quotient bit "
               "moves into the bucket address. Other rows become empty; live values keep "
               "their row positions. The actual split is checked against grow.")
    f.heading(32,42,"Growth moves one quotient bit into the bucket address")
    f.text(32,80,"Same seed · K: 14 → 13 · physical E stays 14 · one eight-row tile shown",22,color=MUTED)
    stages=[(164,"Parent i","split bit : low quotient",1),
            (332,"Child 2i","top bit was 0",4),(452,"Child 2i + 1","top bit was 1",5)]
    for y,title,note,selector in stages:
        f.text(32,y+28,title,24,"bold")
        f.text(32,y+60,note,20,color=MUTED)
        for row,live,top,q,lo,hi in rows:
            x=304+row*118
            f.text(x+57,y-14,row,20,anchor="middle",color=MUTED)
            active = (live,lo,hi)[(1,4,5).index(selector)]
            f.rect(x,y,114,72,GREEN if active else EMPTY,"white",radius=3)
            if not active:
                f.text(x+57,y+46,"–",25,anchor="middle",color=MUTED)
            elif selector==1:
                f.rect(x+8,y+16,26,40,BLUE,"white",radius=3)
                f.text(x+21,y+43,top,23,anchor="middle",mono=True)
                f.text(x+74,y+44,f"{q:03x}",23,anchor="middle",mono=True)
            else:
                f.text(x+57,y+46,f"{q:03x}",25,anchor="middle",mono=True)
    f.path("M282,236 V368 H296",arrow=True)
    f.path("M282,368 V488 H296",arrow=True)
    f.text(32,582,"Quotients are hexadecimal. Live values keep their rows; excluded rows become canonical empties.",21,
           color=MUTED)
    f.save(OUT/"growth.svg")


def staged():
    f = Figure(576, "Staged reads return at dependent address boundaries",
               "Three traces show caller-controlled waits between preparation and advances. "
               "A primary set hit emits the key region and finishes in advance one. A primary map "
               "hit emits its selected value and consumes it in advance two. A Swiss hit emits "
               "controls after the primary miss, a tuple after a tag matches, then verifies the "
               "full key. False tags and more groups can require further advances.")
    f.heading(32,42,"Pause where the next address becomes known")
    f.text(32,80,"Default dependent scheduling · dashed lines let the caller run other work",21,color=MUTED)
    for i,title in enumerate(("Prepare","Advance 1","Advance 2","Advance 3")):
        x=244+i*256
        f.rect(x,122,236,48,EMPTY,"white",radius=3)
        f.text(x+118,154,title,23,"bold",anchor="middle")
        if i:
            f.path(f"M{x-12},184 V474",dash=True,color=RULE)
    traces=[("Primary set hit",202,[("Emit key region",BLUE),("Exact match · done",BLUE)]),
            ("Primary map hit",302,[("Emit key region",BLUE),("Match · emit value",GREEN),("Consume · done",GREEN)]),
            ("Swiss spill hit",402,[("Emit key region",BLUE),("Miss · emit controls",AMBER),
                                   ("Tag · emit tuple",AMBER),("Verify key · done",AMBER)])]
    for label,y,cells in traces:
        f.text(32,y+39,label,23,"bold")
        for i,(label,color) in enumerate(cells):
            x=244+i*256
            f.rect(x,y,236,64,color,"white",radius=3)
            f.text(x+118,y+40,label,21,anchor="middle")
            if i+1<len(cells):
                f.path(f"M{x+236},{y+32} H{x+250}",arrow=True)
    f.lines(32,520,["A primary miss with overflow count zero also finishes in advance 1.",
                    "False tags or further groups may require more advances. Emitting an extent does not wait for it."],21,32,
            color=MUTED)
    f.save(OUT/"staged.svg")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT/"build/clang/dev")
    args=parser.parse_args()
    route,rows=checked_examples(args.build_dir.resolve())
    routing(route)
    layout()
    growth(rows)
    staged()
    print("Generated four figures; checked geometry, recovery, empty bytes, split rows and values.")
