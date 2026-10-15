#!/usr/bin/env python3
"""Independent logical and refusal checks for graduation evidence qualification."""
import argparse
import contextlib
import copy
import csv
import io
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
import graduation_report as report
import graduation_codegen as codegen

def write_csv(path,rows):
    with path.open("w",newline="") as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)

def fixture(root):
    schedule=[]
    for scale in ("hot","large"):
        for kind in range(3):
            for rep in range(5):
                window=f"loom_graduation_window_{scale}_{kind}"
                pair=("private","product") if rep%2==0 else ("product","private")
                for target in (window,*(f"loom_graduation_{scale}_{kind}_{v}" for v in pair)):
                    items=[]
                    if target==window:
                        for s,k,mixed,c,owner,method in sorted(report.expected_windows()):
                            if (s,k)!=(scale,kind):continue
                            keys=6144 if scale=="hot" else 14400000 if kind==0 else 3600000
                            graph=keys*(1+mixed);geometry=9 if scale=="hot" and kind==0 else 10 if scale=="hot" else 20 if kind==0 else 19
                            items.append(dict(K=40,W=0 if kind==0 else 8,kind=kind,A=geometry,B=geometry-4,
                                unit=64 if kind==0 else 128 if kind==1 else 192,keys=keys,mixed=mixed,
                                concurrency=c,owner=owner,method=method,rep=0,requests=graph*max(1,(262144+graph-1)//graph),
                                ns_per_request=20,requested=1,backing=1,metadata=1,jobs_bytes=1,graph_bytes=1,
                                ring_bytes=1,ring_visits=graph if method in ("ring","ring_hints") else 0,graph_hints=1,
                                secondary_unit=1,capacity_family=0,read_windows=2,word_control=int(kind<2),
                                inline_bytes=16,primary_slots=1,secondary_slots=1,hint_bytes_per_graph=1,hint_windows_per_graph=1))
                    else:
                        variant=target.rsplit("_",1)[1]
                        for s,k,v,mixed,phase,mode,cutoff in sorted(report.expected_execution()):
                            if (s,k,v)!=(scale,kind,variant):continue
                            direct=64 if mode in ("direct","caller_known") else 0
                            sampled=96 if mode in ("adaptive","sampling_only") else 0
                            keys=6144 if scale=="hot" else 14400000 if kind==0 else 3600000
                            items.append(dict(kind=kind,keys=keys,mixed=mixed,phase=phase,mode=mode,cutoff=cutoff,
                                rep=rep,requests=16384,ns_per_request=20,sample_ns=sampled*10,
                                sample_requests=sampled,decision_calls=0 if mode in ("direct","ring","caller_known") else 64,
                                direct_batches=direct,ring_batches=64-direct,ring_visits=(64-direct)*256,
                                policy_bytes=80 if variant=="private" else 72,checksum=100000+kind*100+mixed*10+report.PHASES.index(phase),
                                trace_bytes=1024,graph_bytes=1,interference_bytes=134217728,graduated=int(variant=="product"),
                                probe_requests=32,probe_heads=4,probe_groups=8,sample_cadence_batches=32,
                                mode_order=(report.MODES.index(mode)-rep)%6,cutoff_order=(report.CUTOFFS.index(cutoff)-rep)%2))
                    name=f"{target}-pass{rep}.csv";write_csv(root/name,items)
                    schedule.append(dict(target=target,csv=name,sha256=report.digest(root/name),**{"pass":rep}))
    (root/"schedule.json").write_text(json.dumps(schedule))
    for variant in ("private","product"):
        rows=[dict(case=case,rep=rep,operations=1000000,ns_per_operation=3,
            checksum=result*1000000,policy_bytes=56 if variant=="private" else 72,graduated=int(variant=="product"))
            for case,result in report.COST.items() for rep in range(5)]
        write_csv(root/f"policy-cost-{variant}.csv",rows)

def analyze(root):
    with contextlib.redirect_stdout(io.StringIO()):
        report.analyze(SimpleNamespace(root=root,repetitions=5,iterations=64,qualify=False,source_root=None))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.parse_args()
    passed=[]
    with tempfile.TemporaryDirectory(prefix="loom-graduation-check-") as directory:
        root=Path(directory);fixture(root);analyze(root)
        summary=json.loads((root/"summary.json").read_text())
        assert summary["qualification"]["execution_cells"]==2016
        assert summary["qualification"]["window_cells"]==186
        passed.append("complete-independent-logical-matrix")
        schedule=json.loads((root/"schedule.json").read_text())
        target="loom_graduation_hot_0_private-pass0.csv";original=report.old.rows(root/target)
        def reject(name,mutate,rehash=True):
            items=copy.deepcopy(original);mutate(items)
            write_csv(root/target,items)
            changed=copy.deepcopy(schedule)
            if rehash:
                for item in changed:
                    if item["csv"]==target:item["sha256"]=report.digest(root/target)
            (root/"schedule.json").write_text(json.dumps(changed))
            try:analyze(root)
            except (ValueError,KeyError,TypeError):passed.append(name)
            else:raise RuntimeError(f"accepted negative control: {name}")
            write_csv(root/target,original);(root/"schedule.json").write_text(json.dumps(schedule))
        reject("CSV-hash-tamper",lambda rows:rows[0].update(ns_per_request=21),False)
        reject("missing-cell",lambda rows:rows.pop())
        reject("duplicate-cell",lambda rows:rows.append(copy.deepcopy(rows[0])))
        reject("sample-relabels-useful-work",lambda rows:rows[0].update(requests=16416))
        reject("mode-order-did-not-rotate",lambda rows:rows[0].update(mode_order=5))
        reject("cutoff-order-did-not-rotate",lambda rows:rows[0].update(cutoff_order=1))
        reject("policy-variant-mismatch",lambda rows:rows[0].update(graduated=1))
        reject("sample-not-complete-probe",lambda rows:next(r for r in rows if r["mode"]=="adaptive").update(sample_requests=31))
        reject("sample-cost-uncharged",lambda rows:next(r for r in rows if r["mode"]=="adaptive").update(sample_ns=1000000000))
        reject("disabled-control-sampled",lambda rows:next(r for r in rows if r["mode"]=="disabled").update(sample_requests=32,sample_ns=320))
        reject("sampling-only-selects-direct",lambda rows:next(r for r in rows if r["mode"]=="sampling_only").update(direct_batches=1,ring_batches=63))
        reject("wrong-checksum",lambda rows:rows[0].update(checksum=7))
        reject("nonfinite-timing",lambda rows:rows[0].update(ns_per_request="nan"))
        # Inventory/order must also reject a schedule that preserves every file.
        changed=copy.deepcopy(schedule);changed[1],changed[2]=changed[2],changed[1]
        (root/"schedule.json").write_text(json.dumps(changed))
        try:analyze(root)
        except ValueError:passed.append("candidate-order-did-not-alternate")
        else:raise RuntimeError("accepted nonalternating process order")
    # Synthetic instructions exercise refusal behavior independently of native files.
    functions=[
        ("void ht::staged_test::visit<true, Reader>(Cursor)","ret"),
        ("Answer loom_graduation::serial_probe<Reader>(Reader)","ret"),
        ("Answer loom_graduation::ready<Reader>(Reader)","ret"),
        ("Answer loom_graduation::interleaved<Reader>(Reader)","ret")]
    def assembly(functions):
        return "\n".join(f"{0x1000+index*16:x} <{name}>:\n {0x1000+index*16:x}: d65f03c0 {instruction}" for index,(name,instruction) in enumerate(functions))
    codegen.check_assembly(assembly(functions));passed.append("execution-codegen-inventory-positive")
    for index,instruction,name in ((0,"bl 0x800 <bad>","native-call"),(0,"str x0, [sp]","native-stack"),
        (1,"bl 0x800 <bad>","serial-call"),(2,"bl 0x800 <bad>","ready-call")):
        changed=copy.deepcopy(functions);changed[index]=(changed[index][0],instruction)
        try:codegen.check_assembly(assembly(changed))
        except ValueError:passed.append(name)
        else:raise RuntimeError(f"accepted codegen mutation: {name}")
    try:codegen.check_assembly(assembly(functions[:-1]))
    except ValueError:passed.append("missing-execution-function")
    else:raise RuntimeError("accepted missing function")
    codegen.check_assembly(assembly([("graduation_select","ret")]),True);passed.append("decision-codegen-positive")
    try:codegen.check_assembly(assembly([("graduation_select","bl 0x800 <bad>")]),True)
    except ValueError:passed.append("decision-call")
    else:raise RuntimeError("accepted outlined decision")
    print(json.dumps(dict(passed=len(passed),checks=passed),indent=2))
if __name__=="__main__":main()
