#!/usr/bin/env python3
"""Publication-style plots from retained numeric evidence (all latencies measured)."""
import argparse,csv,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                     'axes.spines.top': False, 'axes.spines.right': False})


def read(path):return list(csv.DictReader(path.open()))
def number(row,key):return float(row[key])


def persistence(evidence,out):
 rows=read(evidence/'persistence.csv')
 devices=[('nvme-arm','instance-store-0','i8g NVMe'),('nvme-i7i','instance-store-0','i7i NVMe'),('nvme-i4i','instance-store-0','i4i NVMe'),('hdd','instance-store-0','D3 HDD / FUA¹'),('sata-ssd','instance-store-0','I2 SSD'),('ebs-zen5','io2','io2 EBS'),('ebs-zen5','gp3','gp3 EBS')]
 fig,ax=plt.subplots(figsize=(10.5,5.5));colors=['#157a91','#49a89b','#df9634','#b44d5d']
 for i,(worker,device,label) in enumerate(devices):
  selected=[r for r in rows if r['worker']==worker and r['device']==device and r['bytes']=='4096' and r['stage']=='tail']
  best=min([r for r in selected if r['layout']!='extend'],key=lambda r:number(r,'p50_us'))
  ax.plot([number(best,'p50_us'),number(best,'p999_us')],[i,i],color='#cad2d7',lw=2,zorder=1)
  for color,p,marker in zip(colors,['p50','p90','p99','p999'],['o','s','D','^']):ax.scatter(number(best,p+'_us'),i,c=color,s=50,marker=marker,zorder=3,label=p.replace('p999','p99.9') if i==0 else None)
 ax.set_yticks(range(len(devices)),[d[2] for d in devices]);ax.invert_yaxis();ax.set_xscale('log');ax.set_xlim(7,1400)
 ax.set_xlabel('4 KiB synchronized write completion (µs, logarithmic scale)');ax.grid(axis='x',alpha=.2)
 ax.set_title('Fast NVMe medians still leave roughly 110 µs at p99.9',loc='left',weight='bold',pad=16)
 ax.legend(ncol=4,loc='upper center',bbox_to_anchor=(.53,-.17),frameon=False)
 fig.text(.015,.015,'Deployed platform + lowest-median candidate. 120k writes/setting; D3 30k. ¹FUA requested; physical PLP not certified.',fontsize=9,color='#52616b')
 fig.subplots_adjust(left=.18,right=.98,top=.88,bottom=.23);fig.savefig(out/'persistence.png',dpi=180);plt.close(fig)


