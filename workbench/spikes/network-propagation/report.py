"""Standalone explorer and shareable figures from retained synthetic output."""
import json
import hashlib
from pathlib import Path
import sys


COLORS = {"balanced":"#147660","latency":"#1262ba","shallow":"#8b648d","branch-b1":"#68815c",
          "static":"#bd6836","adaptive":"#147660","adaptive-unbounded":"#7b609e","robust":"#215bcc",
          "direct":"#206a8b","shortest":"#7359a5","cost":"#bd6836","bounded":"#23826f",
          "peer":"#206a8b","blob":"#bd6836","cheapest":"#7359a5","bounded-fetch":"#23826f"}


def figures(out, records):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.family":"DejaVu Sans","font.size":10,"axes.spines.top":False,"axes.spines.right":False})
    image = out/"images"
    image.mkdir(exist_ok=True)
    fig, axes = plt.subplots(1,2,figsize=(11,4.3),layout="constrained")
    for ax,name in zip(axes,["regional-bulk","global-bulk"]):
        groups={}
        for record in records:
            s=record["summary"]
            if s["scenario"]!=name or s["delivery_p99_us"] is None: continue
            groups.setdefault((round(s["delivery_p99_us"]/1000,7),round(s["usd_per_GiB"],9)),[]).append(s["policy"])
        for i,(point,policies) in enumerate(groups.items()):
            ax.scatter(*point,color=COLORS[policies[0]],s=65)
            ax.annotate(str(i+1),point,xytext=((i%2)*22-8,12+(i%3)*13),ha="center",textcoords="offset points",fontsize=8,arrowprops=dict(arrowstyle="-",color="#9babaf",lw=.5))
        ax.legend([str(i+1)+": "+" / ".join(p) for i,p in enumerate(groups.values())],loc="upper left",fontsize=6,frameon=False)
        ax.margins(x=.25,y=.3)
        ax.set(xlabel="Simulated p99 last-recipient delivery (ms)",ylabel="Variable USD / GiB of source payload",title=name.replace("-"," ").title())
        ax.grid(alpha=.2)
    fig.suptitle("Relaying can reduce both charges and source contention · synthetic 1 MiB objects",fontsize=12)
    fig.savefig(image/"tradeoffs.png",dpi=160)
    plt.close(fig)
    names=["regional-bulk","regional-busy","regional-slow-root","regional-slow-relay"]
    fig,axes=plt.subplots(1,2,figsize=(11,4.4),layout="constrained")
    for p in ["direct","cost","shallow","balanced"]:
        selected=[next((r["summary"] for r in records if r["summary"]["scenario"]==n and r["summary"]["policy"]==p),None) for n in names]
        if any(s is None for s in selected):continue
        axes[0].plot(range(len(names)),[(s["delivery_p99_us"] or float("nan"))/1000 for s in selected],"o-",label=p,color=COLORS[p])
        axes[1].plot(range(len(names)),[s["deadline_pct"] for s in selected],"o-",label=p,color=COLORS[p])
    for ax in axes:
        ax.set_xticks(range(len(names)),["Base","Higher load","Root at 25%","Relays at 10%"])
        ax.grid(alpha=.2)
    axes[0].set(ylabel="Simulated p99 delivery (ms)")
    axes[1].set(ylabel="Objects meeting scenario deadline (%)",ylim=(-3,103))
    if axes[0].lines: axes[0].legend(frameon=False)
    fig.suptitle("Shared capacity and offered load can change the useful route",fontsize=12)
    fig.savefig(image/"capacity.png",dpi=160)
    plt.close(fig)
    fig,ax=plt.subplots(figsize=(9,4),layout="constrained")
    names=["regional-4k-chunks","regional-bulk","regional-whole-object"]
    for p in ["direct","cost","shallow","balanced"]:
        ss=[next((r["summary"] for r in records if r["summary"]["scenario"]==n and r["summary"]["policy"]==p),None) for n in names]
        if any(s is None for s in ss):continue
        ax.plot([4,64,1024],[(s["delivery_p99_us"] or float("nan"))/1000 for s in ss],"o-",color=COLORS[p],label=p)
    ax.set(xscale="log",xlabel="Forwarding chunk size (KiB; 1 MiB = whole object)",ylabel="Simulated p99 delivery (ms)",title="Chunking trades forwarding overlap against CPU/framing work")
    ax.set_xticks([4,64,1024],["4","64","1024"])
    ax.grid(alpha=.2)
    if ax.lines: ax.legend(frameon=False)
    fig.savefig(image/"chunks.png",dpi=160)
    plt.close(fig)
    online=["online-forwarder-failure","online-shared-cut","online-capacity-step","online-burst-pressure"]
    if all(any(r["summary"]["scenario"]==n for r in records) for n in online):
        fig,axes=plt.subplots(2,2,figsize=(11,7),layout="constrained")
        labels={"static":"Static + fixed repair","adaptive":"Observed / bounded","adaptive-unbounded":"Observed / unbounded","robust":"Robust / bounded"}
        for ax,name in zip(axes.flat,online):
            ss=[r["summary"] for r in records if r["summary"]["scenario"]==name]
            left=[0.0]*len(ss)
            values=[[s["deadline_pct"] for s in ss],
                    [100*s["complete"]/s["samples"]-s["deadline_pct"] for s in ss],
                    [100*s["rejected"]/s["samples"] for s in ss],
                    [100*(s["samples"]-s["complete"]-s["rejected"])/s["samples"] for s in ss]]
            for vs,color,label in zip(values,["#278674","#ddb665","#9aa6ae","#b85446"],
                                       ["On time","Complete, late","Refused","Admitted, unfinished"]):
                ax.barh(range(len(ss)),vs,left=left,color=color,label=label)
                for i,v in enumerate(vs):
                    if v>=9:ax.text(left[i]+v/2,i,f"{v:.0f}%",ha="center",va="center",fontsize=8)
                left=[a+b for a,b in zip(left,vs)]
            ax.set_yticks(range(len(ss)),[labels[s["policy"]] for s in ss],fontsize=8)
            ax.invert_yaxis()
            ax.set(xlim=(0,100),xlabel="Every offered object (%)",title=name.removeprefix("online-").replace("-"," ").title())
        handles,labels_=axes[0,0].get_legend_handles_labels()
        fig.legend(handles,labels_,loc="outside lower center",ncol=4,frameon=False)
        fig.suptitle("Recovery and overload: completing a subset is not completing the workload · synthetic",fontsize=12)
        fig.savefig(image/"resilience.png",dpi=160)
        plt.close(fig)


