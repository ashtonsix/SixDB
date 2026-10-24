import csv,collections,sys
rows=list(csv.DictReader(open(sys.argv[1])))
for r in rows:
    for k in r:
        if k!='column': r[k]=int(r[k])
    r['gain']=max(0,r['prior_path']-r['sample_path'])
    r['pp_ex']=r['pp_low']+r['pp_high']
    r['prior_ex']=r['prior_low']+r['prior_high']
    r['dv']=abs(r['vbits']-r['prev_vbits'])
    r['vs_prev']=int(100*r['prior_path']/max(1,r['prior_bytes']))
    r['vs_full']=int(100*r['prior_path']/r['full'])
    r['tails8']=r['top_eighth']+r['bottom_eighth']
    r['tails4']=r['top_quarter']+r['bottom_quarter']
    r['dK']=abs(r['pp_width']-r['prior_width'])
    r['atfull']=int(r['pp_width']>=r['vbits'])
G=sum(r['gain'] for r in rows); N=len(rows)
feats=['distinct16','pp_ex','prior_ex','dv','vs_prev','vs_full','tails8','tails4','dK','atfull','prior_low','prior_high','top_eighth','bottom_eighth']
cands=[]
for ft in feats:
    vals=sorted(set(r[ft] for r in rows))
    for q in sorted(set(vals[int(i*(len(vals)-1)/30)] for i in range(31))):
        cands.append((f"{ft}<={q}", lambda r,ft=ft,q=q: r[ft]<=q))
        cands.append((f"{ft}>={q}", lambda r,ft=ft,q=q: r[ft]>=q))
chosen=[]; fired=[False]*N
for step in range(6):
    best=None
    for name,f in cands:
        nf=[fired[i] or f(rows[i]) for i in range(N)]
        fr=sum(nf)/N; cov=sum(rows[i]['gain'] for i in range(N) if nf[i])/G
        score=cov-1.5*fr
        if best is None or score>best[0]: best=(score,name,nf,fr,cov)
    chosen.append(best[1]); fired=best[2]
    print(f"OR {best[1]:22} fires {best[3]*100:5.1f}%  covers {best[4]*100:5.1f}%")
by=collections.defaultdict(lambda:[0,0,0,0,0])
for i,r in enumerate(rows):
    b=by[r['column']]; b[0]+=r['exact']; b[1]+=r['sample_path']; b[2]+= r['sample_path'] if fired[i] else min(r['prior_path'], r['prior_path']); b[3]+=fired[i]; b[4]+=1
for c,b in by.items(): print(f"{c:16} gated regret {100*(b[2]/b[0]-1):6.2f}%  sample path {100*(b[1]/b[0]-1):5.2f}%  fallback {100*b[3]/b[4]:5.1f}%")
