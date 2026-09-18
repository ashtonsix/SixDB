"""Hand-solvable apparatus checks, including failures and charge conservation."""
import copy
import itertools
import math
import unittest

from model import Topology, Simulator, Flow, fair_rates, plan, validate_plan, wire
from scenarios import base, topology
from planner import optimize, depths, planning_spec


def toy(names="abc", gbps=1):
    nodes = [dict(id=n,zone=n,location="US",crypto_gbps=1e100,packet_cpu_us=0,chunk_cpu_us=0) for n in names]
    rs = [dict(id=d+":"+n,gbps=gbps) for n in names for d in ("tx","rx")]
    es = [dict(id=a+">"+b,src=a,dst=b,delay_us=10,resources=["tx:"+a,"rx:"+b],charges={"transfer":.02},locations=["US"])
          for a in names for b in names if a!=b]
    return dict(name="toy",nodes=nodes,resources=rs,edges=es)


def spec(**kwargs):
    s = base()
    s.update(source="a",targets=["b","c"],messages=1,size_bytes=1000,chunk_bytes=1000,control_mode="none",jitter_scale=0)
    s.update(kwargs)
    return s


class Checks(unittest.TestCase):
    def test_shared_nic(self):
        t = Topology(toy())
        r = Simulator(t,spec(),{"b":"a>b","c":"a>c"}).run()
        self.assertAlmostEqual(r["rows"][0]["last_us"],2*wire(1000)[0]/125+10,places=6)
        self.assertTrue(all(v<=1+1e-8 for v in r["resource_peak"].values()))

    def test_chunk_pipeline_and_partial_tail(self):
        t = Topology(toy())
        for count in [1,2,5]:
            r = Simulator(t,spec(targets=["c"],size_bytes=count*1000),{"b":"a>b","c":"b>c"}).run()
            self.assertAlmostEqual(r["rows"][0]["last_us"],(count+1)*wire(1000)[0]/125+20,places=6)
        r = Simulator(t,spec(targets=["c"],size_bytes=2100),{"b":"a>b","c":"b>c"},trace=True).run()
        data = [x for x in r["trace"] if x["kind"]=="data"]
        self.assertEqual(sum(x["wire_bytes"] for x in data),2*(2*wire(1000)[0]+wire(100)[0]))
        for x in data:
            if x["src"]=="b":
                upstream = next(y for y in data if y["src"]=="a" and y["chunk"]==x["chunk"])
                self.assertGreaterEqual(x["start_us"]+1e-7,upstream["arrival_us"])

    def test_weighted_sharing(self):
        def f(rs,w=1): return Flow(100,rs,w,lambda:None,0,"",0,"data",0,100)
        rates = fair_rates({0:f(("nic","wan")),1:f(("nic",)),2:f(("wan",))},{"nic":10,"wan":4})
        self.assertEqual(rates,{0:2,1:8,2:2})
        self.assertEqual(fair_rates({0:f(("nic",),1),1:f(("nic",),3)},{"nic":10}),{0:2.5,1:7.5})

    def test_setup_gates_the_whole_stream(self):
        d=toy()
        for e in d["edges"]: e["setup_us"]=1000
        r=Simulator(Topology(d),spec(targets=["b"],size_bytes=2000),{"b":"a>b"},trace=True).run()
        self.assertTrue(all(t["start_us"]>=1000 for t in r["trace"] if t["kind"]=="data"))
        self.assertAlmostEqual(r["rows"][0]["last_us"],1000+2*wire(1000)[0]/125+10,places=6)

    def test_request_not_per_chunk(self):
        d = toy()
        for e in d["edges"]: e["request_usd"] = .1
        t = Topology(d)
        r = Simulator(t,spec(targets=["c"],size_bytes=2500),{"b":"a>b","c":"b>c"}).run()
        self.assertAlmostEqual(r["costs"]["requests"],.2)
        self.assertAlmostEqual(r["costs"]["transfer"],r["wire_bytes"]*.02/1e9)

    def test_cost_tree_against_exhaustive_parents(self):
        d = toy("abcd")
        for i,e in enumerate(d["edges"]):
            e["charges"] = {"transfer":((i*7+3)%11)/100}
        t = Topology(d)
        s = spec(targets=list("bcd"))
        p = plan(t,s,"cost")
        price = lambda q: sum(t.cost(t.edges[e],1000,1000,1500) for e in q.values())
        best = math.inf
        options = [[e["id"] for e in d["edges"] if e["dst"]==n] for n in "bcd"]
        for ids in itertools.product(*options):
            candidate = dict(zip("bcd",ids))
            try: validate_plan(t,"a",list("bcd"),candidate)
            except ValueError: continue
            best = min(best,price(candidate))
        self.assertAlmostEqual(price(p),best,places=15)
        with self.assertRaises(ValueError): validate_plan(t,"a",list("bc"),{"b":"c>b","c":"b>c"})

    def test_repair_preserves_missing_and_cost(self):
        t = Topology(toy())
        s = spec(targets=["c"],source="a",size_bytes=2500,control_mode="direct",max_retries=0,
                 failure=dict(source="b",start_us=0),fallback_us=100)
        route = {"b":"a>b","c":"b>c"}
        failed = Simulator(t,dict(s,fallback_us=None),route).run()
        repaired = Simulator(t,s,route).run()
        self.assertIsNone(failed["rows"][0]["last_us"])
        self.assertIsNotNone(repaired["rows"][0]["last_us"])
        self.assertGreater(repaired["wire_bytes"],failed["wire_bytes"])
        self.assertGreater(repaired["rows"][0]["last_us"],100)
        self.assertEqual(repaired["repairs"],1)

    def test_partial_blackhole_never_forwards(self):
        t = Topology(toy())
        s = spec(targets=["c"],failure=dict(edge="a>b",start_us=2,end_us=4),max_retries=0)
        r = Simulator(t,s,{"b":"a>b","c":"b>c"},trace=True).run()
        self.assertIsNone(r["rows"][0]["last_us"])
        self.assertFalse(any(x["edge"]=="b>c" and x["kind"]=="data" for x in r["trace"]))

    def test_residency_and_repeatability(self):
        t = Topology(topology(True))
        s = base()
        s.update(targets=["a1","b0"],allowed_locations=["US"],messages=2,size_bytes=4096)
        for policy in ["direct","shortest","cost","bounded"]:
            p = plan(t,s,policy)
            self.assertTrue(all(t.legal(t.edges[e],s) for e in p.values()))
            a,b = Simulator(t,s,p,17).run(),Simulator(t,s,p,17).run()
            self.assertEqual(a,b)
        with self.assertRaises(ValueError): plan(t,dict(s,targets=["c0"]),"direct")

    def test_direct_metadata_uses_resources(self):
        t = Topology(toy())
        route = {"b":"a>b","c":"a>c"}
        plain = Simulator(t,spec(),route).run()
        control = Simulator(t,spec(control_mode="direct"),route).run()
        self.assertGreater(control["wire_bytes"],plain["wire_bytes"])
        self.assertGreater(control["rows"][0]["last_us"],plain["rows"][0]["last_us"])

    def test_branch_chain_crossover(self):
        # Root b has the complete object: branch shares b's TX; chain pipelines.
        d = toy("bcd")
        # Isolate the hand-solvable data formula from return-receipt contention.
        d["resources"].append(dict(id="receipt",gbps=1e6))
        for e in d["edges"]:
            if e["dst"] == "b": e["resources"]=["receipt"]
        t = Topology(d)
        for k in [1, 5]:
            s = spec(source="b", targets=["c","d"], size_bytes=k*1000)
            branch = Simulator(t,s,{"c":"b>c","d":"b>d"}).run()["rows"][0]["last_us"]
            chain = Simulator(t,s,{"c":"b>c","d":"c>d"}).run()["rows"][0]["last_us"]
            serialization = wire(1000)[0]/125
            self.assertAlmostEqual(branch,2*k*serialization+10)
            self.assertAlmostEqual(chain,(k+1)*serialization+20)
            self.assertEqual(branch < chain,k==1)

    def test_multiobjective_against_tiny_exhaustive_frontier(self):
        # Price and latency genuinely disagree: crossing from a is expensive,
        # the local b/c transfer is cheap. Enumerate every rooted tree.
        d=toy()
        for e in d["edges"]:
            e["charges"]={"transfer":.05 if e["src"]=="a" else .001}
        t=Topology(d)
        s=spec(planner_messages=1,planner_slack=.1)
        o=optimize(t,s)
        alternatives=[]
        for ids in itertools.product(["a>b","c>b"],["a>c","b>c"]):
            p=dict(zip("bc",ids))
            try: validate_plan(t,"a",list("bc"),p)
            except ValueError: continue
            r=Simulator(t,s,p,271828).run()
            alternatives.append((r["rows"][0]["last_us"],sum(r["costs"].values()),p))
        best=min(x[0] for x in alternatives)
        chosen=Simulator(t,s,o["balanced"],271828).run()
        self.assertLessEqual(chosen["rows"][0]["last_us"],best*1.1)
        self.assertAlmostEqual(sum(chosen["costs"].values()),min(x[1] for x in alternatives if x[0]<=best*1.1))
        self.assertEqual(depths(t,s,o["balanced"])[0],1)

    def test_planning_does_not_see_future_faults_or_evaluation_seed(self):
        t=Topology(toy())
        s=spec(planner_messages=2)
        o=optimize(t,s)
        failure=optimize(t,dict(s,failure=dict(source="b",start_us=0),packet_loss=.5,jitter_scale=100))
        self.assertEqual(o["balanced"],failure["balanced"])
        p={"b":"a>b","c":"a>c"}
        self.assertEqual(Simulator(t,s,p,17).run(),Simulator(t,s,dict(reversed(list(p.items()))),17).run())
        with self.assertRaises(ValueError): Simulator(t,s,{"b":"c>b","c":"b>c"})

    def test_planning_preserves_bursts_and_repair_policy(self):
        t=Topology(toy())
        s=planning_spec(spec(burst=True,fallback_us=1,control_mode="direct"))
        p={"b":"a>b","c":"a>c"}
        run=Simulator(t,s,p,271828).run()
        normal=Simulator(t,dict(s,burst=False),p,271828).run()
        ratios=[]
        for i in range(1,len(run["rows"])):
            a=run["rows"][i]["release_us"]-run["rows"][i-1]["release_us"]
            b=normal["rows"][i]["release_us"]-normal["rows"][i-1]["release_us"]
            ratios.append(a/b)
        self.assertAlmostEqual(min(ratios),1/3)
        self.assertAlmostEqual(max(ratios),1/.6)
        self.assertGreater(run["repairs"],0)

    def test_observed_rewrite_and_confirmed_holder_repair(self):
        d=toy("abcd")
        for e in d["edges"]:
            if e["id"]=="a>c": e["delay_us"]=100
        t=Topology(d)
        s=spec(targets=list("bcd"),messages=3,rate_per_s=1000,control_mode="direct",adaptive=True,
               failure=dict(source="b",start_us=0),max_retries=0,fallback_us=100,rewrite_hold_us=100)
        p={"b":"a>b","c":"b>c","d":"a>d"}
        sim=Simulator(t,s,p,17)
        r=sim.run()
        self.assertTrue(all(x["last_us"] is not None for x in r["rows"]))
        rewrite=next(x for x in r["controller_events"] if x["kind"]=="rewrite")
        self.assertGreater(rewrite["time_us"],100)
        self.assertEqual(sim.route_versions[0],p)
        self.assertEqual(sim.route_versions[1]["c"],"d>c")
        repairs=[x for x in r["controller_events"] if x["kind"]=="repair"]
        self.assertEqual(repairs[0]["holder"],"d")
        self.assertGreater(r["bytes"]["repair_command"],0)
        self.assertLess(sim.messages[0]["confirmed"]["d"],repairs[0]["time_us"])

    def test_progress_avoids_healthy_repair_and_admission_stays_bounded(self):
        t=Topology(toy())
        s=spec(size_bytes=20000,chunk_bytes=1000,control_mode="direct",fallback_us=100)
        p={"b":"a>b","c":"a>c"}
        static=Simulator(t,s,p).run()
        moving=Simulator(t,dict(s,adaptive=True),p).run()
        self.assertGreater(static["repairs"],0)
        self.assertEqual(moving["repairs"],0)
        r=Simulator(t,dict(s,adaptive=True,messages=20,rate_per_s=1e6,max_inflight_objects=2),p).run()
        self.assertEqual(len(r["rows"]),20)
        self.assertEqual(r["peak_active"],2)
        self.assertEqual(r["rejected"],18)
        self.assertEqual(sum(x["last_us"] is not None for x in r["rows"]),2)

    def test_robust_ensemble_incomplete_and_non_oracle(self):
        from resilience import select
        t=Topology(toy())
        s=spec(robust_messages=2,design_failure_sources=["b"],max_retries=0)
        direct={"b":"a>b","c":"a>c"}
        chain={"b":"a>b","c":"b>c"}
        result=select(t,s,[direct,chain])
        self.assertEqual(result["route"],direct)
        broken=select(t,s,[chain])
        self.assertEqual(broken["status"],"no finite latency bound found")
        self.assertIsNone(broken["latency_budget_us"])
        self.assertEqual(result,select(t,dict(s,failure=dict(source="a",start_us=7),capacity_events=[dict(time_us=3,resource="tx:a",factor=.1)]),[direct,chain]))
        rejected=select(t,dict(s,max_inflight_objects=0),[direct])
        self.assertEqual(rejected["selected"]["risk"],(1,1))

    def test_shared_cut_inference_uses_path_evidence(self):
        t=Topology(topology())
        s=dict(base(),messages=2,size_bytes=4096,adaptive=True,fallback_us=1000,jitter_scale=0,
               max_retries=0,failure=dict(resource="az:a>b",start_us=0))
        route=plan(t,s,"shallow")
        sim=Simulator(t,s,route,17)
        r=sim.run()
        self.assertTrue(all(x["last_us"] is not None for x in r["rows"]))
        pool=next(x for x in r["controller_events"] if x["kind"]=="suspect-pool")
        evidence={x["edge"] for x in r["controller_events"] if x["kind"]=="suspect" and x["time_us"]<=pool["time_us"]}
        self.assertGreaterEqual(sum(pool["resource"] in t.edges[e]["resources"] for e in evidence),2)
        self.assertFalse(any("az:a>b" in t.edges[e]["resources"] for e in r["final_route"].values()))
        self.assertLess(r["rows"][1]["last_us"],r["rows"][0]["last_us"])


if __name__ == "__main__":
    unittest.main()