def report(out):
    config=json.loads((out/"config.json").read_text())
    records=json.loads((out/"results.json").read_text())
    all_records=records
    if (out/"selection.json").exists():
        selection=json.loads((out/"selection.json").read_text())
        selected={tuple(x) for x in selection["records"]}
        records=[r for r in records if (r["summary"]["scenario"],r["summary"]["policy"]) in selected]
        config={**config,"jobs":[{**j,"policies":[p for p in j["policies"] if (j["name"],p) in selected]}
                                for j in config["jobs"] if any(n==j["name"] for n,p in selected)]}
    compact=[]
    for r in records:
        first=r["runs"][0]
        topo=config["topologies"][r["summary"]["topology"]]
        edges={e["id"]:e for e in topo["edges"]}
        capacity={x["id"]:x["gbps"]*125*r["spec"].get("capacity_scale",1)*(1-r["spec"].get("background",{}).get(x["id"],0)) for x in topo["resources"]}
        integrated={k:0.0 for k in capacity}
        previous=0.0
        for change in sorted(r["spec"].get("capacity_events",[]),key=lambda x:x["time_us"]):
            now=min(change["time_us"],first["drain_us"])
            for k in capacity: integrated[k]+=capacity[k]*(now-previous)
            capacity[change["resource"]]*=change["factor"]
            previous=now
        for k in capacity: integrated[k]+=capacity[k]*(first["drain_us"]-previous)
        billed={}
        for run in r["runs"]:
            for eid,b in run["edge_bytes"].items():
                for category,rate in edges[eid]["charges"].items():
                    key=(category,rate)
                    billed[key]=billed.get(key,0)+b/r["summary"]["samples"]
        compact.append(dict(summary=r["summary"],spec=r["spec"],route=r["route"],planning=({**{k:v for k,v in r["planning"].items() if k not in ("candidates","frontier","balanced","latency","route","selected")},"candidate_count":len(r["planning"].get("candidates",[]))} if r.get("planning") else None),
                            controller_events=first.get("controller_events",[]), final_route=first.get("final_route",r["route"]),
                            trace=first["trace"],node_times=first["node_times"],resource_bytes=first["resource_bytes"],
                            resource_peak=first["resource_peak"],billed=[dict(category=k[0],rate=k[1],bytes=b) for k,b in billed.items()],
                            resource_capacity_bytes=integrated,
                            cpu_us=first["cpu_us"],drain_us=first["drain_us"],offered_us=first["offered_us"],
                            costs={k:sum(run["costs"].get(k,0) for run in r["runs"])/r["summary"]["samples"]
                                   for k in sorted({k for run in r["runs"] for k in run["costs"]})}))
    payload=json.dumps(dict(config=config,records=compact,colors=COLORS),separators=(",",":"))
    template=(Path(__file__).parent/"workbench.html").read_text()
    (out/"index.html").write_text(template.replace("__DATA__",payload.replace("</","<\\/")))
    figures(out,all_records)
    render_source=out/"render-source"
    render_source.mkdir(exist_ok=True)
    for p in [Path(__file__),Path(__file__).parent/"workbench.html"]:
        (render_source/p.name).write_bytes(p.read_bytes())
    manifest={"source_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),Path(__file__).parent/"workbench.html"]},"input_sha256":{n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in ["config.json","results.json","selection.json"] if (out/n).exists()}}
    (out/"report-provenance.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")


if __name__=="__main__": report(Path(sys.argv[1]))