def commits(evidence, out):
    rows = read(evidence / 'commits.csv')
    selected = []
    for device, layout, mtu in [('nvme', 'initialized', '9001'), ('gp3', 'extend', '1500')]:
        for placement in ['good', 'bad']:
            match = [r for r in rows if r['stage'] == 'tail' and r['bytes'] == '4096'
                     and r['surviving_only'] == 'False' and r['transport'] == 'tcp'
                     and (r['device'], r['layout'], r['mtu'], r['placement']) == (device, layout, mtu, placement)]
            assert len(match) == 1
            selected.append(match[0])
    fig, axes = plt.subplots(1, 2, figsize=(10.4, 5.2), sharey=True)
    colors = ['#157a91', '#49a89b', '#c27436', '#b44d5d']
    labels = ['NVMe, initialized\ngood placement', 'NVMe, initialized\nbad placement',
              'gp3, growing\ngood placement', 'gp3, growing\nbad placement']
    for ax, percentiles, title, limit in [
        (axes[0], ['p50', 'p90', 'p99', 'p999'], 'Full comparison', 5.5),
        (axes[1], ['p50', 'p90', 'p99', 'p999'], 'NVMe detail · same observations', 1.15)]:
        ax.set_xlim(0, limit)
        for i, row in enumerate(selected if ax is axes[0] else selected[:2]):
            for j, (p, color) in enumerate(zip(percentiles, colors)):
                y = i + (j - 1.5) * .13
                value = number(row, p + '_us') / 1000
                ax.plot([number(row, p + '_repeat_min_us') / 1000, number(row, p + '_repeat_max_us') / 1000], [y, y], color=color, lw=1.6)
                ax.scatter(value, y, color=color, s=28, marker=['o', 's', 'D', '^'][j], label=p.replace('p999', 'p99.9') if i == 0 else None, zorder=3)
            ax.text(number(row, 'p99_us') / 1000 + limit * .025, i + .07,
                    f"{number(row, 'p99_us') / 1000:.3f}", color=colors[2], fontsize=10)
        ax.axvline(1, color='#6b747a', ls='--', lw=1)
        ax.set_xlabel('Quorum commit latency (ms)')
        ax.set_title(title, loc='left', fontsize=12, pad=14)
        ax.grid(axis='x', alpha=.16)
    axes[0].set_yticks(range(4), labels); axes[0].invert_yaxis()
    axes[1].text(.96, .97, '1 ms budget', transform=axes[1].transAxes, ha='right', va='top', fontsize=10)
    axes[1].text(.06, .38, 'Matched NVMe detail:\nGood: leader az4 → followers az2, az1.\nBad: leader az6 → followers az4, az2.', transform=axes[1].transAxes, va='top', fontsize=10.5, color='#52616b', linespacing=1.6)
    axes[0].legend(ncol=4, loc='upper left', bbox_to_anchor=(-.01, 1.27), frameon=False, fontsize=10)
    fig.suptitle('Preparing the log and choosing nearby AZs reduce joint commit latency', x=.025, ha='left', fontsize=14, weight='bold')
    fig.text(.025, .055, 'Dots: pooled percentiles. Whiskers: range across three passes. Labels: p99. 120,000 commits per policy.', fontsize=10, color='#52616b')
    fig.text(.025, .017, '4 KiB, TCP; NVMe MTU 9001, gp3 MTU 1500. This compares whole policies. Client RPC and elections excluded.', fontsize=10, color='#52616b')
    fig.subplots_adjust(left=.21, right=.98, top=.75, bottom=.22, wspace=.20)
    fig.savefig(out / 'commits.png', dpi=180); plt.close(fig)


def throughput_frontier(evidence, out):
    rows = [r for r in read(evidence / 'throughput.csv') if r['stage'] == 'tail']
    colors = {'tcp': '#157a91', 'udp': '#c27436'}
    xmax = max(number(r, 'goodput_rps') for r in rows) / 1000 * 1.06
    full_high = max(1.25, max(number(r, 'p999_repeat_max_us') for r in rows) / 1000 * 1.2)
    from matplotlib.lines import Line2D
    handles = [Line2D([], [], color=c, marker='o', ls='', label=t.upper()) for t, c in colors.items()]
    if any(r['stable_all_passes'] != 'True' for r in rows):
        handles.append(Line2D([], [], color='#344751', marker='x', ls='', label='Fails stability in ≥1 pass'))
    for detail, name in [(True, 'throughput.png'), (False, 'throughput-full.png')]:
        fig, axes = plt.subplots(2, 2, figsize=(10.4, 7.4), sharex=True, sharey=True)
        high = 1.25 if detail else full_high
        log = not detail and full_high > 3
        for ax, p in zip(axes.flat, ['p50', 'p90', 'p99', 'p999']):
            ax.set_ylim(.3, high)
            if log:
                ax.set_yscale('log')
                ticks = [t for t in [.5, 1, 5, 10, 50, 100, 250] if t < high]
                ax.set_yticks(ticks, [str(t) for t in ticks])
            clipped = 0
            for r in rows:
                x, y = number(r, 'goodput_rps') / 1000, number(r, p + '_us') / 1000
                lo, hi = number(r, p + '_repeat_min_us') / 1000, number(r, p + '_repeat_max_us') / 1000
                color = colors[r['transport']]
                ax.plot([x, x], [lo, hi], color=color, lw=1.2, zorder=2)
                ax.scatter(x, y, color=color, marker='o' if r['stable_all_passes'] == 'True' else 'x', s=32, zorder=3)
                if detail and hi > high:
                    clipped += 1
                    ax.scatter(x, 1.225, marker='^', color=color, s=30, zorder=4)
            ax.axhline(1, color='#657179', ls='--', lw=.9)
            ax.set_xlim(-xmax * .025, xmax); ax.grid(alpha=.15)
            ax.set_title(p.replace('p999', 'p99.9'), loc='left', weight='bold', fontsize=12)
            if clipped:
                ax.text(1, 1.025, f'↑ {clipped} candidate ranges above view', transform=ax.transAxes,
                        ha='right', fontsize=9, color='#52616b')
            ax.set_ylabel('Arrival → commit (ms' + (', log)' if log else ')'))
        for ax in axes[-1]: ax.set_xlabel('Durable commits/s (thousands)')
        fig.legend(handles=handles, loc='lower left', bbox_to_anchor=(.06, .075), ncol=3, frameon=False, fontsize=10)
        title = 'Budget detail · read the margin around 1 ms' if detail else 'Full tail view · keep the expensive observations in sight'
        fig.suptitle(title, x=.075, ha='left', fontsize=15, weight='bold')
        fig.text(.075, .925, cohort_name(evidence) + ' · 4 KiB raw NVMe commits', fontsize=10, color='#52616b')
        fig.text(.075, .055, 'Dots: pooled percentiles; whiskers: three-pass ranges, not confidence intervals. Different points may use different policies.', fontsize=9, color='#52616b')
        fig.text(.075, .032, 'Triangles flag values above the detail view; the companion full view retains them. No interpolation across gaps.' if detail
                 else 'Every repeated candidate and its pass range is shown. Gaps are unmeasured; stability is a finite-run criterion.', fontsize=9, color='#52616b')
        fig.subplots_adjust(left=.09, right=.97, top=.84, bottom=.19, wspace=.16, hspace=.30)
        fig.savefig(out / name, dpi=180); plt.close(fig)


