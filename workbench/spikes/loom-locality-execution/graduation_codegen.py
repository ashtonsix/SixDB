#!/usr/bin/env python3
"""Check the actual graduation native visit, serial sample and ordinary paths."""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path
import check_consumer_codegen as common

def check_assembly(text,cost=False):
    bodies={};current=None
    for line in text.splitlines():
        header=common.HEADER.match(line.strip())
        if header:
            name=header[1]
            wanted=name=="graduation_select" if cost else (
                name.startswith("void ht::staged_test::visit<") or
                re.search(r"loom_graduation::(?:serial_probe|ready|interleaved)<",name))
            current=name if wanted else None
            if current is not None:
                common.require(name not in bodies,f"duplicate graduation function: {name}")
                bodies[name]=[]
        elif current is not None:
            instruction=common.INSTRUCTION.match(line)
            if instruction:bodies[current].append((instruction[1],instruction[2],line.strip()))
    if cost:
        common.require(set(bodies)=={"graduation_select"},"expected exactly one decision wrapper")
    else:
        expected={"visit":1,"serial_probe":1,"ready":1,"interleaved":1};actual=dict.fromkeys(expected,0)
        for name in bodies:
            kind="visit" if name.startswith("void ht::staged_test::visit<") else re.search(
                r"loom_graduation::(serial_probe|ready|interleaved)<",name)[1]
            actual[kind]+=1
            if kind=="visit":
                common.require(name.startswith("void ht::staged_test::visit<true, ") and "word_reader<" not in name,
                    "expected only actual generic hinted native callback")
        common.require(actual==expected,f"graduation function inventory: {actual}")
    out=[]
    for name,body in bodies.items():
        common.require(body,f"undecoded graduation function: {name}")
        calls=[];stack=[];rbp=[];failures=[]
        native=name.startswith("void ht::staged_test::visit<")
        serial="loom_graduation::serial_probe<" in name
        ordinary="loom_graduation::ready<" in name
        for mnemonic,operands,line in body:
            call=common.ordinary_call(mnemonic)
            frame=bool(common.STACK_REGISTER.search(operands.split(" <",1)[0])) or mnemonic.startswith(("push","pop")) or mnemonic in ("enter","leave","leaveq")
            if call:calls.append(line)
            if frame:stack.append(line)
            if native and mnemonic in ("pushq","popq") and operands.strip()=="%rbp":rbp.append(line);continue
            if native and (call or frame):failures.append(line)
            if (serial or cost) and call:failures.append(line)
            if cost and frame:failures.append(line)
            if ordinary and call and not common.permitted_outer_call(operands):failures.append(line)
            if (serial or ordinary) and common.OUTLINED_LOOKUP.search(operands):failures.append(line)
        common.require(not failures,f"graduation codegen violation in {name}: "+json.dumps(failures))
        if rbp:
            common.require(sum("pushq" in line for line in rbp)==1 and any("popq" in line for line in rbp),
                "native RBP save/restore is incomplete")
        normalized=[mnemonic+" "+re.sub(r"(?:0x)?[0-9a-f]+ <([^>]+)>",r"<\1>",operands) for mnemonic,operands,_ in body]
        identity=hashlib.sha256(json.dumps(normalized,separators=(",",":")).encode()).hexdigest()
        out.append(dict(name=name,instructions=len(body),normalized_instructions=normalized,
            normalized_sha256=identity,calls=calls,stack_references=stack,
            native=native,rbp_preservation=rbp))
    return dict(functions=out,native_callbacks=0 if cost else 1,
        serial_samples=0 if cost else 1,ordinary_decision=cost,
        limitation="Static frame/call inventory; instruction counts are diagnostic, not measured uops or traffic.")

def check(path,cost=False):
    opener=gzip.open if path.suffix==".gz" else open
    with opener(path,"rt") as stream:return check_assembly(stream.read(),cost)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("assembly",type=Path)
    parser.add_argument("--cost",action="store_true");args=parser.parse_args()
    try:print(json.dumps(check(args.assembly,args.cost),indent=2))
    except (ValueError,OSError) as error:parser.exit(1,f"Graduation codegen failed: {error}\n")
if __name__=="__main__":main()
