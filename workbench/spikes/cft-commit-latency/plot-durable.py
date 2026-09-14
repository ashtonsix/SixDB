#!/usr/bin/env python3
"""Publication-style plots from retained numeric evidence (all latencies measured)."""
import argparse,csv,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def read(path):return list(csv.DictReader(path.open()))
def number(row,key):return float(row[key])


def persistence(evidence,out):
 rows=read(evidence/'persistence.csv')
 devices=[('nvme-arm','instance-store-0','i8g NVMe'),('nvme-i7i','instance-store-0','i7i NVMe'),('nvme-i4i','instance-store-0','i4i NVMe'),('hdd','instance-store-0','D3 HDD / FUA¹'),('sata-ssd','instance-store-0','I2 SSD'),('ebs-zen5','io2','io2 EBS'),('ebs-zen5','gp3','gp3 EBS')]
 fig,ax=plt.subplots(figsize=(10.5,5.5));colors=['#157a91','#49a89b','#df9634','#b44d5d']
 for i,(worker,device,label) in enumerate(devices):
  selected=[r for r in rows if r['worker']==worker and r['device']==device and r['bytes']=='4096' and r['stage']=='tail']
  best=min([r for r in selected if r['layout']!='extend'],key=lambda r:number(r,'p50_us'))
  growth=next(r for r in selected if r['layout']=='extend')
  ax.plot([number(best,'p50_us'),number(best,'p999_us')],[i,i],color='#cad2d7',lw=2,zorder=1)
  for color,p in zip(colors,['p50','p90','p99','p999']):ax.scatter(number(best,p+'_us'),i,c=color,s=50,zorder=3,label=p.replace('p999','p99.9') if i==0 else None)
  ax.scatter(number(growth,'p50_us'),i,marker='|',s=130,c='#333333',label='Growing-file p50' if i==0 else None)
 ax.set_yticks(range(len(devices)),[d[2] for d in devices]);ax.invert_yaxis();ax.set_xscale('log');ax.set_xlim(7,5000)
 ax.set_xlabel('4 KiB power-safe write completion (µs, logarithmic scale)');ax.grid(axis='x',alpha=.2)
 ax.set_title('Persistence: low medians and long tails are separate results',loc='left',weight='bold',pad=16)
 ax.legend(ncol=5,loc='upper center',bbox_to_anchor=(.53,-.17),frameon=False)
 fig.text(.015,.015,'Deployed platform + lowest-median candidate. 120k writes/setting; D3 30k. ¹FUA requested; physical PLP not certified.',fontsize=9,color='#52616b')
 fig.subplots_adjust(left=.18,right=.98,top=.88,bottom=.23);fig.savefig(out/'persistence.png',dpi=180);plt.close(fig)


