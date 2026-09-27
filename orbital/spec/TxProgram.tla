------------------------------ MODULE TxProgram ------------------------------
EXTENDS Contracts, Integers
CONSTANT TxNone

(* Application/extension fixture interpreter shared by the transaction and epoch
   models. Opaque scope IDs have no SQL meaning. A request dynamically inserts a
   query continuation. Source inputs are captured at the parent's fixed cut;
   private writes take precedence over that capture. The independent oracle does
   not invoke this interpreter. *)
Instruction(op,key,value) == [op |-> op,key |-> key,value |-> value]
Init(p,t,inputs,context) ==
  [todo |-> CASE p.program[t]="put" -> <<Instruction("literal",0,p.value[t]),Instruction("write",0,0),Instruction("finish",0,0)>>
            [] p.program[t]="none" -> <<Instruction("literal",0,p.value[t]),Instruction("finish",0,0)>>
            [] p.program[t]="own-read" -> <<Instruction("literal",0,p.value[t]),Instruction("write",0,0),Instruction("query",p.firstRead[t],0),Instruction("finish",0,0)>>
            [] p.program[t]="undeclared" -> <<Instruction("literal",0,p.value[t]),Instruction("illegal",0,0),Instruction("finish",0,0)>>
            [] p.program[t]="increment" -> <<Instruction("request",p.firstRead[t],0),Instruction("wasm-add",0,1),Instruction("write",0,0),Instruction("own",p.firstRead[t],0),Instruction("finish",0,0)>>
            [] p.program[t]="conditional" -> <<Instruction("request",p.firstRead[t],0),Instruction("branch",p.secondRead[t],0),Instruction("wasm-add",0,1),Instruction("write",0,0),Instruction("own",p.firstRead[t],0),Instruction("finish",0,0)>>
            [] p.program[t]="sum" -> <<Instruction("request",p.firstRead[t],0),Instruction("save",0,0),Instruction("request",p.secondRead[t],0),Instruction("add-saved",0,0),Instruction("write",0,0),Instruction("finish",0,0)>>
            [] OTHER -> <<Instruction("request",p.firstRead[t],0),Instruction("finish",0,0)>>,
   inputs |-> inputs, effects |-> [k \in {} |-> 0], result |-> 0,
   saved |-> 0, status |-> "running", context |-> context,
   trace |-> <<>>, calls |-> <<>>, outbox |-> <<>>, children |-> {}]

Step(p,t,s) ==
  LET i==Head(s.todo) rest==Tail(s.todo)
      source==IF i.key \in DOMAIN s.effects THEN s.effects[i.key]
              ELSE IF i.key \in DOMAIN s.inputs THEN s.inputs[i.key] ELSE 0
      base==[s EXCEPT !.todo=rest]
  IN CASE i.op="literal" -> [base EXCEPT !.result=i.value]
    [] i.op="request" -> [base EXCEPT
         !.todo= <<Instruction("query",i.key,0)>> \o @,
         !.calls=Append(@,<<"request-query",i.key,s.context>>),
         !.children=@ \cup {<<Len(s.calls)+1,i.key>>}]
    [] i.op="branch" ->
         LET key==IF s.result=0 THEN i.key ELSE p.firstRead[t]
         IN [base EXCEPT !.todo= <<Instruction("query",key,0)>> \o @,
              !.calls=Append(@,<<"branch-query",key,s.context>>),
              !.children=@ \cup {<<Len(s.calls)+1,key>>}]
    [] i.op="query" -> [base EXCEPT !.result=source,
         !.trace=Append(@,<<"query",i.key,source,s.context>>)]
    [] i.op="save" -> [base EXCEPT !.saved=s.result]
    [] i.op="wasm-add" -> [base EXCEPT !.result=@+i.value,
         !.calls=Append(@,<<"wasm-add",s.result,i.value,s.context>>)]
    [] i.op="add-saved" -> [base EXCEPT !.result=@+s.saved]
    [] i.op="write" -> [base EXCEPT !.effects=[k \in p.writes[t] |-> s.result]]
    [] i.op="own" -> [base EXCEPT
         !.trace=Append(@,<<"own",i.key,source,s.context>>)]
    [] i.op="illegal" -> [base EXCEPT !.effects=[k \in {} |-> 0], !.status="bad-envelope"]
    [] i.op="finish" -> [base EXCEPT
         !.status=IF @="bad-envelope" THEN @ ELSE "ok",
         !.outbox= <<[id |-> <<s.context,"result",0>>,value |-> s.result]>>]
    [] OTHER -> base

RECURSIVE Run(_,_,_)
Run(p,t,s) == IF s.todo= <<>> THEN s ELSE Run(p,t,Step(p,t,s))
Outcome(s) == [effects |-> s.effects,result |-> s.result,status |-> s.status,
               context |-> s.context,trace |-> s.trace,calls |-> s.calls,
               outbox |-> s.outbox,children |-> s.children]
Evaluate(p,t,inputs,context) == Outcome(Run(p,t,Init(p,t,inputs,context)))
=============================================================================
