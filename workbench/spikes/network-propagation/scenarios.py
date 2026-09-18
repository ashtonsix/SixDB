"""Editable synthetic fixtures. These are assumptions, not cloud observations."""
import copy


def topology(global_=False):
    nodes, resources, edges = [], {}, []
    groups = [("a", "east", "A", "US"), ("b", "west" if global_ else "east", "A", "US"),
              ("c", "europe" if global_ else "east", "B" if global_ else "A", "EU" if global_ else "US")]
    def resource(name, gbps, **extra):
        resources.setdefault(name, dict(id=name, gbps=gbps, **extra))
        return name
    for j, (zone, region, provider, country) in enumerate(groups):
        for i in range(3):
            node = dict(id=f"{zone}{i}", zone=zone, region=region, provider=provider, location=country,
                        x=170+j*270, y=90+i*90, crypto_gbps=32, chunk_cpu_us=1, packet_cpu_us=.08)
            nodes.append(node)
    if global_:
        nodes.append(dict(id="edge", zone="edge", region="edge", provider="edge", location="US",
                          x=35, y=180, crypto_gbps=16, chunk_cpu_us=1, packet_cpu_us=.08))
    for n in nodes:
        resource("tx:"+n["id"], 2.5 if n["id"] == "edge" else 10)
        resource("rx:"+n["id"], 2.5 if n["id"] == "edge" else 10)
    def edge(a, b, kind, delay, gbps, charges, extra=(), request=0, setup=0):
        eid = a["id"]+">"+b["id"]+":"+kind
        rs = ["tx:"+a["id"], "rx:"+b["id"], resource("link:"+eid, gbps), *extra]
        edges.append(dict(id=eid, src=a["id"], dst=b["id"], kind=kind, delay_us=delay,
                          jitter_us=2 if delay < 500 else delay*.01, resources=rs, charges=charges,
                          locations=sorted({a["location"], b["location"]}),
                          jitter_domain=a["zone"]+">"+b["zone"], request_usd=request, setup_us=setup))
    for a in nodes:
        for b in nodes:
            if a == b:
                continue
            if a["zone"] == b["zone"]:
                edge(a,b,"local",25,10,{})
            elif not global_:
                delay = {frozenset("ab"):125, frozenset("ac"):175, frozenset("bc"):200}[frozenset([a["zone"], b["zone"]])]
                shared = resource("az:"+a["zone"]+">"+b["zone"], 10)
                edge(a,b,"cross-zone",delay,10,{"cross_zone":.02},[shared])
            elif "edge" in (a["id"], b["id"]):
                other = b if a["id"] == "edge" else a
                delay = {"a":12000,"b":38000,"c":45000}[other["zone"]]
                edge(a,b,"internet",delay,2.5,{"internet":.005 if a["id"] == "edge" else .09},
                     [resource("wan:"+a["region"],2.5)])
            else:
                delay = {frozenset("ab"):35000, frozenset("ac"):40000, frozenset("bc"):65000}[frozenset([a["zone"], b["zone"]])]
                extra = [resource("wan:"+a["region"],2.5)]
                charges = {"inter_region":.02} if a["provider"] == b["provider"] else {"internet":.09 if a["provider"] == "A" else .04}
                if a["provider"] != b["provider"]:
                    charges["nat"] = .045
                edge(a,b,"public",delay,5,charges,extra)
                # A provisioned gateway pair: optional lower variable price, slower/smaller link.
                if {a["id"],b["id"]} == {"a1","c0"}:
                    edge(a,b,"private",46000,1,{"private":.015},
                         [resource("private:US-EU",1,fixed_usd_hour=1.0)])
    return dict(name="global" if global_ else "regional", nodes=nodes, resources=list(resources.values()), edges=edges)


def fetch_topology():
    topo = topology(False)
    keep = {"a0","b0"}
    topo["nodes"] = [n for n in topo["nodes"] if n["id"] in keep]
    topo["edges"] = [e for e in topo["edges"] if e["src"] in keep and e["dst"] in keep]
    topo["nodes"].append(dict(id="blob",zone="service",region="east",provider="A",location="US",x=710,y=180,
                              crypto_gbps=40,chunk_cpu_us=1,packet_cpu_us=.08,serve_us=3000))
    topo["resources"] += [dict(id="tx:blob",gbps=5),dict(id="rx:blob",gbps=5)]
    for a,b in [("blob","b0"),("b0","blob")]:
        eid = a+">"+b+":endpoint"
        topo["resources"].append(dict(id="link:"+eid,gbps=5))
        topo["edges"].append(dict(id=eid,src=a,dst=b,kind="endpoint",delay_us=1500,jitter_us=100,
                                  resources=["tx:"+a,"rx:"+b,"link:"+eid],charges={},locations=["US"],
                                  request_usd=0.0000004 if a == "blob" else 0, jitter_domain="service"))
    topo["name"] = "fetch"
    return topo


