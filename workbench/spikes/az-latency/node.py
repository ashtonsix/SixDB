#!/usr/bin/env python3
"""Study-local six-node rendezvous and repeated private-address RTT sweep."""
import concurrent.futures
import gzip
import http.server
import json
import os
from pathlib import Path
import random
import re
import subprocess
import threading
import time
import urllib.request

OUT = Path(os.environ['SIXDB_RESULTS'])
PREFIX = os.environ['AZ_RUN_URI']
NODE = int(os.environ['AZ_NODE'])
BINARY = OUT / 'probe'
DEADLINE = time.monotonic() + 3000


def run(args, **kw):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kw).stdout


def put(value, key):
    file = OUT / ('mailbox-' + key.replace('/', '-'))
    file.write_text(json.dumps(value, indent=2) + '\n')
    run(['aws', 's3', 'cp', '--only-show-errors', str(file), PREFIX + '/' + key], timeout=30)


def get(key):
    try:
        return json.loads(run(['aws', 's3', 'cp', '--only-show-errors', PREFIX + '/' + key, '-'], timeout=30))
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return None


def wait_for(fn):
    while time.monotonic() < DEADLINE:
        value = fn()
        if value is not None:
            return value
        time.sleep(3)
    raise TimeoutError('study rendezvous exceeded deadline')


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
            ok = self.condition.wait_for(lambda: len(peers) == 6, timeout=600)
        self.send_response(200 if ok else 500)
        self.end_headers()
        self.wfile.write(b'ready' if ok else b'timeout')

    def log_message(self, *args):
        pass


def barrier(key):
    url = f"http://{PEERS[0]['ip']}:43000/{key}/{NODE}"
    with urllib.request.urlopen(url, timeout=610) as response:
        assert response.read() == b'ready'


def diagnostics(name):
    commands = {
        'link': ['ip', '-j', '-d', 'link', 'show', 'dev', DEVICE],
        'route': ['ip', '-j', 'route'],
        'driver': ['ethtool', '-i', DEVICE],
        'offloads': ['ethtool', '-k', DEVICE],
        'counters': ['ethtool', '-S', DEVICE],
        'tcp': ['cat', '/proc/net/snmp'],
        'cpu': ['cat', '/proc/stat'],
        'interrupts': ['cat', '/proc/interrupts'],
    }
    data = {'utc': time.time(), 'node': NODE}
    for key, command in commands.items():
        proc = subprocess.run(command, capture_output=True, text=True)
        data[key] = {'code': proc.returncode, 'stdout': proc.stdout, 'stderr': proc.stderr}
    (OUT / f'{name}.json').write_text(json.dumps(data, indent=2) + '\n')


identity = json.loads(metadata('dynamic/instance-identity/document'))
me = {'node': NODE, 'ip': identity['privateIp'], 'az': identity['availabilityZone'],
      'az_id': metadata('meta-data/placement/availability-zone-id'), 'identity': identity}
DEVICE = json.loads(run(['ip', '-j', 'route', 'show', 'default']))[0]['dev']
cpus = sorted(os.sched_getaffinity(0))
assert len(cpus) == 2, cpus
server = subprocess.Popen(['taskset', '-c', str(cpus[1]), str(BINARY), 'server'], stdout=subprocess.DEVNULL, stderr=(OUT / 'server-errors.txt').open('w'))
try:
    if NODE == 0:
        httpd = http.server.ThreadingHTTPServer(('0.0.0.0', 43000), Barrier)
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
    put(me, f'ready/{NODE}.json')
    if NODE == 0:
        ready = [wait_for(lambda i=i: get(f'ready/{i}.json')) for i in range(6)]
        assert len({p['az_id'] for p in ready}) == 6
        put(ready, 'peers.json')
    PEERS = wait_for(lambda: get('peers.json'))
    (OUT / 'peers.json').write_text(json.dumps(PEERS, indent=2) + '\n')
    diagnostics('initial')
    # Round-robin tournament: every host has exactly one partner per matching.
    roster = list(range(6))
    matchings = []
    for _ in range(5):
        matchings.append(list(zip(roster[:3], reversed(roster[3:]))))
        roster = [roster[0], roster[-1]] + roster[1:-1]
    rows = []
    for repeat in range(3):
        mtus = [1500, 9001] if repeat % 2 == 0 else [9001, 1500]
        for mtu in mtus:
            epoch = f'r{repeat}-mtu{mtu}'
            barrier(epoch + '-before-mtu')
            run(['ip', 'link', 'set', 'dev', DEVICE, 'mtu', str(mtu)])
            diagnostics(epoch + '-before')
            barrier(epoch + '-mtu-ready')
            if NODE == 0:
                put({'epoch': epoch, 'utc': time.time(), 'state': 'measuring'}, 'progress.json')
            barrier(epoch + '-begin')
            order = list(range(5)); random.Random(24681 + repeat).shuffle(order)
            for match in order:
                pair = next(pair for pair in matchings[match] if NODE in pair)
                dest = PEERS[pair[0] if pair[1] == NODE else pair[1]]
                sizes = [64, 512, 1400, 4096, 8192, 65536]
                random.Random(710 + repeat * 9 + match).shuffle(sizes)
                for size in sizes:
                    key = f'{epoch}-m{match}-tcp{size}'
                    barrier(key)
                    output = OUT / f'{key}.csv'
                    started = time.time()
                    detail = json.loads(run(['taskset', '-c', str(cpus[0]), str(BINARY), dest['ip'], str(size), '3000', str(output), '200'], timeout=180))
                    rows.append({'protocol': 'tcp', 'node': NODE, 'destination': dest['node'], 'repeat': repeat,
                                 'mtu': mtu, 'bytes': size, 'started': started, 'ended': time.time(), 'file': output.name + '.gz', **detail})
                    with gzip.open(str(output) + '.gz', 'wb') as target:
                        target.write(output.read_bytes())
                    output.unlink()
                # DF validates both exact IPv4 MTU boundaries and oversize failure.
                ping_sizes = [56, 1472, 1473, 8973, 8974]
                for size in ping_sizes:
                    key = f'{epoch}-m{match}-icmp{size}'
                    barrier(key)
                    expected_fit = size + 28 <= mtu
                    count = 1000 if expected_fit else 3
                    args = ['ping', '-n', '-D', '-M', 'do', '-i', '0.002', '-W', '1', '-c', str(count), '-s', str(size), dest['ip']]
                    started = time.time()
                    proc = subprocess.run(args, capture_output=True, text=True, timeout=45, env=os.environ | {'LC_ALL': 'C'})
                    output = OUT / f'{key}.txt.gz'
                    with gzip.open(output, 'wt') as target:
                        target.write(proc.stdout + proc.stderr)
                    rows.append({'protocol': 'icmp', 'node': NODE, 'destination': dest['node'], 'repeat': repeat,
                                 'mtu': mtu, 'bytes': size, 'started': started, 'ended': time.time(), 'file': output.name,
                                 'expected_fit': expected_fit, 'requested': count, 'returncode': proc.returncode})
            barrier(epoch + '-finished')
            diagnostics(epoch + '-after')
            (OUT / 'cases.json').write_text(json.dumps(rows, indent=2) + '\n')
    barrier('all-measurements-finished')
    if NODE == 0:
        put({'state': 'completed', 'utc': time.time()}, 'progress.json')
    barrier('all-uploads-finished')
    time.sleep(3)
finally:
    server.terminate()
    server.wait(timeout=5)
