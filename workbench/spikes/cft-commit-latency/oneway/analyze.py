#!/usr/bin/env python3
"""Directional delay intervals from independent references; never fit link symmetry."""
import argparse
import bisect
import csv
import gzip
import json
import math
from pathlib import Path
import statistics

WALL_RATE = .001


def map_raw(event):
    age = event['sw_real'] - event['wall']
    if event['b'] < event['a']:
        raise ValueError('reversed raw bracket')
    # Observed age is in REALTIME units, so account for its slowest allowed rate.
    margin = (event['b']-event['a'])/2 + WALL_RATE*abs(age)/(1-WALL_RATE) + 1
    return (event['a']+event['b'])/2 + age, margin


def wall_rate_lower_bound(rows):
    rates = []
    wall = sorted((r for r in rows if r['kind']=='wall'),key=lambda r:r['a']+r['b'])
    for a,b in zip(wall,wall[1:]):
        dt = ((b['a']+b['b'])-(a['a']+a['b']))/2
        uncertainty = ((b['b']-b['a'])+(a['b']-a['a']))/2
        # The real interval can differ from the midpoint interval by uncertainty.
        rates.append(max(0,abs((b['utc']-a['utc'])-dt)-uncertainty)/(dt+uncertainty))
    return max(rates,default=0)


def quantile(values, p):
    values = sorted(values)
    if not values:
        return None
    x = (len(values) - 1) * p
    i = int(x)
    return values[i] + (values[min(i + 1, len(values) - 1)] - values[i]) * (x - i)


def write_csv(path, rows):
    if not rows:
        return
    with path.open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