def base():
    return dict(source="a0",targets=[f"{z}{i}" for z in "abc" for i in range(3) if z+str(i)!="a0"],
                size_bytes=1024*1024,chunk_bytes=65536,mtu=1500,messages=32,rate_per_s=100,
                deadline_us=10000,stretch=1.5,control_mode="direct",jitter_scale=1,packet_loss=0,
                control_weight=1,max_retries=1,mode="propagate")


def campaign(quick=False):
    tops = {"regional":topology(),"global":topology(True),"fetch":fetch_topology()}
    jobs = []
    def add(name, topo="regional", **changes):
        spec = base()
        spec.update(changes)
        jobs.append(dict(name=name,topology=topo,spec=spec,policies=["balanced","latency","shallow","direct","shortest","cost","bounded"]))
    add("regional-small",size_bytes=4096,messages=128,rate_per_s=10000,deadline_us=600)
    add("regional-bulk")
    add("regional-busy",rate_per_s=300,deadline_us=15000)
    add("regional-burst",burst=True,rate_per_s=180,deadline_us=15000)
    add("regional-whole-object",chunk_bytes=1024*1024)
    add("regional-4k-chunks",chunk_bytes=4096)
    add("regional-jumbo",mtu=9000)
    add("regional-slow-root",background={"tx:a0":.75},deadline_us=20000)
    add("regional-slow-relay",background={"tx:b0":.9,"tx:c0":.9},deadline_us=20000)
    # Egress forwarding failure; recipients remain alive. This is not AZ/VM failure.
    add("regional-forwarder-outage",failure=dict(source="b0",start_us=0,kinds=["data"]),deadline_us=10000)
    add("regional-repair",failure=dict(source="b0",start_us=0,kinds=["data"]),fallback_us=2000,deadline_us=10000)
    add("regional-common-cut",failure=dict(resource="az:a>b",start_us=0,kinds=["data"]),fallback_us=2000,deadline_us=10000)
    add("regional-loss",packet_loss=0.0001,deadline_us=10000)
    add("regional-loss-jumbo",packet_loss=0.0001,mtu=9000,deadline_us=10000)
    add("regional-control-tree",control_mode="tree",rate_per_s=300,deadline_us=15000)
    add("global-bulk","global",rate_per_s=15,deadline_us=100000)
    add("global-busy","global",rate_per_s=55,deadline_us=150000)
    add("global-small","global",size_bytes=4096,messages=128,rate_per_s=5000,deadline_us=90000)
    add("edge-origin","global",source="edge",targets=[f"{z}{i}" for z in "abc" for i in range(3)],rate_per_s=15,deadline_us=150000)
    add("US-residency","global",targets=[f"{z}{i}" for z in "ab" for i in range(3) if z+str(i)!="a0"],
        allowed_locations=["US"],rate_per_s=30,deadline_us=90000)
    add("global-tight-stretch","global",rate_per_s=15,deadline_us=100000,stretch=1.1)
    add("global-loose-stretch","global",rate_per_s=15,deadline_us=100000,stretch=3)
    for size in (1024,4096,16384,65536,1048576):
        add("fetch-"+str(size),"fetch",mode="fetch",source="a0",targets=["b0"],eligible_holders=["a0","blob"],
            size_bytes=size,messages=128 if size<65536 else 32,rate_per_s=2000 if size<65536 else 50,deadline_us=8000)
        jobs[-1]["policies"] = ["peer","blob","cheapest","bounded-fetch"]
    if quick:
        jobs = [j for j in jobs if j["name"] in ("regional-small","regional-bulk","global-bulk","fetch-4096")]
        for j in jobs:
            j["spec"]["messages"] = 4
    return dict(schema=1, label="Synthetic propagation and request-routing scenarios",seed_list=[17,29,43] if not quick else [17],
                topologies=tops,jobs=jobs)


def fetch_choice(topo, spec, policy):
    target = spec["targets"][0]
    choices = []
    for source in spec["eligible_holders"]:
        trial = copy.deepcopy(spec)
        trial["source"] = source
        out = topo.edges[topo.direct(source,target,trial)]
        back = topo.edges[topo.direct(target,source,trial)]
        latency = topo.delay(out,spec["size_bytes"],spec)+topo.delay(back,128,spec)+topo.nodes[source].get("serve_us",0)
        # Replay includes a request and a separate full-object receipt.
        cost = topo.cost(out,spec["size_bytes"],spec["chunk_bytes"],spec["mtu"])+2*topo.cost(back,128,128,spec["mtu"])
        choices.append((source,cost,latency))
    if policy in ("peer","blob"):
        source = "a0" if policy == "peer" else "blob"
    else:
        eligible = [c for c in choices if policy == "cheapest" or c[2] <= spec["deadline_us"]]
        if not eligible:
            raise ValueError("no fetch candidate meets the unloaded deadline estimate")
        source = min(eligible,key=lambda c:(c[1],c[2],c[0]))[0]
    result = copy.deepcopy(spec)
    result["source"] = source
    return result