def commits(evidence,out):
 rows=read(evidence/'commits.csv');rows=[r for r in rows if r['stage']=='tail' and r['bytes']=='4096' and r['surviving_only']=='False']
 selected=[]
 for placement in ['good','bad']:
  nvme=[r for r in rows if r['placement']==placement and r['device']=='nvme' and r['transport']=='tcp' and r['layout']=='initialized' and r['mtu']=='9001']
  assert len(nvme)==1
  selected.append((placement+' AZs · NVMe\nTCP / initialized',nvme[0]))
  base=[r for r in rows if r['placement']==placement and r['device']=='gp3' and r['layout']=='extend' and r['mtu']=='1500']
  selected.append((placement+' AZs\ngp3 growing log',base[0]))
 fig,ax=plt.subplots(figsize=(9.5,5.5));x=np.arange(len(selected));colors=['#157a91','#49a89b','#df9634','#b44d5d']
 for j,(p,color) in enumerate(zip(['p50','p90','p99','p999'],colors)):
  ys=[number(r,p+'_us')/1000 for _,r in selected]
  spread=np.array([[abs(number(r,p+'_repeat_min_us')/1000-y) for (_,r),y in zip(selected,ys)],[abs(number(r,p+'_repeat_max_us')/1000-y) for (_,r),y in zip(selected,ys)]])
  ax.bar(x+(j-1.5)*.18,ys,.17,color=color,label=p.replace('p999','p99.9'),yerr=spread,error_kw=dict(ecolor='#333333',elinewidth=.7,capsize=2))
 ax.axhline(1,color='#444',ls='--',lw=1);ax.text(.985,1.04,'1 ms',transform=ax.get_yaxis_transform(),ha='right',color='#444',fontsize=9)
 ax.set_xticks(x,[name for name,_ in selected]);ax.set_ylabel('Durable quorum commit (ms)');ax.set_ylim(bottom=0);ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
 ax.set_title('Composed CFT fast path · 4 KiB records',loc='left',weight='bold',pad=16);ax.legend(ncol=4,frameon=False)
 fig.text(.02,.015,'Leader + one durable follower; 120k commits/setting. Whiskers: observed pass range. Client RPC/elections excluded.',fontsize=9,color='#52616b')
 fig.subplots_adjust(left=.10,right=.98,top=.88,bottom=.18);fig.savefig(out/'commits.png',dpi=180);plt.close(fig)


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
 fig,axes=plt.subplots(2,2,figsize=(11,8.5),sharex=True,sharey=True)
 colors={'tcp':'#157a91','udp':'#c27436'}
 low=min(number(r,'p50_repeat_min_us') for r in rows)/1400
 high=max(1100,max(number(r,'p999_repeat_max_us') for r in rows))*1.15/1000
 log_scale=high/low>10
 for ax,p in zip(axes.flat,['p50','p90','p99','p999']):
  for transport in ['tcp','udp']:
   values=[r for r in rows if r['transport']==transport]
   for stable,marker in [(True,'o'),(False,'x')]:
    vs=[r for r in values if (r['stable_all_passes']=='True')==stable]
    xs=[number(r,'goodput_rps')/1000 for r in vs];ys=[number(r,p+'_us')/1000 for r in vs]
    ax.scatter(xs,ys,color=colors[transport],marker=marker,s=35,alpha=.85 if stable else .4,label=transport.upper()+(' stable' if stable else ' unstable'))
    for x,y,r in zip(xs,ys,vs):
     ax.plot([x,x],[number(r,p+'_repeat_min_us')/1000,number(r,p+'_repeat_max_us')/1000],c=colors[transport],lw=.8,alpha=.7)
  landmarks={}
  for k in knees:
   if k['percentile']!=p or k['observed']!='True':continue
   label={'within_10pct':'+10%','within_25pct':'+25%','below_1ms':'<1 ms'}[k['criterion']]
   coord=(number(k,'goodput_rps')/1000,number(k,'latency_us')/1000)
   landmarks.setdefault(coord,[]).append(label)
  for j,((x,y),labels) in enumerate(landmarks.items()):
   ax.scatter(x,y,s=115,facecolors='none',edgecolors='#202c33',lw=1,zorder=4)
   left=x<.2*max(number(r,'goodput_rps')/1000 for r in rows)
   vertical=-(14+j*7) if y>high*.72 else 14+j*7
   ax.annotate('/'.join(labels),(x,y),xytext=(8 if left else -8,vertical),textcoords='offset points',ha='left' if left else 'right',fontsize=8,color='#202c33',
               arrowprops=dict(arrowstyle='-',color='#52616b',lw=.6),bbox=dict(facecolor='white',edgecolor='none',alpha=.85,pad=.2))
  ax.axhline(1,color='#606a70',ls='--',lw=.8);ax.set_yscale('log' if log_scale else 'linear');ax.set_ylim(low if log_scale else 0,high);ax.grid(alpha=.15)
  ax.set_title(p.replace('p999','p99.9'),loc='left',weight='bold');ax.set_ylabel('Scheduled arrival → commit (ms'+', log scale)' if log_scale else 'Scheduled arrival → commit (ms)')
 for ax in axes[-1]:ax.set_xlabel('Unique durable commits / second (thousands)')
 handles,labels=axes[0,0].get_legend_handles_labels();fig.legend(handles,labels,loc='lower center',bbox_to_anchor=(.5,.048),ncol=4,fontsize=8,frameon=False)
 fig.suptitle('Latency–throughput frontier · 4 KiB durable CFT commits',x=.075,ha='left',weight='bold',fontsize=14)
 fig.text(.075,.928,cohort_name(evidence),fontsize=11,color='#52616b')
 fig.text(.075,.025,'Three passes/point. Whiskers: observed variation. Gaps are unmeasured; stability is a finite-run criterion.',fontsize=9,color='#52616b')
 fig.tight_layout(rect=[.02,.09,1,.91]);fig.savefig(out/'throughput.png',dpi=180);plt.close(fig)
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
 fig,axes=plt.subplots(2,2,figsize=(11,8),sharex=True);palette=plt.get_cmap('tab10')
 metrics=[('goodput_rps',1000,'Unique durable commits/s (thousands)'),('p99_us',1000,'p99 arrival → commit (ms)'),('queue_p99_us',1000,'p99 arrival → batch preparation (ms)'),('max_sampled_backlog',1,'Maximum sampled uncommitted records')]
 for i,key in enumerate(selected):
  vs=sorted([r for r in screens if policy(r)==key],key=lambda r:number(r,'offered_rps'))
  label=f'{key[0].upper()} W{key[1]} B{key[2]} wait {key[3]} µs'
  for ax,(metric,divisor,ylabel) in zip(axes.flat,metrics):
   xs=[number(r,'offered_rps')/1000 for r in vs];ys=[number(r,metric)/divisor for r in vs]
   ax.plot(xs,ys,color=palette(i),alpha=.65,lw=1,label=label)
   for x,y,r in zip(xs,ys,vs):ax.scatter(x,y,color=palette(i),marker='o' if r['stable_all_passes']=='True' else 'x',s=30)
   ax.set_ylabel(ylabel);ax.grid(alpha=.15)
 limit=max(number(r,'offered_rps') for r in screens)/1000
 axes[0,0].plot([0,limit],[0,limit],color='#555',ls='--',lw=.8)
 for ax in [axes[0,1],axes[1,0]]:ax.set_yscale('log');ax.autoscale(axis='y')
 axes[1,1].set_yscale('symlog',linthresh=10);axes[1,1].set_ylim(bottom=0)
 axes[0,1].axhline(1,color='#555',ls='--',lw=.8)
 for ax in axes[-1]:ax.set_xlabel('Offered records / second (thousands)')
 handles,labels=axes[0,0].get_legend_handles_labels();fig.legend(handles,labels,loc='lower center',bbox_to_anchor=(.5,.045),ncol=3,fontsize=8,frameon=False)
 fig.suptitle('What happens as offered load rises?',x=.075,ha='left',weight='bold',fontsize=14)
 fig.text(.075,.925,cohort_name(evidence),fontsize=11,color='#52616b')
 fig.text(.075,.02,'Screen passes for selected policies. Circles meet finite-run stability; crosses do not. Queueing remains inside commit latency.',fontsize=9,color='#52616b')
 fig.tight_layout(rect=[.02,.12,1,.90]);fig.savefig(out/'offered-load.png',dpi=180);plt.close(fig)


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 for file,fn in [('persistence.csv',persistence),('commits.csv',commits),('throughput.csv',throughput)]:
  if (a.evidence/file).exists():fn(a.evidence,a.output)
 for name in ['throughput-small','throughput-scale','throughput-express']:
  if (a.evidence/name/'throughput.csv').exists():
   (a.output/name).mkdir(parents=True,exist_ok=True);throughput(a.evidence/name,a.output/name)


if __name__=='__main__':main()
