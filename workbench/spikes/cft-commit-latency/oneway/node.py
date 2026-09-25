#!/usr/bin/env python3
"""Disjoint host-pair blocks, independent clocks and a fixed packet budget."""
import gzip
import http.server
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from worker_context import GroupContext

OUT = Path(os.environ['SIXDB_RESULTS'])
NODE = int(os.environ['AZ_NODE'])
HERE = Path(__file__).resolve().parent
NODES = int(os.environ.get('ONEWAY_NODES','12'))
FLOWS = int(os.environ.get('ONEWAY_FLOWS','1'))
ROUNDS = int(os.environ.get('ONEWAY_ROUNDS','4'))
LAYOUT = os.environ.get('ONEWAY_LAYOUT','full')
PORT_MODE = os.environ.get('ONEWAY_PORT_MODE','both')
SAMPLING = PORT_MODE == 'port-sampling'
assert NODES in (8,12) and 1 <= FLOWS <= (64 if SAMPLING else 16) and ROUNDS in (4,5)
COUNT = int(os.environ.get('ONEWAY_COUNT','1000'))
PACE_US = int(os.environ.get('ONEWAY_PACE_US','5000'))
assert 20 < COUNT <= 1000
assert 2000 <= PACE_US <= 100000
ctx = GroupContext.from_env()


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs).stdout


def metadata(path):
    base = 'http://169.254.169.254/latest/'
    req = urllib.request.Request(base + 'api/token', method='PUT', headers={'X-aws-ec2-metadata-token-ttl-seconds': '60'})
    token = urllib.request.urlopen(req, timeout=3).read().decode()
    req = urllib.request.Request(base + path, headers={'X-aws-ec2-metadata-token': token})
    return urllib.request.urlopen(req, timeout=3).read().decode()


class Barrier(http.server.BaseHTTPRequestHandler):
    condition = threading.Condition()
    arrivals = {}

    def do_GET(self):
        key, node = self.path.strip('/').rsplit('/', 1)
        with self.condition:
            peers = self.arrivals.setdefault(key, set())
            peers.add(node)
            self.condition.notify_all()
            ok = self.condition.wait_for(lambda: len(peers) == NODES, timeout=180)
        self.send_response(200 if ok else 500)
        self.end_headers()
        self.wfile.write(b'ready' if ok else b'timeout')

    def log_message(self, *args):
        pass


def barrier(key):
    with urllib.request.urlopen(f"http://{PEERS[0]['ip']}:43400/{key}/{NODE}", timeout=190) as response:
        assert response.read() == b'ready'


def diagnostics(name):
    commands = {'driver': ['ethtool', '-i', DEVICE], 'timestamping': ['ethtool', '-T', DEVICE],
                'offloads': ['ethtool', '-k', DEVICE], 'counters': ['ethtool', '-S', DEVICE],
                'coalescing': ['ethtool', '-c', DEVICE], 'link': ['ip', '-j', 'link', 'show', DEVICE],
                'clocksource': ['cat', '/sys/devices/system/clocksource/clocksource0/current_clocksource'],
                'clock_names': ['sh', '-c', 'cat /sys/class/ptp/*/clock_name'],
                'cpu': ['cat', '/proc/stat'], 'interrupts': ['cat', '/proc/interrupts'],
                'reserved_ports': ['sysctl', 'net.ipv4.ip_local_reserved_ports'],
                'ntp': ['timedatectl', 'show-timesync', '--all']}
    record = {'utc': time.time(), 'node': NODE}
    for key, args in commands.items():
        p = subprocess.run(args, capture_output=True, text=True)
        record[key] = {'code': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}
    (OUT / f'{name}.json').write_text(json.dumps(record, indent=2) + '\n')


identity = json.loads(metadata('dynamic/instance-identity/document'))
me = {'node': NODE, 'ip': identity['privateIp'], 'az': identity['availabilityZone'],
      'az_id': metadata('meta-data/placement/availability-zone-id'), 'identity': identity}
(OUT / 'identity.json').write_text(json.dumps(me, indent=2) + '\n')
DEVICE = json.loads(run(['ip', '-j', 'route', 'show', 'default']))[0]['dev']
os.environ['ONEWAY_DEVICE'] = DEVICE
run(['ip', 'link', 'set', 'dev', DEVICE, 'mtu', '1500'])
cpus = sorted(os.sched_getaffinity(0))
assert len(cpus) >= 2
if NODE == 0:
    http.server.ThreadingHTTPServer.request_queue_size = 64
    httpd = http.server.ThreadingHTTPServer(('0.0.0.0', 43400), Barrier)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
if SAMPLING:
    sys.path.insert(0, str(HERE.parent / 'variance'))
    from port_plan import make_plan
    PLAN = make_plan(NODES)
    assert (FLOWS, ROUNDS, COUNT, PACE_US) == (64, PLAN['rounds'], PLAN['count'], PLAN['pace_us'])
    (OUT / 'port-plan.json').write_text(json.dumps(PLAN, indent=2) + '\n')
    pair_plans = {(p['node_a'], p['node_b']): p['candidates'] for p in PLAN['pairs']}
    # Explicit binds remain legal; automatic NTP/HTTP allocation cannot steal a probe port.
    original = run(['sysctl', '-n', 'net.ipv4.ip_local_reserved_ports']).strip()
    reserved = set()
    for item in filter(None, original.split(',')):
        ends = list(map(int, item.split('-')))
        reserved.update(range(ends[0], ends[-1] + 1))
    reserved.update(range(49152, 65536))
    reserved.add(48100)
    ranges = []
    for port in sorted(reserved):
        if ranges and port == ranges[-1][1] + 1:
            ranges[-1][1] = port
        else:
            ranges.append([port, port])
    value = ','.join(str(a) if a == b else f'{a}-{b}' for a, b in ranges)
    run(['sysctl', '-w', f'net.ipv4.ip_local_reserved_ports={value}'])
    assert run(['sysctl', '-n', 'net.ipv4.ip_local_reserved_ports']).strip() == value
    (OUT / 'port-reservation.json').write_text(json.dumps(dict(original=original, applied=value)) + '\n')