def operating_choices(evidence, out):
    selected = []
    for cohort, p, label in [('small', 'p999', 'Small · standard ENA\np99.9 budget'), ('scale', 'p999', 'Larger · standard ENA\np99.9 budget'),
                             ('express', 'p999', 'Larger · Express\np99.9 budget'), ('express', 'p99', 'Larger · Express\np99 budget')]:
        root = evidence / ('throughput-' + cohort)
        k = next(r for r in read(root / 'knees.csv') if r['transport'] == 'either' and r['percentile'] == p and r['criterion'] == 'below_1ms')
        candidates = [r for r in read(root / 'throughput.csv') if r['stage'] == 'tail'
                      and (r['transport'], r['window'], r['batch'], r['rate'], r['wait_us']) ==
                      (k['chosen_transport'], k['window'], k['batch'], k['rate'], k['wait_us'])]
        assert len(candidates) == 1
        selected.append((label, candidates[0]))
    fig, axes = plt.subplots(1, 2, figsize=(10.4, 5.2), sharey=True, gridspec_kw={'width_ratios': [1, 1.35]})
    for i, (label, r) in enumerate(selected):
        g = number(r, 'goodput_rps') / 1000
        axes[0].barh(i, g, height=.4, color='#157a91')
        axes[0].text(g + 5, i, f'{g:.1f}k', va='center', fontsize=11, weight='bold')
        for p, dy, color, marker in [('p99', -.12, '#c27436', 'D'), ('p999', .12, '#b44d5d', '^')]:
            lo, hi, v = [number(r, p + suffix) / 1000 for suffix in ['_repeat_min_us', '_repeat_max_us', '_us']]
            axes[1].plot([lo, hi], [i + dy] * 2, color=color, lw=2)
            axes[1].scatter(v, i + dy, color=color, s=35, marker=marker, label=p.replace('p999', 'p99.9') if i == 0 else None, zorder=3)
    axes[0].set_yticks(range(4), [s[0] for s in selected]); axes[0].invert_yaxis()
    axes[0].set_xlim(0, 285); axes[0].set_xlabel('Durable commits/s (thousands)')
    axes[1].set_xlim(.7, 1.2); axes[1].set_xlabel('Arrival → commit latency (ms)')
    axes[1].axvline(1, color='#657179', ls='--', lw=1)
    axes[1].text(1.01, -.55, '1 ms', fontsize=10, color='#52616b')
    axes[1].legend(ncol=2, loc='lower left', bbox_to_anchor=(0, 1.03), frameon=False)
    for ax in axes: ax.grid(axis='x', alpha=.15); ax.set_axisbelow(True)
    fig.suptitle('The percentile you protect changes the throughput you can choose', x=.025, ha='left', fontsize=14, weight='bold')
    fig.text(.025, .075, 'Greatest observed stable goodput within each pooled budget. All three passes stable; latency limits apply to the pool.', fontsize=9.5, color='#52616b')
    fig.text(.025, .04, 'Dots: pooled latency; whiskers: three-pass ranges. Standard-large p99.9 crosses 1 ms in one pass.', fontsize=10, color='#52616b')
    fig.text(.025, .005, '4 KiB raw NVMe, fixed good placement. Different platforms/policies on fresh cohorts; 23–45 measured seconds per choice.', fontsize=9.5, color='#52616b')
    fig.subplots_adjust(left=.31, right=.98, top=.79, bottom=.24, wspace=.27)
    fig.savefig(out / 'operating-choices.png', dpi=180); plt.close(fig)


