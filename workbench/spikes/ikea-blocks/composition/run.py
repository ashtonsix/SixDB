#!/usr/bin/env python3
"""Capture this composition probe's code, checks and actual handoff assembly."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import os
import shutil
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/"workbench/tools"))
from experiment import Run

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cross",action="store_true",help="also compile x86 AVX2/Zen5 from the configured ARM development host")
parser.add_argument("--benchmark",action="store_true",help="time the native executable only")
parser.add_argument("--march",default="",help="native -march, such as znver5 on a Zen5 worker")
parser.add_argument("--sanitize",action="store_true",help="ASan/UBSan native correctness check")
args=parser.parse_args()
stamp=datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
output=Path(os.environ.get("SIXDB_RESULTS",ROOT/"build/experiments/ikea-composition-probe"))/stamp
flags=[] if not args.march else ["-march="+args.march]
if args.sanitize:flags+=["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
variants={"native":flags}
if args.cross:
    variants|={"x86-avx2":["--target=x86_64-linux-gnu","-march=x86-64-v3"],
               "x86-zen5":["--target=x86_64-linux-gnu","-march=znver5"]}
run=Run(ROOT,output.resolve(),{"question":"same authoring work, carrier costs and model identity",
        "variants":variants,"benchmark":args.benchmark},workspace=ROOT/"build/workspaces/ikea-composition-probe")
source=run.source_root/"workbench/spikes/ikea-blocks/composition"
try:
    run.step("compiler",["clang++-21","--version"],"compiler.txt")
    run.step("cpu",["lscpu"],"cpu.txt")
    compact=["compiler.txt"]
    for name,variant_flags in variants.items():
        build=run.build_dir/name
        command=["cmake","-G","Ninja","-S",str(source),"-B",str(build),
                 "-DCMAKE_CXX_COMPILER=clang++-21","-DCMAKE_BUILD_TYPE=",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON","-DCMAKE_CXX_FLAGS="+" ".join(variant_flags)]
        if name.startswith("x86"):
            command += ["-DCMAKE_SYSTEM_NAME=Linux","-DCMAKE_SYSTEM_PROCESSOR=x86_64"]
        run.step(name+"-configure",command)
        run.step(name+"-build",["cmake","--build",str(build),"--parallel",os.environ.get("SIXDB_BUILD_JOBS","1")])
        binary_dir=run.output/name;binary_dir.mkdir()
        shutil.copy2(build/"compile_commands.json",binary_dir/"compile_commands.json")
        for executable in ["ikea_composition_check","ikea_composition_bench"]:
            shutil.copy2(build/executable,binary_dir/executable)
        units=["driver","native_exports","stages_load","stages_features","stages_model","stages_done"]
        for unit in units:
            obj=build/"CMakeFiles/ikea_composition_core.dir"/(unit+".cpp.o")
            shutil.copy2(obj,binary_dir/(unit+".o"))
        run.step(name+"-assembly",["llvm-objdump-21","-dr","--no-show-raw-insn",*[str(binary_dir/(unit+".o")) for unit in units]],name+"-hot.asm")
        if name!="x86-zen5":
            prefix=[] if name=="native" else ["qemu-x86_64","-cpu","max","-L","/usr/x86_64-linux-gnu"]
            run.step(name+"-check",prefix+[str(build/"ikea_composition_check")],name+"-check.txt")
            compact.append(name+"-check.txt")
        if name=="native" and args.benchmark:
            if args.sanitize: raise ValueError("Timing with sanitizers is not a useful comparison")
            run.step(name+"-benchmark",[str(build/"ikea_composition_bench")],"samples.csv")
            compact.append("samples.csv")
    if not args.sanitize:
        run.step("select-assembly",[sys.executable,str(source.parent/"assembly.py"),
                                  "composition",str(run.output),str(run.output)])
        compact += [name+"-excerpts.asm" for name in variants]
    run.compact(compact,[])  # Direct observations; compiling again is a separate operation.
except Exception as exc:
    run.finish(exc);raise
else:
    error=run.finish()
    if error:raise error
    print(run.output)
