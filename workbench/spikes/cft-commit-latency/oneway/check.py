#!/usr/bin/env python3
"""Check clock offset/drift/asymmetry separation, not only report formatting."""
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

from analyze import Clock, delay, causal_bounds, join_events
from calibrate import sample_phc

EPOCH = 1790000000000000000


def reference(path, boot, ppm, error=10000):
    rows = []
    for t in range(0, 2000000001, 50000000):
        rows.append(dict(kind='phc', a=t-100, b=t+100,
                         utc=EPOCH+boot+t+round(t*ppm*1e-6),error_ns=error))
    path.write_text('\n'.join(json.dumps(r) for r in rows)+'\n')
    return Clock(path)


def event(physical, boot, ppm, wall_bias):
    raw = round((physical-boot)/(1+ppm*1e-6))
    wall = EPOCH+physical+wall_bias
    return dict(a=raw-100,b=raw+100,wall=wall,sw_real=wall,app_raw=raw,hw=0)


with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    a = reference(path/'a', 100000000, 2)
    b = reference(path/'b', -200000000, -3)
    t1 = event(1000000000,100000000,2,8000000)
    t2 = event(1000150000,-200000000,-3,-9000000)
    t3 = event(1000160000,-200000000,-3,-9000000)
    t4 = event(1000510000,100000000,2,8000000)
    f,_ = delay(t1,t2,a,b)
    r,_ = delay(t3,t4,b,a)
    assert abs(f[0]-150) < .01 and f[1] <= 150 <= f[2], f
    assert abs(r[0]-350) < .01 and r[1] <= 350 <= r[2], r
    assert f[2]-r[1] < 0, (f,r)
    # A reference bias shifts apparent directions oppositely, while the RTT stays fixed.
    shifted = reference(path/'b2', -199970000, -3, error=50000)
    fs,_ = delay(t1,t2,a,shifted)
    rs,_ = delay(t3,t4,shifted,a)
    assert abs(fs[0]-f[0]-30) < .01
    assert abs(rs[0]-r[0]+30) < .01
    assert abs(fs[0]+rs[0]-f[0]-r[0]) < .01
    assert fs[1] <= 150 <= fs[2] and rs[1] <= 350 <= rs[2]
    cf,cr,cd=causal_bounds((150,-500,700),(350,-200,900),500,1)
    assert cf[1:3]==(0,501) and cr[1:3]==(0,501)
    assert cd[1]<=-200<=cd[2] and cf[3:]==(-500,700)
    # Local NTP path asymmetry belongs in bounds, not an assumed symmetric correction.
    rows = [dict(kind='ntp',a=t,b=t+100000,t2=EPOCH+t+80000,t3=EPOCH+t+80000,
                 root_delay_ns=0,dispersion_ns=0,precision_ns=1) for t in range(0,2000000000,50000000)]
    (path/'ntp').write_text('\n'.join(json.dumps(x) for x in rows)+'\n')
    n = Clock(path/'ntp')
    p,lo,hi = n.offset(1000000000)
    true_offset = EPOCH-n.origin
    assert lo <= true_offset <= hi and abs(p-true_offset) > 20000
    try:
        a.offset(3000000000)
        raise AssertionError('uncovered timestamp accepted')
    except ValueError:
        pass
    # A one-second-old stamp under the slowest permitted wall slew must include
    # its actual UTC, including transfer of raw-position error to UTC.
    exact = reference(path/'exact',0,0,error=1)
    old = dict(a=1000000000,b=1000000000,wall=EPOCH+999000000,sw_real=EPOCH)
    raw,point,lo,hi = exact.stamp(old)
    truth = EPOCH-exact.origin-raw
    assert lo <= truth <= hi, (truth,lo,hi)
    extra = [dict(kind='wall',a=t,b=t,utc=EPOCH+t+(10000000 if t else 0))
             for t in (0,50000000)]
    (path/'step').write_text((path/'exact').read_text()+'\n'.join(map(json.dumps,extra))+'\n')
    try:
        Clock(path/'step')
        raise AssertionError('detectable realtime step accepted')
    except ValueError:
        pass
    base=dict(src=0,dst=1,**{'pass':0},seq=0,reply=0,tx=1)
    assert len(join_events([base],[],0,1,0,1))==1
    for client,server in [([base,base],[]),([], [base]),([dict(base,seq=1)],[]),([dict(base,dst=2)],[])]:
        try:
            join_events(client,server,0,1,0,1)
            raise AssertionError('invalid event join accepted')
        except ValueError:
            pass
    try:
        causal_bounds((-1,-10,100),(51,0,100),50,1)
        raise AssertionError('infeasible affine point accepted')
    except ValueError:
        pass
    bound=path/'phc-bound';bound.write_text('25000')
    calls=[]
    def gettime(clock):
        calls.append(clock)
        if len(calls)==1:
            raise OSError(16,'Device or resource busy')
        return EPOCH
    original_open=open
    def guarded_open(filename,*args,**kwargs):
        if str(filename)==str(bound) and len(calls)<2:
            raise OSError(16,'cached PHC error bound invalid until refreshed')
        return original_open(filename,*args,**kwargs)
    with patch('calibrate.raw',return_value=100),patch('calibrate.time.clock_gettime_ns',side_effect=gettime),patch('builtins.open',side_effect=guarded_open):
        try:
            sample_phc(-5,[str(bound)])
            raise AssertionError('transient PHC failure hidden')
        except OSError:
            pass
        assert sample_phc(-5,[str(bound)])['error_ns']==25000
        assert len(calls)==2
    # A changing drift rate can invalidate an affine point despite consistent
    # references. The alternative must satisfy every reference and rate bound,
    # without using a link delay or making discontinuous window-fit jumps.
    rows = [dict(kind='phc', a=t, b=t, utc=EPOCH+t+int(min(t,2000000000-t)*20e-6),
                 error_ns=1000) for t in range(0,2000000001,50000000)]
    curve_path=path/'curve';curve_path.write_text('\n'.join(map(json.dumps,rows))+'\n')
    try:
        Clock(curve_path)
        raise AssertionError('invalid affine trajectory accepted')
    except ValueError:
        pass
    curve=Clock(curve_path,point_model='feasible')
    previous=None
    for t in range(0,2000000001,1000000):
        point,lo,hi=curve.offset(t)
        assert lo-1 <= point <= hi+1
        assert lo <= EPOCH-curve.origin+min(t,2000000000-t)*20e-6 <= hi
        if previous is not None:
            assert abs(point-previous) <= 100.000001  # 100 ppm over one millisecond.
        previous=point
    for (t,lo,hi),point in zip(curve.anchors,curve.points):
        assert lo <= point <= hi
    rows[20]['utc']+=1000000
    curve_path.write_text('\n'.join(map(json.dumps,rows))+'\n')
    try:
        Clock(curve_path,point_model='feasible')
        raise AssertionError('inconsistent reference intervals accepted')
    except ValueError:
        pass
print('Clock offset, drift, asymmetric NTP, uncertainty and coverage checks passed.')
