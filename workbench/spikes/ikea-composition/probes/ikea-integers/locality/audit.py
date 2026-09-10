#!/usr/bin/env python3
"""Exact byte-dependency audit; no kernels, load-width guesses or timings.

Run under Linux from the repository root:
  python3 workbench/spikes/ikea-composition/probes/ikea-integers/locality/audit.py --output DIR

Each format maps every logical data bit to one stored bit. Exhaustive checks
verify the mapping is a bijection, then test every point and aligned16 group
at every start residue reached by dense repetition from a64-byte boundary.
"""
from __future__ import annotations

import argparse
import csv
import json
from dataclasses import dataclass
from math import gcd
from pathlib import Path


# Format data, independently transcribed from Calico's frozen bytepack_ref.h
# piece table: (packed byte group, packed bit, input group, input bit, width).
# These are the prior's bit assignments, not a SixDB codec implementation.
PRIOR = {
    1: tuple((0, i, i, 0, 1) for i in range(8)),
    2: tuple((0, 2*i, i, 0, 2) for i in range(4)),
    3: ((0,0,0,0,3),(0,3,1,0,3),(0,6,2,0,2),
        (1,0,3,0,3),(1,3,4,0,3),(1,6,2,2,1),(1,7,5,2,1),
        (2,0,6,0,3),(2,3,7,0,3),(2,6,5,0,2)),
    4: ((0,0,0,0,4),(0,4,1,0,4)),
    5: ((0,0,0,0,5),(0,5,1,2,3),(1,0,2,0,5),(1,5,3,2,3),
        (2,0,4,0,5),(2,5,5,2,3),(3,0,6,0,5),(3,5,7,2,3),
        (4,0,1,0,2),(4,2,3,0,2),(4,4,5,0,2),(4,6,7,0,2)),
    6: ((0,0,0,0,6),(0,6,1,4,2),(1,0,1,0,4),
        (1,4,2,0,4),(2,0,3,0,6),(2,6,2,4,2)),
    7: ((0,0,0,0,7),(0,7,1,6,1),(1,0,1,0,6),(1,6,2,4,2),
        (2,0,3,0,7),(2,7,2,6,1),(3,0,2,0,4),(3,4,6,0,4),
        (4,0,4,0,7),(4,7,5,6,1),(5,0,5,0,6),(5,6,6,4,2),
        (6,0,7,0,7),(6,7,6,6,1)),
}

# Physical chunk order, expressed in the prior's chunk numbers. Only the
# 32-byte chunks move: their lane order and every intra-byte field stay intact.
PERMUTED_PRIOR = {5: (0, 1, 4, 2, 3), 7: (0, 1, 2, 3, 6, 5, 4)}