diagnostics('initial')
calibration = (OUT / 'calibration.jsonl').open('w')
cal = subprocess.Popen(['taskset', '-c', str(cpus[-1]), 'python3', str(HERE / 'calibrate.py'),
                        '--seconds', '2400' if SAMPLING else '1500', '--hz', '20'], stdout=calibration,
                       stderr=(OUT / 'calibration-errors.txt').open('w'))
try:
    time.sleep(2)
    assert cal.poll() is None
    data = [json.loads(s) for s in (OUT / 'calibration.jsonl').read_text().splitlines()]
    assert any(r['kind'] == 'ntp' for r in data), 'no independent clock reference'
    me['phc'] = any(r['kind'] == 'phc' for r in data)
    ctx.ready(me)
    ready = ctx.wait_ready(timeout=600)
    PEERS = sorted(ready.values(), key=lambda p: p['node'])
    assert [p['node'] for p in PEERS] == list(range(NODES))
    (OUT / 'peers.json').write_text(json.dumps(PEERS, indent=2) + '\n')
    barrier('calibration-start')
    time.sleep(10)
    matchings = []
    if LAYOUT=='bipartite':
        half=NODES//2
        matchings=[[(n,half+(n+offset)%half) for n in range(half)] for offset in range(half)]
    else:
        assert LAYOUT=='full'
        roster = list(range(NODES))
        for _ in range(NODES - 1):
            matchings.append(list(zip(roster[:NODES // 2], reversed(roster[NODES // 2:]))))
            roster = [roster[0], roster[-1]] + roster[1:-1]
    cases = []
    execution = []
    per_round=len(matchings)*FLOWS
    for repeat in range(ROUNDS):
        ctx.progress('measuring', completed=repeat*per_round, total=ROUNDS*per_round, case=f'pass-{repeat}')
        order = [(matching,flow) for matching in range(len(matchings)) for flow in range(FLOWS)]
        if SAMPLING:
            assert [[sorted(p) for p in m] for m in matchings] == PLAN['matchings']
            order = [(s['matching'], s['flow']) for s in PLAN['schedule'] if s['round'] == repeat]
        else:
            random.Random(240926 + repeat).shuffle(order)
        for matching,flow in order:
            pair = sorted(next(pair for pair in matchings[matching] if NODE in pair))
            src, dst = pair if repeat % 2 == 0 else pair[::-1]
            role = 'client' if NODE == src else 'server'
            wire_pass=repeat*FLOWS+flow
            key = f'r{repeat}-f{flow}-m{matching}-n{src}-n{dst}'
            path = OUT / (key + '.csv')
            env=os.environ.copy()
            candidate = pair_plans[tuple(pair)][flow] if SAMPLING else None
            if FLOWS>1:
                # The endpoint ports stay attached to the hosts when roles reverse.
                ports=({pair[0]:candidate['port_a'],pair[1]:candidate['port_b']} if SAMPLING else
                       {pair[0]:48000+flow,pair[1]:48100+(flow if PORT_MODE=='both' else 0)})
                env.update(ONEWAY_LOCAL_PORT=str(ports[NODE]),ONEWAY_PEER_PORT=str(ports[dst if role=='client' else src]))
            command = ['taskset', '-c', str(cpus[0]), str(OUT / 'probe'), PEERS[dst if role=='client' else src]['ip'],
                       str(src), str(dst), str(wire_pass), str(COUNT), str(PACE_US), str(path), role]
            barrier(f'r{repeat}-f{flow}-m{matching}-start')
            if SAMPLING:
                execution.append(dict(round=repeat, matching=matching, flow=flow,
                    node_a=pair[0], node_b=pair[1], canonical_flow=candidate['canonical_flow'], utc=time.time()))
                (OUT / 'execution.json').write_text(json.dumps(execution) + '\n')
                if candidate['canonical_flow'] != flow:
                    barrier(f'r{repeat}-f{flow}-m{matching}-ready')
                    continue
            if role == 'server':
                proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,env=env)
                time.sleep(.05)
                assert proc.poll() is None, 'server startup failed'
            # All servers are listening before clients are released.
            barrier(f'r{repeat}-f{flow}-m{matching}-ready')
            if role == 'client':
                proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,env=env)
            stdout, stderr = proc.communicate(timeout=120)
            (OUT / (key + '-stderr.txt')).write_text(stderr)
            if proc.returncode:
                raise RuntimeError(f'{key}: {stderr}')
            cases.append(dict(key=key, src=src, dst=dst, role=role, repeat=wire_pass,round=repeat,flow=flow,
                              file=path.name + '.gz', **json.loads(stdout)))
            with gzip.open(str(path) + '.gz', 'wb') as target:
                target.write(path.read_bytes())
            path.unlink()
            (OUT / 'cases.json').write_text(json.dumps(cases, indent=2) + '\n')
            assert cal.poll() is None
    barrier('measurements-finished')
    time.sleep(10)
    diagnostics('final')
    ctx.progress('complete', completed=ROUNDS*per_round, total=ROUNDS*per_round)
    barrier('diagnostics-finished')
finally:
    cal.terminate()
    cal.wait(timeout=5)
    calibration.close()