class Clock:
    def __init__(self, path, ppm=100, point_model='affine'):
        assert point_model in ('affine', 'feasible')
        self.point_model = point_model
        self.rows = [json.loads(s) for s in path.read_text().splitlines()]
        self.wall_rate = wall_rate_lower_bound(self.rows)
        if self.wall_rate > WALL_RATE:
            raise ValueError('observed REALTIME/RAW step or rate exceeds mapping envelope')
        self.kind = 'phc+ntp' if any(r['kind'] == 'phc' for r in self.rows) else 'ntp'
        rows = sorted([r for r in self.rows if r['kind'] in ('phc','ntp')],key=lambda r:r['a']+r['b'])
        if not rows:
            raise ValueError(f'no clock reference: {path}')
        first = rows[0]
        self.origin = (first['utc'] if first['kind'] == 'phc' else first['t2']) - (first['a'] + first['b']) // 2
        self.rho = ppm * 1e-6
        self.anchors = []
        self.types = []
        for r in rows:
            a, b = r['a'], r['b']
            m = (a + b) / 2
            if r['kind'] == 'phc':
                # PHC bound is fetched with the PHC read, then exposed in sysfs.
                assert 0 < r['error_ns'] < 10**9, r
                lo = r['utc'] - self.origin - b - r['error_ns']
                hi = r['utc'] - self.origin - a + r['error_ns']
            else:
                # Server root distance + clock precision + conservative 16.16-field rounding.
                e = max(0, r['root_delay_ns']) / 2 + r['dispersion_ns'] + r['precision_ns'] + 1.5e9 / 65536
                lo = r['t3'] - self.origin - b - e
                hi = r['t2'] - self.origin - a + e
            # Transfer the timestamp bracket to its midpoint without assuming exact rate.
            margin = self.rho * (b - a) / 2
            self.anchors.append((m, lo - margin, hi + margin))
            self.types.append(r['kind'])
        self.times = [a[0] for a in self.anchors]
        self.max_gap = max(b-a for a,b in zip(self.times, self.times[1:]))
        self.inconsistent = 0
        # A single independently calibrated affine point trajectory avoids
        # artificial forward/reverse jumps as a moving fit window changes.
        # Its validity is checked against EVERY reference interval; interval
        # results below still allow non-affine drift within the rate envelope.
        weights = [1/max(1,hi-lo)**2 for _,lo,hi in self.anchors]
        total = sum(weights)
        self.fit_start = self.times[0]
        xs = [(t-self.fit_start)/1e9 for t,_,_ in self.anchors]
        ys = [(lo+hi)/2 for _,lo,hi in self.anchors]
        mx = sum(w*x for w,x in zip(weights,xs))/total
        my = sum(w*y for w,y in zip(weights,ys))/total
        self.fit_slope = sum(w*(x-mx)*(y-my) for w,x,y in zip(weights,xs,ys))/sum(w*(x-mx)**2 for w,x in zip(weights,xs))
        self.fit_intercept = my-self.fit_slope*mx
        if point_model == 'affine' and abs(self.fit_slope)/1e9 > self.rho:
            raise ValueError('fitted clock rate exceeds the requested envelope')
        violations = [max(lo-(self.fit_intercept+self.fit_slope*x),
                          (self.fit_intercept+self.fit_slope*x)-hi,0)
                      for x,(_,lo,hi) in zip(xs,self.anchors)]
        self.affine_violation = max(violations)
        if point_model == 'affine' and max(violations) > 1:
            raise ValueError(f'affine clock point model violates reference bounds by {max(violations)} ns')
        if point_model == 'feasible':
            # Intersect all independent reference intervals with the rate envelope,
            # in both time directions. No packet/link observation enters this fit.
            limits = [[lo, hi] for _, lo, hi in self.anchors]
            for indices in (range(1, len(limits)), range(len(limits)-2, -1, -1)):
                for i in indices:
                    j = i-indices.step
                    reach = self.rho * abs(self.times[i]-self.times[j])
                    limits[i][0] = max(limits[i][0], limits[j][0]-reach)
                    limits[i][1] = min(limits[i][1], limits[j][1]+reach)
                    if limits[i][0] > limits[i][1]:
                        raise ValueError('independent reference intervals contradict clock rate envelope')
            # Stay as close to the affine target as each step permits, while
            # retaining reachability of all future intervals. Linear interpolation
            # gives a continuous, rate-bounded curve through feasible anchor values.
            self.points = []
            for i, (lo, hi) in enumerate(limits):
                if i:
                    reach = self.rho * (self.times[i]-self.times[i-1])
                    lo, hi = max(lo, self.points[-1]-reach), min(hi, self.points[-1]+reach)
                if lo > hi:
                    if lo-hi > 1e-6:
                        raise ValueError(f'no feasible clock trajectory at anchor {i}: {lo-hi} ns gap')
                    lo = hi = (lo+hi)/2  # Sub-femtosecond floating-point boundary noise.
                target = self.fit_intercept+self.fit_slope*xs[i]
                self.points.append(min(hi, max(lo, target)))

    def point(self, t):
        if self.point_model == 'affine':
            return self.fit_intercept+self.fit_slope*(t-self.fit_start)/1e9
        j = bisect.bisect_right(self.times, t)
        if j == 0:
            return self.points[0]
        if j == len(self.times):
            return self.points[-1]
        fraction = (t-self.times[j-1])/(self.times[j]-self.times[j-1])
        return self.points[j-1]+fraction*(self.points[j]-self.points[j-1])

    def offset(self, t):
        left = bisect.bisect_left(self.times, t - 250000000)
        right = bisect.bisect_right(self.times, t + 250000000)
        if t < self.times[0] or t > self.times[-1]:
            raise ValueError('timestamp outside clock calibration coverage')
        # A timed-out reference request can leave a longer gap. Include both
        # surrounding anchors and widen by the full elapsed-time rate envelope.
        # Never substitute nominal sampling frequency for actual timestamps.
        position = bisect.bisect_left(self.times,t)
        left = min(left,max(0,position-1))
        right = max(right,min(len(self.times),position+1))
        anchors = self.anchors[left:right]
        lo = max(a - self.rho * abs(t-m) for m,a,b in anchors)
        hi = min(b + self.rho * abs(t-m) for m,a,b in anchors)
        if lo > hi:
            self.inconsistent += 1
            raise ValueError(f'inconsistent clock envelope ({lo-hi:.1f} ns); do not hide it by fitting')
        point = self.point(t)
        assert lo-1 <= point <= hi+1, (point,lo,hi)
        return point, lo, hi

    def summary(self, node):
        anchors = self.anchors
        start = anchors[0][0]
        bins = {}
        for m,a,b in anchors:
            bins.setdefault(int((m-start)/1e9), []).append((m, (a+b)/2))
        centers = [(statistics.mean(x[0] for x in v), statistics.median(x[1] for x in v)) for v in bins.values()]
        mx = statistics.mean(v[0] for v in centers)
        my = statistics.mean(v[1] for v in centers)
        slope = sum((x-mx)*(y-my) for x,y in centers) / sum((x-mx)**2 for x,y in centers)
        residual = [y-my-slope*(x-mx) for x,y in centers]
        ntp = [r for r in self.rows if r['kind']=='ntp']
        phc = [r for r in self.rows if r['kind']=='phc']
        extra = {}
        point_drift = self.fit_slope/1000
        if self.point_model == 'feasible':
            point_drift = (self.points[-1]-self.points[0])/(self.times[-1]-self.times[0])*1e6
            extra = dict(point_model=self.point_model, affine_target_drift_ppm=self.fit_slope/1000,
                affine_max_violation_us=self.affine_violation/1000,
                point_max_adjustment_us=max(abs(p-(self.fit_intercept+self.fit_slope*(t-self.fit_start)/1e9))
                    for t,p in zip(self.times,self.points))/1000,
                point_max_rate_ppm=max(abs(b-a)/(y-x)*1e6 for x,y,a,b in
                    zip(self.times,self.times[1:],self.points,self.points[1:]) if y>x))
        return dict(node=node, reference=self.kind, samples=len(anchors), drift_ppm=slope*1e6,
                    point_model_drift_ppm=point_drift,
                    fit_residual_p50_us=quantile([abs(r) for r in residual], .5)/1000,
                    fit_residual_p99_us=quantile([abs(r) for r in residual], .99)/1000,
                    anchor_halfwidth_p50_us=quantile([(b-a)/2000 for _,a,b in anchors], .5),
                    anchor_halfwidth_max_us=max((b-a)/2000 for _,a,b in anchors),
                    max_gap_ms=self.max_gap/1e6, assumed_rate_envelope_ppm=self.rho*1e6,
                    errors=sum(r['kind']=='error' for r in self.rows),
                    wall_rate_lower_bound_max_ppm=self.wall_rate*1e6,
                    phc_error_p50_us=quantile([r['error_ns']/1000 for r in phc], .5),
                    ntp_strata='/'.join(map(str, sorted(set(r['stratum'] for r in ntp)))),
                    ntp_root_distance_p50_us=quantile([(max(0,r['root_delay_ns'])/2+r['dispersion_ns'])/1000 for r in ntp],.5), **extra)

    def stamp(self, event):
        # Integer subtract before floating point: epoch nanoseconds exceed 2**53.
        raw, mapping = map_raw(event)
        point, lo, hi = self.offset(raw)
        # Moving the event in RAW also changes UTC-minus-RAW by up to rho.
        margin = mapping*(1+self.rho)
        return raw, point, lo-margin, hi+margin


