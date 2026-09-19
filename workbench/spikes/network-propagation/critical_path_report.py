#!/usr/bin/env python3
"""Render the selected tail diagnostics without replaying the simulator."""
import html
import json
from pathlib import Path
import sys


def report(out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    data=json.loads((out/'diagnostic.json').read_text())
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,
                         'axes.spines.top':False,'axes.spines.right':False})
    sections=[]
    for p in data['tail_paths']:
        fig,axes=plt.subplots(2,1,figsize=(12,8),layout='constrained',height_ratios=[1.5,1])
        color='#206a8b'; other='#94623d'
        edges=list(data['route'].values())
        # Show the selected object's work only here; overlap across lanes is real.
        for t in p['payload_transfers']:
            y=edges.index(t['edge'])
            a=(t['start_us']-p['release_us'])/1000
            b=(t['wire_end_us']-p['release_us'])/1000
            c=(t['delivered_us']-p['release_us'])/1000
            axes[0].barh(y,b-a,left=a,height=.5,color=color,edgecolor='white',linewidth=.25)
            axes[0].plot([b,c],[y,y],color='#91a8ad',lw=.8)
        axes[0].set_yticks(range(len(edges)),[e.split(':')[0].replace('>',' → ') for e in edges])
        axes[0].invert_yaxis()
        axes[0].set_title('This object’s 16 chunks overlap across the forwarding tree',loc='left',pad=14)
        axes[0].text(0,1.01,'Solid: transmission under contention · thin tail: propagation + receive CPU',
                     transform=axes[0].transAxes,fontsize=9,color='#52636a')
        lanes=['a0 TX','a1 TX','b2 TX','CPU','Propagation']
        chain=p['completion_dependency_chain']
        for i,c in enumerate(chain):
            label=(c['edge'].split('>')[0]+' TX' if c['stage']=='network service with sharing'
                   else 'Propagation' if c['stage']=='propagation' else 'CPU')
            y=lanes.index(label);a=c['start_us']/1000;b=c['end_us']/1000
            axes[1].barh(y,b-a,left=a,height=.52,color=color if c['message']==p['message'] else other)
            if i:
                prev=chain[i-1]
                prevlabel=(prev['edge'].split('>')[0]+' TX' if prev['stage']=='network service with sharing'
                           else 'Propagation' if prev['stage']=='propagation' else 'CPU')
                axes[1].plot([a,a],[lanes.index(prevlabel),y],color='#9ca9ae',lw=.6)
        axes[1].set_yticks(range(len(lanes)),lanes)
        axes[1].invert_yaxis()
        axes[1].set_title('Latest-prerequisite chain: earlier objects can determine this completion',loc='left',pad=14)
        axes[1].legend(handles=[Patch(color=color,label=f"Object {p['message']}"),
                                Patch(color=other,label='Earlier object’s work')],loc='lower left',fontsize=9)
        for ax in axes:
            ax.axvline(p['latency_us']/1000,color='#34474e',ls='--',lw=.8)
            ax.set_xlim(0,5.7)
            ax.grid(axis='x',alpha=.15)
            ax.set_xlabel('Elapsed from this object’s release (ms)')
        fig.suptitle(f"Synthetic tail trace · seed {p['seed']}, object {p['message']} · b1 completes at {p['latency_us']/1000:.3f} ms\n"
                     '1 MiB → eight recipients / nine hosts / three AZs · 100/s Poisson · assumed 10 Gbit/s',fontsize=13)
        image=f"tail-{p['message']}.png"
        fig.savefig(out/image,dpi=150)
        plt.close(fig)
        rows=''.join(f"<tr><td>{html.escape(s['edge'].split(':')[0].replace('>',' → '))}</td>"
                     f"<td>{html.escape(s['stage'])}</td><td>{s['start_us']/1000:.3f}</td>"
                     f"<td>{s['end_us']/1000:.3f}</td><td>{s['duration_us']:.1f}</td></tr>" for s in p['stages'])
        sections.append(f"<section><h2>Object {p['message']}: {p['latency_us']/1000:.3f} ms</h2>"
            f"<p>Seed {p['seed']}; absolute release {p['release_us']/1000:.6f} ms. "
            f"Last required recipient b1, completing chunk 15 (zero based). All receipts return at {p['confirmed_us']/1000:.3f} ms.</p>"
            f"<img src='{image}' alt='Overlapped chunk transmissions and the actual completion dependency chain'>"
            '<p>The lower chart follows the latest prerequisite at CPU and stream gates. Its non-overlapping intervals sum to delivery time. '
            'Network intervals already include reduced rates under sharing. Removing an earlier job would change those rates; '
            'this trace alone does not predict the savings.</p>'
            '<details><summary>Last chunk’s queue and service timestamps</summary><p>This separate accounting follows chunk 15 along its route. '
            'Queue waits contain earlier work, much of which overlaps useful transmission. They are not independent avoidable costs.</p>'
            '<table><tr><th>Edge</th><th>Stage</th><th>Start ms</th><th>End ms</th><th>Duration µs</th></tr>'+rows+'</table></details></section>')
    sensitivity=''.join('<tr><td>'+v['name']+'</td>'+''.join(f"<td>{v['percentiles_us'][str(q)]/1000:.3f}</td>" for q in (50,90,99,99.9))+'</tr>' for v in data['sensitivity'])
    page="""<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>What the 5.322 ms means · SixDB</title><style>
body{margin:0;background:#f3f6f5;color:#182d3b;font:16px/1.55 system-ui,sans-serif}main{max-width:1100px;margin:auto;padding:36px 24px}h1{font-size:36px;line-height:1.15}h2{font-size:24px}section{background:white;border:1px solid #d4dfe0;padding:24px;margin:24px 0;border-radius:8px}img{width:100%;height:auto}table{border-collapse:collapse;font-size:14px;width:100%}td,th{text-align:left;padding:7px 10px;border-bottom:1px solid #dbe2e3}summary{cursor:pointer;font-weight:650}.note{border-left:4px solid #206a8b;padding-left:16px}a{color:#206a8b}details{overflow:auto}code{font-size:14px}</style><main>
<p>SIXDB · SYNTHETIC NETWORK DIAGNOSTIC</p><h1>What the 5.322 ms means</h1>
<p class="note"><b>Full 1 MiB delivery to all eight recipients</b>—nine hosts across three modeled AZs.
From payload release to availability after receive CPU. No durable writes, consensus or returning receipts in this number.
64 KiB chunks, MTU 1500, 100 objects/s Poisson arrivals, shared 10 Gbit/s NICs. All figures below are simulated.</p>
<p>The 96-object run has p50 2.429 ms, p90 3.669 ms, p99 5.322 ms and p99.9 5.517 ms.
p99 is interpolated: <code>0.95 × 5.310826 + 0.05 × 5.538416</code>. There is no literal 5.322 ms object.
The following two actual objects bracket that percentile. Both were traced while replaying their complete seed;
every retained output field matched exactly with auditing enabled.</p>
<p>The burst contains objects 27, 28 and 29, released within 1.133 ms. Each requires two source copies.
At the assumed framing and 10 Gbit/s, source transmission alone consumes 1.883 ms per object;
the three objects demand 5.648 ms of source service. The long tail is principally backlog and capacity sharing.</p>
"""+''.join(sections)+"""<section><h2>Controlled sensitivities</h2><p>Same fixed route and seeds. Each row changes one stated input; these are not additive savings and the route is not reoptimized.
Isolated runs preserve each object’s jitter identity with no other offered object. The 25 Gbit/s row raises every network capacity pool, while leaving CPU and propagation unchanged.</p>
<table><tr><th>Case</th><th>p50 ms</th><th>p90 ms</th><th>p99 ms</th><th>p99.9 ms</th></tr>"""+sensitivity+"""</table>
<p>4 KiB chunks means a 1 MiB object split more finely. “4KiB same route” means a 4 KiB object instead.
Zero CPU cost is a limiting counterfactual, not an implementation proposal.</p></section>
<section><h2>Relation to xmem</h2><p>The nearby retained xmem results measure durable 4 KiB serial quorum commits,
including client acknowledgement, around 2 ms at p50 across five AZs. This bulk result measures concurrent full-copy delivery
of 256 times as many payload bytes at p99. The local evidence has seven- and ten-witness cases; it does not identify Ashton’s exact eight-VM run.</p>
<p>These are different workloads and cannot establish a speed ratio. The matched-payload 4 KiB sensitivity here gives
0.251 ms p99 network delivery under assumed hardware and delays; it does not predict durable commit latency.
96 synthetic samples also do not establish a production p99.9 or SLA.</p></section></main></html>"""
    (out/'index.html').write_text(page)


if __name__=='__main__': report(Path(sys.argv[1]))