def screen_reversal(evidence, out):
    rows = [r for r in read(evidence / 'throughput-passes.csv') if r['stage'] in ['open', 'tail']
            and (r['transport'], r['window'], r['batch'], r['rate'], r['wait_us']) == ('tcp', '64', '4', '104400', '50')]
    rows.sort(key=lambda r: (r['stage'] == 'tail', int(r['repeat'])))
    assert len(rows) == 4
    fig, axes = plt.subplots(1, 2, figsize=(10.4, 4.8), sharey=True)
    ys = np.arange(4)
    g = [number(r, 'goodput_rps') / 1000 for r in rows]
    axes[0].barh(ys, g, height=.42, color=['#157a91'] + ['#b44d5d'] * 3)
    for y, value in zip(ys, g): axes[0].text(value + 1.5, y, f'{value:.1f}k', va='center', fontsize=10)
    axes[0].set_xlim(0, 123); axes[0].set_xlabel('Durable commits/s (thousands)')
    labels = [(('Screen' if i == 0 else f'Repeat {i}') + f" · {number(r, 'measurement_seconds'):.1f} s") for i, r in enumerate(rows)]
    axes[0].set_yticks(ys, labels); axes[0].invert_yaxis()
    for p, offset, color in [('p50', -.13, '#157a91'), ('p99', .13, '#c27436')]:
        values = [number(r, p + '_us') / 1000 for r in rows]
        axes[1].scatter(values, ys + offset, color=color, s=40, marker='o' if p == 'p50' else 'D', label=p)
        for y, v in zip(ys + offset, values): axes[1].text(v * 1.15, y, f'{v:.3f}' if v < 1 else f'{v:.1f}', va='center', fontsize=10, color=color)
    axes[1].set_xscale('log'); axes[1].set_xlim(.35, 180)
    axes[1].set_xticks([.5, 1, 10, 100], ['0.5', '1', '10', '100'])
    axes[1].axvline(1, color='#657179', ls='--', lw=.9)
    axes[1].set_xlabel('Arrival → commit (ms, logarithmic)')
    axes[1].legend(ncol=2, loc='lower left', bbox_to_anchor=(0, 1.01), frameon=False)
    for ax in axes: ax.grid(axis='x', alpha=.15); ax.set_axisbelow(True)
    fig.suptitle('The rate holds up while the tail deteriorates by two orders of magnitude', x=.025, ha='left', fontsize=14, weight='bold')
    fig.text(.025, .08, 'Same i8g.large policy: TCP, W64, B4, 50 µs fill wait, 104,400 offered records/s. Separate runs, not a time series.', fontsize=10, color='#52616b')
    fig.text(.025, .035, 'Durations exclude warmup. Every repeat passes the goodput and drain checks but fails the queue-growth check.', fontsize=10, color='#52616b')
    fig.subplots_adjust(left=.19, right=.98, top=.77, bottom=.23, wspace=.25)
    fig.savefig(out / 'screen-reversal.png', dpi=180); plt.close(fig)