def delay(tx, rx, a, b):
    at, ap, al, ah = a.stamp(tx)
    bt, bp, bl, bh = b.stamp(rx)
    base = (b.origin-a.origin) + (bt-at)
    return ((base+bp-ap)/1000, (base+bl-ah)/1000, (base+bh-al)/1000), (at, bt)


def causal_bounds(f, r, rtt, error):
    """Both legs are nonnegative and sum to the independently bounded RTT."""
    low, high = max(0,rtt-error), rtt+error
    fl,fh=max(0,f[1],low-r[2]),min(f[2],high-r[1],high)
    rl,rh=max(0,r[1],low-f[2]),min(r[2],high-f[1],high)
    if fl>fh or rl>rh:
        raise ValueError('clock bounds contradict packet causality')
    if not (fl <= f[0] <= fh and rl <= r[0] <= rh and low <= f[0]+r[0] <= high):
        raise ValueError('affine point model contradicts packet causality')
    dl=max(fl-rh,2*fl-high,low-2*rh)
    dh=min(fh-rl,2*fh-low,high-2*rl)
    return (f[0],fl,fh,f[1],f[2]),(r[0],rl,rh,r[1],r[2]),(f[0]-r[0],dl,dh,f[1]-r[2],f[2]-r[1])


def stats(rows):
    result = {'n': len(rows)}
    for label, p in [('p50', .5), ('p90', .9), ('p99', .99)]:
        for suffix, index in [('',0), ('_lo',1), ('_hi',2)]:
            result[label+suffix+'_us'] = quantile([r[index] for r in rows], p)
    result['min_us'] = min(r[0] for r in rows)
    result['max_us'] = max(r[0] for r in rows)
    if len(rows[0])>3:
        result['p50_clock_only_lo_us']=quantile([r[3] for r in rows],.5)
        result['p50_clock_only_hi_us']=quantile([r[4] for r in rows],.5)
    return result


