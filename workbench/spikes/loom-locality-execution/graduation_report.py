#!/usr/bin/env python3
"""Qualify matched private/product policy and unchanged complete caller controls."""
import argparse
import csv
import json
import os
import re
import shlex
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
import report as old
import followup_report as followup

require=old.require
digest=old.digest
SCALES=("hot","large")
VARIANTS=("private","product")
CUTOFFS=(32,128)
PHASES=old.PHASES
MODES=old.EXECUTION_MODES
COST={"known_direct":1,"cheap":1,"expensive":2,"expired":2}

def targets():
    windows={f"loom_graduation_window_{scale}_{kind}" for scale in SCALES for kind in range(3)}
    execution={f"loom_graduation_{scale}_{kind}_{variant}"
        for scale in SCALES for kind in range(3) for variant in VARIANTS}
    costs={f"loom_graduation_cost_{variant}" for variant in VARIANTS}
    return windows,execution,costs

def expected_windows():
    return {(scale,kind,mixed,concurrency,owner,method)
        for scale,kind,mode,mixed,concurrency,owner,method in old.expected_window_cells() if mode=="reference"}

def expected_execution():
    return {(scale,kind,variant,mixed,phase,mode,cutoff)
        for scale in SCALES for kind in range(3) for variant in VARIANTS for mixed in (0,1)
        for phase in PHASES for mode in MODES for cutoff in CUTOFFS}

def schedule(root,repetitions,hashes):
    entries=json.loads((root/"schedule.json").read_text())
    expected=[]
    for scale in SCALES:
        for kind in range(3):
            for rep in range(repetitions):
                pair=VARIANTS if rep%2==0 else tuple(reversed(VARIANTS))
                expected.append((f"loom_graduation_window_{scale}_{kind}",rep))
                expected.extend((f"loom_graduation_{scale}_{kind}_{variant}",rep) for variant in pair)
    require(isinstance(entries,list) and len(entries)==len(expected),"scheduled process count")
    seen=[]
    for item in entries:
        require(isinstance(item,dict) and set(item)=={"target","pass","csv","sha256"},"schedule fields")
        require(type(item["pass"]) is int,"noninteger schedule pass")
        require(item["csv"]==f'{item["target"]}-pass{item["pass"]}.csv',"schedule filename")
        followup.safe_name(item["csv"])
        require(re.fullmatch(r"[0-9a-f]{64}",item["sha256"]) is not None,"schedule hash syntax")
        require(digest(root/item["csv"])==item["sha256"],"schedule CSV digest")
        hashes[item["csv"]]=item["sha256"];seen.append((item["target"],item["pass"]))
    require(seen==expected,"schedule order/inventory differs from alternating plan")
    require({p.name for p in root.glob("loom_*-pass*.csv")}=={item["csv"] for item in entries},"extra/missing scheduled CSV")
    hashes["schedule.json"]=digest(root/"schedule.json")
    return entries

def compile_receipts(root,expected,hashes):
    entries=json.loads((root/"compile.json").read_text());present=defaultdict(list)
    for item in entries:
        tokens=item.get("arguments") or shlex.split(item["command"])
        for target in expected:
            if any(f"/CMakeFiles/{target}.dir/" in t or t.startswith(f"CMakeFiles/{target}.dir/") for t in tokens):
                present[target].append((item,tokens))
    require(set(present)==expected,"compilation target inventory")
    for target,commands in present.items():
        for item,tokens in commands:
            require("-O3" in tokens and "-std=c++23" in tokens,f"release options: {target}")
            name=Path(item["file"]).name
            if target in ("loom_execution_check","loom_example_execution"):
                directory="test" if target=="loom_execution_check" else "examples"
                require(name=="execution.cpp" and f"/loom/{directory}/" in item["file"],"public behavior/example source")
                continue
            require("-fno-vectorize" in tokens and "-fno-slp-vectorize" in tokens,f"vector controls: {target}")
            if target.startswith("loom_graduation_cost_"):
                require(name in ("graduation_cost.cpp","graduation_cost_codegen.cpp"),"decision cost source")
            else:
                require("-DNDEBUG" in tokens,f"consumer assertion setting: {target}")
                match=re.fullmatch(r"loom_graduation_(?:window_)?(hot|large)_([0-2])(?:_(private|product))?",target)
                require(match is not None,"unknown consumer target")
                scale,kind,variant=match.groups()
                for flag in ("-DHT_STAGED_WIDTH=8","-DHT_STAGED_KEY=40","-DHT_STAGED_GRAPH_HINT=1","-DHT_RECORD_READ_WINDOWS=2",f"-DHT_STAGED_KIND={kind}"):
                    require(flag in tokens,f"consumer option {flag}: {target}")
                geometry=9 if scale=="hot" and kind=="0" else 10 if scale=="hot" else 20 if kind=="0" else 19
                if geometry!=10:require(f"-DHT_STAGED_A={geometry}" in tokens,f"consumer geometry: {target}")
                require(name==("graduation.cpp" if variant else "staged.cpp"),f"consumer source: {target}")
            if target.endswith(("_private","_product")):
                require(f"-DLOOM_GRADUATED_POLICY={int(target.endswith('_product'))}" in tokens,f"policy variant: {target}")
    hashes["compile.json"]=digest(root/"compile.json")
    return len(present)