def tail_bits(r: int, row: int, layout: str) -> tuple[int, ...]:
    if not r:
        return ()
    if layout == "bitslice8":
        return tuple(8*((row//8)*r+b)+row%8 for b in range(r))
    den = 8//gcd(r, 8)
    block, local = divmod(row, 32*den)
    group, lane = divmod(local, 32)
    origin = block*32*r*den//8
    if layout == "sequential32":
        return tuple(8*(origin+32*((group*r+b)//8)+lane)+(group*r+b)%8
                     for b in range(r))
    if layout == "continuous32":
        # Finish the high fragment in the current chunk; begin the low
        # fragment at bit zero in the next. Same byte dependencies as the
        # LSB-first stream, but a distinct, fully specified physical encoding.
        chunk, used = divmod(group*r, 8)
        room = 8-used
        if r <= room:
            return tuple(8*(origin+32*chunk+lane)+used+b for b in range(r))
        low = r-room
        return tuple(8*(origin+32*(chunk+1)+lane)+b if b < low
                     else 8*(origin+32*chunk+lane)+used+b-low for b in range(r))
    assert layout in ("prior32", "permuted_prior32")
    old_to_new = {old: new for new, old in enumerate(PERMUTED_PRIOR[r])} \
        if layout == "permuted_prior32" else None
    positions = [None]*r
    for yg, yb, xg, xb, count in PRIOR[r]:
        if xg == group:
            for b in range(count):
                assert positions[xb+b] is None
                physical_group = old_to_new[yg] if old_to_new is not None else yg
                positions[xb+b] = 8*(origin+32*physical_group+lane)+yb+b
    assert None not in positions
    return tuple(positions)


@dataclass(frozen=True)
class Format:
    name: str
    width: int  # Body width after removal of deliberate filter heads.
    values: int
    stride: int
    bits: tuple[tuple[int, ...], ...]

    def check_mapping(self):
        assert len(self.bits) == self.values
        flat = [p for row in self.bits for p in row]
        assert all(len(row) == self.width for row in self.bits)
        assert len(flat) == len(set(flat)) == self.width*self.values
        assert set(flat) == set(range(8*self.stride))

    def byte_rows(self):
        self.check_mapping()
        return tuple(tuple(sorted({p//8 for p in row})) for row in self.bits)


def body_first(w: int, n: int, tail: str, groups=None, name=None) -> Format:
    q, r = divmod(w, 8)
    assert n % 8 == 0
    if tail != "bitslice8" and r:
        assert n % (32*(8//gcd(r,8))) == 0
    groups = [q] if groups is None else groups
    assert sum(groups) == q
    rows = []
    for row in range(n):
        out, offset = [], 0
        for group in groups:
            out.extend(range(8*(offset+row*group), 8*(offset+(row+1)*group)))
            offset += n*group
        out.extend(8*offset+p for p in tail_bits(r,row,tail))
        rows.append(tuple(out))
    return Format(name or f"body_first{n}_{tail}", w, n, n*w//8, tuple(rows))


def calico256(w: int, heads: int, tail: str) -> Format:
    q = w//8
    leading = min(q, 2-heads//8)
    rest = q-leading
    groups = [1]*leading + [b for b in (1,2,4) if rest & b]
    return body_first(w,256,tail,groups,name=f"calico256_{tail}")


# Xi is the AoS one-byte body for input group i of32 values; Yi is a
# contiguous32-byte residual chunk. These are proposals, with exact density.
HYBRIDS = {
    2: ("X0","X1","Y0","X2","X3"),
    4: ("X0","Y0","X1"),
    6: ("X0","Y0","X1","Y1","X2","Y2","X3"),
    7: tuple(x for i in range(7) for x in (f"X{i}",f"Y{i}"))+("X7",),
}


def hybrid(r: int, q: int=1) -> Format:
    assert q==1 or (q==2 and r==4)
    order = HYBRIDS[r] if q==1 else ("X0.0","X0.1","Y0","X1.0","X1.1")
    lookup = {name:i*32 for i,name in enumerate(order)}
    n = 32*(8//gcd(r,8))
    rows = []
    for row in range(n):
        group,lane = divmod(row,32)
        body_chunk,body_lane=divmod(q*lane,32)
        body_name=f"X{group}" if q==1 else f"X{group}.{body_chunk}"
        body = range(8*(lookup[body_name]+body_lane),8*(lookup[body_name]+body_lane+q))
        tails = []
        for bit in tail_bits(r,row,"sequential32" if r==7 else "prior32"):
            byte,b = divmod(bit,8)
            chunk,lane = divmod(byte,32)
            tails.append(8*(lookup[f"Y{chunk}"]+lane)+b)
        rows.append(tuple(body)+tuple(tails))
    return Format(f"hybrid32_q{q}_r{r}",8*q+r,n,32*len(order),tuple(rows))


def chunk_order_search(r: int, tail: str | None = None):
    """Exact bandwidth2 existence for q1's intact32B body/tail chunks.

    A row group requires its Xi body and one or more Ys. Form a clique from
    these jointly required chunks. A physical order meets the stipulated
    span bound exactly when every resulting edge has length<=2.
    """
    den=8//gcd(r,8)
    num=r*den//8
    nodes=tuple(f"X{i}" for i in range(den))+tuple(f"Y{i}" for i in range(num))
    neighbours={n:set() for n in nodes}
    hyperedges=[]
    tail=tail or ("sequential32" if r in (5,7) else "prior32")
    for i in range(den):
        edge={f"X{i}"}|{f"Y{p//256}" for p in tail_bits(r,i*32,tail)}
        hyperedges.append(sorted(edge))
        for a in edge:
            neighbours[a].update(edge-{a})
    overloaded=next((n for n in nodes if len(neighbours[n])>4),None)
    result={"r":r,"q":1,"values":32*den,"bytes":32*(den+num),
            "tail":tail,"required_chunk_sets":hyperedges,"search_states":0}
    too_many=next((edge for edge in hyperedges if len(edge)>3),None)
    if too_many:
        result.update({"exists":False,"certificate":"four jointly required intact chunks cannot fit bandwidth2",
                       "required_chunks":too_many,"minimum_chunk_span":len(too_many)-1})
        return result
    if overloaded:
        result.update({"exists":False,"certificate":"degree exceeds4 in bandwidth2 graph",
                       "vertex":overloaded,"neighbours":sorted(neighbours[overloaded])})
        return result

    def search(order,positions,unplaced):
        result["search_states"]+=1
        if not unplaced:
            return order
        at=len(order)
        for node in sorted(unplaced):
            if any(at-positions[n]>2 for n in neighbours[node] if n in positions):
                continue
            after=unplaced-{node}
            assigned=positions|{node:at}
            # Each old vertex has only these still-empty positions within2.
            if any(len(neighbours[n]&after)>max(0,p+2-at) for n,p in assigned.items()):
                continue
            found=search(order+[node],assigned,after)
            if found:
                return found
        return None

    order=search([],{},set(nodes))
    result.update({"exists":order is not None,"order":order})
    if order:
        positions={n:i for i,n in enumerate(order)}
        assert all(max(positions[n] for n in edge)-min(positions[n] for n in edge)<=2
                   for edge in hyperedges)
    return result


def permutation_checks():
    """Bounded refinement, with all points/groups tested at both legal phases.

    Compare against the original bit map bit-for-bit under only the declared
    chunk permutation, and report phase counts separately from run totals.
    """
    results=[]
    for r, order in PERMUTED_PRIOR.items():
        fmt=body_first(r,256,"permuted_prior32")
        rows=fmt.byte_rows()
        old=body_first(r,256,"prior32")
        for row in range(256):
            for bit in range(r):
                old_chunk, within=divmod(old.bits[row][bit],256)
                assert fmt.bits[row][bit]==256*order.index(old_chunk)+within
        groups=tuple(tuple(sorted({b//32 for b in rows[i]})) for i in range(0,256,32))
        phases=[]
        for phase in (0,32):
            counts={"base_residue":phase}
            for count in (1,16):
                line_counts=[]
                for first in range(0,256,count):
                    lines={((phase+b)//64) for row in rows[first:first+count] for b in row}
                    assert max(lines)-min(lines)<=1
                    line_counts.append(len(lines))
                counts[f"get{count}_one_line"]=line_counts.count(1)
                counts[f"get{count}_two_lines"]=line_counts.count(2)
                counts[f"get{count}_failures"]=0
            phases.append(counts)
        search=chunk_order_search(r,"permuted_prior32")
        assert not search["exists"]
        results.append({"r":r,"physical_order_old_chunks":order,
                        "tail_chunks_by_input32":groups,"phases":phases,"q1":search})
    return results


def intervals(values):
    result=[]
    for value in sorted(values):
        if result and result[-1][1]+1 == value:
            result[-1][1]=value
        else:
            result.append([value,value])
    return result


def audit(fmt: Format):
    rows = fmt.byte_rows()
    # Cover both the byte-residue period and logical16 alignment. For N=8,
    # groups can cross two primitives, so inspect an even number of tiles.
    tile_period = 64//gcd(fmt.stride,64)
    if fmt.values==8 and tile_period%2:
        tile_period*=2
    total_values=tile_period*fmt.values

    def point(index):
        tile,row=divmod(index,fmt.values)
        return tuple(tile*fmt.stride+b for b in rows[row])

    result={"format":fmt.name,"w":fmt.width,"values":fmt.values,"stride":fmt.stride,
            "tile_residues":sorted({i*fmt.stride%64 for i in range(tile_period)})}
    for count in (1,16):
        worst_lines=worst_span=failures=0
        witness=None
        for first in range(0,total_values,count):
            touched={b for i in range(first,first+count) for b in point(i)}
            lines=sorted({b//64 for b in touched})
            span=lines[-1]-lines[0]+1 if lines else 0
            worst_lines=max(worst_lines,len(lines))
            worst_span=max(worst_span,span)
            if span>2:
                failures+=1
                if witness is None:
                    tile,row=divmod(first,fmt.values)
                    base=tile*fmt.stride
                    witness={"tile":tile,"tile_residue":base%64,"first_row":row,
                             "relative_byte_intervals":intervals({b-base for b in touched}),
                             "cache_lines":lines}
        result[f"get{count}"]={"cases":total_values//count,"failures":failures,
                               "max_lines":worst_lines,"max_line_span":worst_span,
                               "first_failure":witness}
    return result


def padding_rows():
    out=[]
    for w in range(33,65):
        group=2*w
        delta=group-64
        m=64//delta
        pad=64-m*delta
        extent=64*(m+1)
        assert m*group+pad==extent
        for i in range(m):
            assert (i*group)//64 == i
            assert (i*group+group-1)//64-i<=1
        out.append({"w":w,"group_bytes":group,"groups_per_supertile":m,
                    "values_per_supertile":16*m,"supertile_bytes":extent,
                    "padding_bytes":pad,"padding_per_group":pad/m,
                    "overhead_percent":100*pad/(m*group)})
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    # Broad tables and witnesses regenerate in ignored output. Retained compact
    # evidence is an explicit selection, not overwritten by a routine run.
    parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[6]/
                        "build/experiments/ikea-integers-locality/audit")
    args=parser.parse_args()
    results=[]
    by_key={}

    def add(fmt):
        key=(fmt.name,fmt.width)
        if key not in by_key:
            by_key[key]=audit(fmt)
            results.append(by_key[key])
        return by_key[key]

    # Width means the remaining independent body: k=w+h, h in{0,8,16}.
    # h changes Calico's remaining global plane groups, so those are separate.
    head_cases=[]
    for k in range(1,65):
        for h in (0,8,16):
            if h>k:
                continue
            w=k-h
            local=add(body_first(w,8,"bitslice8",name="local8"))
            head_cases.append({"k":k,"head_bits":h,"w":w,
                               "local8_get1_pass":local["get1"]["failures"]==0,
                               "local8_get16_pass":local["get16"]["failures"]==0})
            for tail in ("bitslice8","prior32"):
                fmt=calico256(w,h,tail)
                fmt=Format(fmt.name+f"_h{h}",w,fmt.values,fmt.stride,fmt.bits)
                add(fmt)

    for w in range(1,65):
        for n in (16,32,64,128,256):
            add(body_first(w,n,"bitslice8"))
        r=w%8
        n=32*(8//gcd(r,8)) if r else 32
        for tail in ("prior32","sequential32"):
            add(body_first(w,n,tail))
    for r in HYBRIDS:
        candidate=add(hybrid(r))
        assert candidate["get1"]["failures"]==candidate["get16"]["failures"]==0
    candidate=add(hybrid(4,q=2))
    assert candidate["get1"]["failures"]==candidate["get16"]["failures"]==0
    searches=[chunk_order_search(r) for r in range(1,8)]
    assert [x["r"] for x in searches if x["exists"]]==[2,4,6,7]
    permutations=permutation_checks()
    for r in PERMUTED_PRIOR:
        candidate=add(body_first(r,256,"permuted_prior32"))
        assert candidate["get1"]["failures"]==candidate["get16"]["failures"]==0
        continuous=body_first(r,256,"continuous32")
        assert continuous.byte_rows()==body_first(r,256,"sequential32").byte_rows()
        candidate=add(continuous)
        assert candidate["get1"]["failures"]==candidate["get16"]["failures"]==0
    searches.extend(row["q1"] for row in permutations)

    # Analytic baseline and exact counterexamples are executable checks.
    for w in range(65):
        row=by_key[("local8",w)]
        assert row["get1"]["failures"]==0
        predicted=w<=32+gcd(w,32)
        assert (row["get16"]["failures"]==0)==predicted
    for r in range(1,8):
        n=32*(8//gcd(r,8))
        repaired=by_key[(f"body_first{n}_sequential32",r)]
        assert repaired["get1"]["failures"]==repaired["get16"]["failures"]==0
    for tail in ("prior32","bitslice8"):
        k12=by_key[(f"body_first64_{tail}",12)]
        assert k12["get1"]["failures"]==k12["get16"]["failures"]==0

    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/"audit.json").write_text(json.dumps({
        "contract":"exact independent stored-bit dependencies;64B-aligned array;all reachable residues",
        "widths":"k1..64; separate heads0/8/16<=k; w=k-head",
        "formats":results,"head_cases":head_cases,"optimal_padding":padding_rows(),
        "q1_chunk_search":searches,"permuted_prior_checks":permutations},indent=2)+"\n")
    (args.output/"chunk-search.json").write_text(json.dumps(searches,indent=2)+"\n")
    (args.output/"permuted-prior.json").write_text(json.dumps(permutations,indent=2)+"\n")
    fields=["format","w","values","stride","tile_residues",
            "get1_max_lines","get1_max_line_span","get1_failures",
            "get16_max_lines","get16_max_line_span","get16_failures"]
    with (args.output/"summary.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=fields)
        writer.writeheader()
        for item in results:
            row={field:item[field] for field in fields[:4]}
            row["tile_residues"]="/".join(map(str,item["tile_residues"]))
            for count in (1,16):
                for field in ("max_lines","max_line_span","failures"):
                    row[f"get{count}_{field}"]=item[f"get{count}"][field]
            writer.writerow(row)
    # Keep decisive cases beside the findings; the broad sweep remains raw.
    local_widths = set(range(1,8)) | {12,20,32,33,34,35,36,37,40,48,49,56,63,64}
    def selected(row):
        return ((row["format"] == "local8" and int(row["w"]) in local_widths)
                or ("permuted_prior32" in row["format"])
                or ("continuous32" in row["format"])
                or (row["format"] == "calico256_prior32_h0" and int(row["w"]) <= 7)
                or (row["format"].startswith("hybrid"))
                or (row["format"] in {"body_first64_bitslice8", "body_first64_prior32"}
                    and int(row["w"]) == 12))
    with (args.output/"summary.csv").open(newline="") as source, \
            (args.output/"cases.csv").open("w",newline="") as destination:
        writer = csv.DictWriter(destination, fieldnames=fields)
        writer.writeheader()
        writer.writerows(row for row in csv.DictReader(source) if selected(row))
    for name,rows in (("heads.csv",head_cases),("padding.csv",padding_rows())):
        with (args.output/name).open("w",newline="") as f:
            writer=csv.DictWriter(f,fieldnames=rows[0])
            writer.writeheader();writer.writerows(rows)
    print(json.dumps({"formats":len(results),"head_cases":len(head_cases),
                      "point_cases":sum(r["get1"]["cases"] for r in results),
                      "aligned16_cases":sum(r["get16"]["cases"] for r in results),
                      "status":"pass","output":str(args.output)},sort_keys=True))


if __name__=="__main__":
    main()