def read_events(path):
    with gzip.open(path, 'rt') as f:
        return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(f)]


def join_events(client, server, src, dst, repeat, requested):
    events = {}
    for is_client, rows in ((True,client),(False,server)):
        for event in rows:
            if ((event['src'],event['dst'],event['pass']) != (src,dst,repeat)
                or not 0 <= event['seq'] < requested
                or event['reply'] not in (0,1) or event['tx'] not in (0,1)
                or (event['reply'] != event['tx']) != is_client):
                raise ValueError('unexpected exchange identity, sequence or endpoint role')
            key = event['seq'],event['reply'],event['tx']
            if key in events:
                raise ValueError('duplicate packet timestamp')
            events[key] = event
    return events


def analyze(directories, output, ppm=100, point_model='affine'):
    output.mkdir(parents=True, exist_ok=True)
    hosts = {}
    for directory in directories:
        identity = json.loads((directory / 'identity.json').read_text())
        n = identity['node']
        if n in hosts:
            raise ValueError('duplicate host identity')
        hosts[n] = dict(path=directory, identity=identity,
                        clock=Clock(directory/'calibration.jsonl', ppm, point_model),
                        cases=json.loads((directory/'cases.json').read_text()))
    az={n:int(h['identity']['az_id'].removeprefix('use1-az')) for n,h in hosts.items()}
    assert [az[n] for n in sorted(hosts)]==sorted(az.values()), 'node ordering must follow AZ ordering'
    clocks = [h['clock'].summary(n) for n,h in sorted(hosts.items())]
    write_csv(output/'clocks.csv', clocks)
    blocks, pairblocks, overhead, hardware = [], [], [], []
    directed, asymmetry = {}, {}
    checks = dict(exchanges=0, missing_exchanges=0, duplicate_events=0,
                  negative_point_delays=0, negative_upper_delays=0,
                  closure_max_abs_us=0, hardware_rx_stamps=0, requested_exchanges=0)
    cross_az_requests=0
    for n, host in sorted(hosts.items()):
        for case in host['cases']:
            if case['role'] != 'client':
                continue
            src, dst, repeat = case['src'], case['dst'], case['repeat']
            other = hosts[dst]
            peer_case = next(c for c in other['cases'] if c['key'] == case['key'])
            assert src == n and peer_case['role']=='server'
            assert (peer_case['src'],peer_case['dst'],peer_case['repeat'],peer_case['requested']) == (src,dst,repeat,case['requested'])
            events = join_events(read_events(host['path']/case['file']),
                                 read_events(other['path']/peer_case['file']),src,dst,repeat,case['requested'])
            checks['hardware_rx_stamps'] += sum(bool(e['hw']) for e in events.values())
            forward, reverse, asym, rtts, turnaround, closure = [], [], [], [], [], []
            checks['requested_exchanges'] += case['requested']
            if az[src] != az[dst]:
                cross_az_requests += case['requested']
            for seq in range(case['requested']):
                keys = [(seq,0,1),(seq,0,0),(seq,1,1),(seq,1,0)]
                if not all(k in events for k in keys):
                    checks['missing_exchanges'] += 1
                    continue
                tx1,rx2,tx3,rx4 = [events[k] for k in keys]
                f,(t1,t2) = delay(tx1,rx2,host['clock'],other['clock'])
                r,(t3,t4) = delay(tx3,rx4,other['clock'],host['clock'])
                checks['exchanges'] += 1
                checks['negative_point_delays'] += (f[0]<0)+(r[0]<0)
                checks['negative_upper_delays'] += (f[2]<0)+(r[2]<0)
                # Identically timestamped round-trip check, independent of clock offsets.
                network_rtt = ((t4-t1)-(t3-t2))/1000
                discrepancy = f[0]+r[0]-network_rtt
                checks['closure_max_abs_us'] = max(checks['closure_max_abs_us'], abs(discrepancy))
                mapping=sum(map_raw(e)[1] for e in (tx1,rx2,tx3,rx4))
                rho=ppm*1e-6
                rtt_error=(mapping*(1+rho)+rho*(abs(t4-t1)+abs(t3-t2)))/1000
                f,r,d=causal_bounds(f,r,network_rtt,rtt_error)
                if seq < 20:  # Explicit warmup; raw remains archived.
                    continue
                forward.append(f); reverse.append(r)
                # Always express pair asymmetry in ascending node/AZ direction.
                if src > dst:
                    d = (-d[0], -d[2], -d[1], -d[4], -d[3])
                asym.append(d)
                rtts.append(network_rtt)
                turnaround.append((t3-t2)/1000)
                closure.append(discrepancy)
                for node,e,raw in [(src,tx1,t1),(dst,rx2,t2),(dst,tx3,t3),(src,rx4,t4)]:
                    gap = (raw-e['app_raw'])/1000 if e['tx'] else (e['app_raw']-raw)/1000
                    overhead.append((node,e['tx'],gap))
                    if e['hw'] and not e['tx']:
                        c = hosts[node]['clock']
                        point,lo,hi = c.offset(raw)
                        hardware.append((node, ((c.origin-e['hw'])+raw+point)/1000))
            if not forward:
                raise ValueError('empty block')
            for a,b,values in [(src,dst,forward),(dst,src,reverse)]:
                az_a,az_b = az[a],az[b]
                directed.setdefault((az_a,az_b),[]).extend(values)
                blocks.append(dict(src=a,dst=b,az_src=az_a,az_dst=az_b,repeat=repeat,
                                   initiator=src,**stats(values)))
            azpair = tuple(sorted((az[src],az[dst])))
            asymmetry.setdefault(azpair,[]).extend(asym)
            pairblocks.append(dict(node_a=min(src,dst),node_b=max(src,dst),az_a=azpair[0],az_b=azpair[1],
                repeat=repeat,initiator=src,**stats(asym),rtt_p50_us=quantile(rtts,.5),
                rtt_p99_us=quantile(rtts,.99),turnaround_p50_us=quantile(turnaround,.5),
                closure_max_abs_us=max(abs(x) for x in closure)))
    summary = []
    for (a,b),values in sorted(directed.items()):
        selected = [r for r in blocks if (r['az_src'],r['az_dst']) == (a,b)]
        summary.append(dict(az_src=a,az_dst=b,**stats(values),
                            block_p50_min_us=min(r['p50_us'] for r in selected),
                            block_p50_max_us=max(r['p50_us'] for r in selected)))
    pairs = []
    for (a,b),values in sorted(asymmetry.items()):
        s = stats(values)
        selected = [r for r in pairblocks if (r['az_a'],r['az_b']) == (a,b)]
        positive = sum(r['p50_lo_us']>0 for r in selected)
        negative = sum(r['p50_hi_us']<0 for r in selected)
        pairs.append(dict(az_a=a,az_b=b,**s,positive_blocks=positive,negative_blocks=negative,
                          blocks=len(selected),block_p50_min_us=min(r['p50_us'] for r in selected),
                          block_p50_max_us=max(r['p50_us'] for r in selected)))
    write_csv(output/'directions.csv',summary)
    write_csv(output/'asymmetry.csv',pairs)
    write_csv(output/'blocks.csv',blocks)
    write_csv(output/'pair-blocks.csv',pairblocks)
    write_csv(output/'overhead.csv',[dict(node=n,tx=tx,n=len(v),p50_us=quantile(v,.5),p99_us=quantile(v,.99),max_us=max(v))
        for n in sorted(hosts) for tx in (0,1) if (v:=[r[2] for r in overhead if r[0]==n and r[1]==tx])])
    write_csv(output/'hardware-rx.csv',[dict(node=n,n=len(v),p50_us=quantile(v,.5),p99_us=quantile(v,.99),
                                           min_us=min(v),max_us=max(v))
        for n in sorted(hosts) if (v:=[r[1] for r in hardware if r[0]==n])])
    checks['cross_az_payload_bytes'] = cross_az_requests*2*64
    checks['cross_az_ipv4_bytes'] = cross_az_requests*2*(64+28)
    checks['warmup_exchanges_per_block'] = 20
    checks['clock_rate_envelope_ppm'] = ppm
    if point_model != 'affine':
        checks['clock_point_model'] = point_model
    (output/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
    print(json.dumps(checks,indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directories', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ppm', type=float, default=100)
    parser.add_argument('--clock-point-model', choices=('affine', 'feasible'), default='affine')
    args = parser.parse_args()
    analyze(args.directories,args.output,args.ppm,args.clock_point_model)