def receipts(root,source_root,hashes):
    windows,execution,cost=targets();expected=windows|execution|cost|{"loom_execution_check","loom_example_execution"}
    binaries=json.loads((root/"binaries.json").read_text())
    require(set(binaries)==expected,"binary inventory differs")
    for name in expected:
        require(digest(root/name)==binaries[name],f"binary digest: {name}")
    hashes["binaries.json"]=digest(root/"binaries.json")
    checks={"loom_execution_check","loom_example_execution"}|{name+"_check" for name in windows|execution}
    text=(root/"check.txt").read_text()
    passed=re.findall(r"Test\s+#\d+:\s+(\S+)\s+\.{2,}\s+Passed\b",text)
    require(len(passed)==len(checks) and set(passed)==checks,"CTest identities")
    require(re.search(rf"100% tests passed, 0 tests failed out of {len(checks)}\b",text) is not None,"CTest completion")
    hashes["check.txt"]=digest(root/"check.txt")
    here=Path(__file__).resolve().parent
    environment=dict(os.environ,PYTHONOPTIMIZE="0")
    codegen={}
    for target in sorted(windows|execution|cost):
        checker=here/("check_consumer_codegen.py" if target in windows else "graduation_codegen.py")
        command=[sys.executable,"-B",str(checker),str(root/(target+".asm.gz"))]
        if target in windows and int(target.rsplit("_",1)[1])<2:command.append("--word-control")
        if target in cost:command.append("--cost")
        replay=subprocess.run(command,capture_output=True,text=True,env=environment)
        require(replay.returncode==0,f"native codegen: {target}: {replay.stderr[-2000:]}")
        receipt_path=root/(target+"-codegen.json")
        codegen[target]=json.loads(replay.stdout)
        require(codegen[target]==json.loads(receipt_path.read_text()),f"codegen receipt changed: {target}")
        for path in (receipt_path,root/(target+".asm.gz")):hashes[path.name]=digest(path)
    callback_comparison=[]
    for scale in SCALES:
        for kind in range(3):
            identities=[]
            for variant in VARIANTS:
                receipt=codegen[f"loom_graduation_{scale}_{kind}_{variant}"]
                native=[body for body in receipt["functions"] if body["native"]]
                require(len(native)==1,"actual native callback identity")
                identities.append(native[0]["normalized_sha256"])
            require(identities[0]==identities[1],f"private/product native callback changed: {scale}/{kind}")
            callback_comparison.append(dict(scale=scale,kind=kind,normalized_sha256=identities[0],identical=True))
    source=followup.verify_source(root,source_root,hashes)
    compile_targets=compile_receipts(root,expected,hashes)
    result=json.loads((root/"worker-result.json").read_text())
    require(result["state"]=="complete" and result["script_returncode"]==0,"worker not complete")
    for name in ("compiler.txt","hardware.json","host.json","worker-result.json"):
        hashes[name]=digest(root/name)
    require("21.1.8" in (root/"compiler.txt").read_text(),"unexpected compiler pin")
    for path in (Path(__file__),here/"graduation_codegen.py",here/"check_consumer_codegen.py",here/"report.py",here/"followup_report.py"):
        hashes["tool:"+path.name]=digest(path)
    return dict(checks=len(checks),binary_hashes=len(expected),codegen_replays=len(windows|execution|cost),
        compile_targets=compile_targets,native_callback_comparison=callback_comparison,**source)

def paired(groups):
    out=[]
    for key,control in sorted(groups.items()):
        if key[2]!="private":continue
        other=groups[key[:2]+("product",)+key[3:]]
        other={int(row["rep"]):row for row in other};ratios=[];differences=[]
        for row in sorted(control,key=lambda row:int(row["rep"])):
            before=float(row["ns_per_request"]);after=float(other[int(row["rep"])]["ns_per_request"])
            ratios.append(after/before);differences.append(after-before)
        out.append(dict(zip(("scale","kind","mixed","phase","mode","cutoff"),key[:2]+key[3:]))|
            dict(product_over_private_median=statistics.median(ratios),pass_ratios=ratios,
                 product_minus_private_median=statistics.median(differences),pass_differences=differences))
    return out