def cohort_name(evidence):
 nodes=json.loads((evidence/'throughput-nodes.json').read_text())
 instance=nodes[0]['peers'][0]['identity']['instanceType']
 modes={r['ena_srd_mode'] for r in read(evidence/'network-counters.csv')}
 mode='ENA Express' if modes=={'3'} else ('standard ENA' if modes=={'0'} else 'ENA mode recorded in context')
 mtu=read(evidence/'throughput.csv')[0]['mtu']
 return f'{instance} · {mode} · MTU {mtu}'+(' (burst-capable)' if instance=='i8g.large' else '')


def throughput(evidence,out):
 rows=[r for r in read(evidence/'throughput.csv') if r['stage']=='tail']
 knees=[r for r in read(evidence/'knees.csv') if r['transport']=='either']
 throughput_frontier(evidence,out)
 choices=[]
 for p in ['p50','p90','p99','p999']:
  for k in [r for r in knees if r['percentile']==p]:
   criterion={'within_10pct':'Within 10% of best','within_25pct':'Within 25% of best','below_1ms':'Below 1 ms'}[k['criterion']]
   if k['observed']!='True':choices.append([p.replace('p999','p99.9'),criterion,'Not observed','—','—','—']);continue
   config=f"{k['chosen_transport'].upper()}  W{k['window']} / B{k['batch']} / {k['wait_us']} µs"
   choices.append([p.replace('p999','p99.9'),criterion,f"{number(k,'latency_us')/1000:.3f}",f"{number(k,'goodput_rps')/1000:.1f}",f"{number(k,'throughput_sacrificed_pct'):.1f}%",config])
 fig,ax=plt.subplots(figsize=(12,6.5));ax.axis('off')
 table=ax.table(cellText=choices,colLabels=['Percentile','Latency allowance','Latency (ms)','k commits/s','Throughput given up','Configuration'],cellLoc='left',colLoc='left',colWidths=[.08,.20,.11,.11,.16,.34],bbox=[0,.12,1,.80])
 table.auto_set_font_size(False);table.set_fontsize(9.5)
 for (r,c),cell in table.get_celld().items():
  cell.set_edgecolor('#ffffff');cell.set_height(.061)
  if r==0:cell.set_facecolor('#173e4a');cell.get_text().set_color('white');cell.get_text().set_weight('bold')
  else:cell.set_facecolor('#eef3f4' if ((r-1)//3)%2==0 else '#fafafa')
 ax.set_title('Measured operating choices · '+cohort_name(evidence),loc='left',fontsize=14,weight='bold',pad=15)
 fig.text(.035,.078,'W = outstanding batches; B = maximum records/batch; µs = maximum batch-fill wait. Actual batches can be smaller.',fontsize=9.5,color='#52616b')
 fig.text(.035,.048,'Sacrifice is relative to the largest repeated stable goodput in this cohort. Each percentile has its own observed baseline.',fontsize=9.5,color='#52616b')
 fig.text(.035,.018,'Latency is pooled across three passes; pass ranges are in the companion frontier.',fontsize=9.5,color='#52616b')
 fig.subplots_adjust(left=.035,right=.97,top=.88,bottom=.06);fig.savefig(out/'choices.png',dpi=180);plt.close(fig)
 offered_load(evidence,out)


def offered_load(evidence,out):
 rows=read(evidence/'throughput.csv');screens=[r for r in rows if r['stage']=='open']
 tails=[r for r in rows if r['stage']=='tail' and r['stable_all_passes']=='True']
 def policy(r):return (r['transport'],r['window'],r['batch'],r['wait_us'])
 selected=[]
 for transport in ['tcp','udp']:
  vs=[r for r in tails if r['transport']==transport]
  if not vs:continue
  for r in [min(vs,key=lambda r:number(r,'p999_us')),max(vs,key=lambda r:number(r,'goodput_rps'))]:
   if policy(r) not in selected:selected.append(policy(r))
  candidates=[r for r in screens if r['transport']==transport and r['batch']=='4']
  if candidates:
   r=max(candidates,key=lambda r:number(r,'goodput_rps'))
   if policy(r) not in selected:selected.append(policy(r))
 fig,axes=plt.subplots(2,2,figsize=(11,8.3),sharex=True);palette=plt.get_cmap('tab10')
 metrics=[('goodput_rps',1000,'Goodput · dashed = offered rate','Commits/s (thousands)'),('p99_us',1000,'Arrival → commit · p99','ms (log scale)'),('queue_p99_us',1000,'Arrival → preparation · p99','ms (log scale)'),('max_sampled_backlog',1,'Peak uncommitted backlog','Records (log above 10)')]
 for i,key in enumerate(selected):
  vs=sorted([r for r in screens if policy(r)==key],key=lambda r:number(r,'offered_rps'))
  label=f'{key[0].upper()} W{key[1]} B{key[2]} wait {key[3]} µs'
  for ax,(metric,divisor,title,ylabel) in zip(axes.flat,metrics):
   xs=[number(r,'offered_rps')/1000 for r in vs];ys=[number(r,metric)/divisor for r in vs]
   ax.plot(xs,ys,color=palette(i),alpha=.65,lw=1,label=label)
   for x,y,r in zip(xs,ys,vs):ax.scatter(x,y,color=palette(i),marker='o' if r['stable_all_passes']=='True' else 'x',s=30)
   ax.set_ylabel(ylabel);ax.set_title(title,loc='left',fontsize=12,weight='bold');ax.grid(alpha=.15)
 limit=max(number(r,'offered_rps') for r in screens)/1000
 axes[0,0].plot([0,limit],[0,limit],color='#555',ls='--',lw=.8)
 for ax in [axes[0,1],axes[1,0]]:ax.set_yscale('log');ax.autoscale(axis='y')
 axes[1,1].set_yscale('symlog',linthresh=10);axes[1,1].set_ylim(bottom=0)
 axes[0,1].axhline(1,color='#555',ls='--',lw=.8)
 for ax in axes[-1]:ax.set_xlabel('Offered records/s (thousands)')
 handles,labels=axes[0,0].get_legend_handles_labels();fig.legend(handles,labels,loc='lower center',bbox_to_anchor=(.5,.045),ncol=3,fontsize=8,frameon=False)
 fig.suptitle('Queueing can rise sharply while goodput still looks healthy',x=.075,ha='left',weight='bold',fontsize=14)
 fig.text(.075,.925,cohort_name(evidence),fontsize=11,color='#52616b')
 fig.text(.075,.035,'Short screens: circles meet finite-run stability; crosses do not. W = retained batches; B = records/batch cap; wait = fill limit.',fontsize=9,color='#52616b')
 fig.text(.075,.015,'Lines join tested rates for one policy; intermediate rates remain unmeasured. Queueing stays inside commit latency.',fontsize=9,color='#52616b')
 fig.tight_layout(rect=[.02,.12,1,.90]);fig.savefig(out/'offered-load.png',dpi=180);plt.close(fig)


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 for file,fn in [('persistence.csv',persistence),('commits.csv',commits),('throughput.csv',throughput)]:
  if (a.evidence/file).exists():fn(a.evidence,a.output)
 for name in ['throughput-small','throughput-scale','throughput-express']:
  if (a.evidence/name/'throughput.csv').exists():
   (a.output/name).mkdir(parents=True,exist_ok=True);throughput(a.evidence/name,a.output/name)
 if all((a.evidence/('throughput-'+c)/'knees.csv').exists() for c in ['small','scale','express']):
  operating_choices(a.evidence,a.output)
 if (a.evidence/'throughput-small'/'throughput-passes.csv').exists():
  screen_reversal(a.evidence/'throughput-small',a.output/'throughput-small')


if __name__=='__main__':main()