def analyze(args):
    root=args.root;hashes={};windows=defaultdict(list);execution=defaultdict(list);cost=defaultdict(list);inventory={}
    entries=schedule(root,args.repetitions,hashes)
    policy_sizes={}
    for entry in entries:
        target,rep=entry["target"],entry["pass"]
        for original in old.rows(root/entry["csv"]):
            row=dict(original);mixed=int(row["mixed"])
            require(mixed in (0,1),"mixed population")
            match=re.fullmatch(r"loom_graduation_window_(hot|large)_([0-2])",target)
            if match:
                scale,kind=match.groups();kind=int(kind)
                require(int(row["rep"])==0,"window process raw repetition")
                row["rep"]=str(rep)
                keys=old.keys_for(scale,kind);graph=keys*(1+mixed)
                geometry=9 if scale=="hot" and kind==0 else 10 if scale=="hot" else 20 if kind==0 else 19
                require(int(row["K"])==40 and int(row["W"])==(0 if kind==0 else 8),"window key/value")
                require(int(row["kind"])==kind and int(row["keys"])==keys,"window owner")
                require(int(row["A"])==geometry and int(row["B"])==geometry-4,"window geometry")
                require(int(row["unit"])==(64 if kind==0 else 128 if kind==1 else 192),"window unit")
                require(int(row["requests"])==graph*max(1,(262144+graph-1)//graph),"window complete graph")
                require(int(row["graph_hints"])==1 and int(row["capacity_family"])==0 and int(row["read_windows"])==2,
                    "captured window configuration")
                require(int(row["word_control"])==int(kind<2),"word control configuration")
                concurrency=int(row["concurrency"]);owner,method=row["owner"],row["method"]
                require(int(row["ring_visits"])>=0 and bool(int(row["ring_visits"]))==(method in ("ring","ring_hints")),"window Ring work")
                key=(scale,kind,mixed,concurrency,owner,method);windows[key].append(row)
                old.same_inventory(inventory,("window",)+key,row,("requests","requested","backing","metadata","jobs_bytes",
                    "graph_bytes","ring_bytes","ring_visits","graph_hints","secondary_unit","inline_bytes",
                    "primary_slots","secondary_slots","hint_bytes_per_graph","hint_windows_per_graph"))
                continue
            match=re.fullmatch(r"loom_graduation_(hot|large)_([0-2])_(private|product)",target)
            require(match is not None,"unexpected execution target")
            scale,kind,variant=match.groups();kind=int(kind)
            phase,mode,cutoff=row["phase"],row["mode"],int(row["cutoff"])
            require(int(row["rep"])==rep,"execution external repetition")
            require(int(row["graduated"])==int(variant=="product"),"execution policy variant")
            require(mode in MODES and phase in PHASES and cutoff in CUTOFFS,"execution case")
            require(int(row["mode_order"])==(MODES.index(mode)-rep)%len(MODES),"execution mode did not rotate")
            require(int(row["cutoff_order"])==(CUTOFFS.index(cutoff)-rep)%2,"execution cutoff did not rotate")
            require(int(row["kind"])==kind and int(row["keys"])==old.keys_for(scale,kind),"execution shape")
            for field,value in (("probe_requests",32),("probe_heads",4),("probe_groups",8),("sample_cadence_batches",32)):
                require(int(row[field])==value,f"execution probe {field}")
            direct,ring,sampled,sample_ns=(int(row[field]) for field in ("direct_batches","ring_batches","sample_requests","sample_ns"))
            require(min(direct,ring)>=0 and direct+ring==args.iterations,"execution batch count")
            require(int(row["requests"])==args.iterations*256,"useful request denominator")
            require(0<=sampled<=args.iterations*32 and sampled%32==0,"completed sample request count")
            require((sampled==0 and sample_ns==0) or (sampled>0 and sample_ns>0),"completed sample timing")
            require(sample_ns<=float(row["ns_per_request"])*int(row["requests"])+1,"sample charge exceeds complete operation")
            require(int(row["ring_visits"])>=0 and bool(int(row["ring_visits"]))==bool(ring),"execution Ring work")
            require(0<int(row["policy_bytes"])<=128,"bounded owner-local state")
            if variant=="private":require(int(row["policy_bytes"])==80,"private record/cadence size")
            if variant in policy_sizes:require(policy_sizes[variant]==int(row["policy_bytes"]),"policy shape changed")
            policy_sizes[variant]=int(row["policy_bytes"])
            if mode in ("direct","ring","disabled","caller_known"):
                require(sampled==0,"nonsampling control sampled")
            else:require(sampled>0,"sampling control omitted actual samples")
            if mode in ("direct","caller_known"):
                require(direct==args.iterations and int(row["decision_calls"])==0,"direct control")
            elif mode=="ring":
                require(ring==args.iterations and int(row["decision_calls"])==0,"Ring control")
            else:
                require(int(row["decision_calls"])==args.iterations,"ordinary selection count")
                if mode in ("disabled","sampling_only"):require(ring==args.iterations,"Ring-only control")
            key=(scale,kind,variant,mixed,phase,mode,cutoff);execution[key].append(row)
            old.same_inventory(inventory,("execution",scale,kind,mixed,phase),row,
                ("requests","checksum","trace_bytes","graph_bytes","interference_bytes"))
    for variant in VARIANTS:
        path=root/f"policy-cost-{variant}.csv";hashes[path.name]=digest(path)
        for row in old.rows(path):
            case=row["case"];require(case in COST,"decision cost case")
            require(int(row["operations"])==1000000 and int(row["checksum"])==COST[case]*1000000,"decision cost outcome")
            require(int(row["graduated"])==int(variant=="product"),"decision cost variant")
            require(0<int(row["policy_bytes"])<=128,"decision cost state")
            cost[(variant,case)].append(row)
    for label,groups,expected in (("window",windows,expected_windows()),("execution",execution,expected_execution()),
        ("cost",cost,{(variant,case) for variant in VARIANTS for case in COST})):
        old.exact_cells(groups,expected,label,False)
        for key,group in groups.items():old.repetitions(group,args.repetitions,f"{label} {key}",False)
    qualification=dict(complete_matrix=True,repetitions=args.repetitions,execution_iterations=args.iterations,
        scheduled_processes=len(entries),window_cells=len(windows),execution_cells=len(execution),receipts_verified=False)
    if args.qualify:
        qualification|=receipts(root,args.source_root,hashes);qualification["receipts_verified"]=True
    window_summary=[dict(zip(("scale","kind","mixed","concurrency","owner","method"),key))|old.summarize(group)
        for key,group in sorted(windows.items())]
    execution_summary=[dict(zip(("scale","kind","variant","mixed","phase","mode","cutoff"),key))|old.summarize(group)|
        {field:[int(row[field]) for row in sorted(group,key=lambda row:int(row["rep"]))]
        for field in ("sample_requests","sample_ns","direct_batches","ring_batches","ring_visits","decision_calls")}
        for key,group in sorted(execution.items())]
    cost_summary=[dict(variant=key[0],case=key[1])|old.summarize(group) for key,group in sorted(cost.items())]
    pairs=paired(execution)
    result=dict(qualification=qualification,input_hashes=hashes,policy_bytes=policy_sizes,window=window_summary,
        execution=execution_summary,paired_execution=pairs,policy_cost=cost_summary,
        limits=["Fresh controls use the captured current caller; old campaign timings are not pooled.",
            "The private policy_bytes counter reports a 56-byte Record and 24-byte ProbeCadence; its wrapper also stores 24 bytes of options (104 bytes total). Product reports its complete 88-byte Policy including options. These counters are not a total-memory ranking.",
            "Separate processes alternate private/product order; internal execution mode/cutoff order rotates by external pass.",
            "Samples are additional completed serial work, fully charged outside the useful request denominator.",
            "Thirty-two-query probes are bounded in work, not time; expiry can cause one probe per operation.",
            "Two cheap probes cover eight of thirty-two heads; heterogeneity remains a caller-cohort limitation.",
            "Interference conditions history but does not establish cache service tiers; identity transitions are synthetic.",
            "The source/binary/assembly qualification verifies retained bytes, not independent remeasurement.",
            "No PMU, IPC, stall, miss-occupancy or physical-migration claim."])
    (root/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    for name,items in (("window-summary.csv",window_summary),("execution-summary.csv",execution_summary),
        ("paired-execution-summary.csv",pairs),("policy-cost-summary.csv",cost_summary)):
        with (root/name).open("w",newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(items[0]));writer.writeheader();writer.writerows(items)
    print(json.dumps(dict(qualification=qualification,policy_bytes=policy_sizes,policy_cost=cost_summary),indent=2))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("root",type=Path)
    parser.add_argument("--qualify",action="store_true");parser.add_argument("--source-root",type=Path)
    parser.add_argument("--repetitions",type=int,default=5);parser.add_argument("--iterations",type=int,default=64)
    args=parser.parse_args()
    if args.repetitions<1 or args.iterations<1:parser.error("positive repetitions/iterations required")
    if args.source_root and not args.qualify:parser.error("--source-root requires --qualify")
    try:analyze(args)
    except (ValueError,KeyError,TypeError,OSError) as error:parser.exit(2,f"Graduation qualification failed: {error}\n")
if __name__=="__main__":main()
