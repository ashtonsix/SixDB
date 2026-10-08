#!/usr/bin/env python3
"""Controller lifecycle/inference checks; generated helpers are test fixtures only."""
from __future__ import annotations
from copy import deepcopy
import argparse
import csv
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import startup

DRIVER = r'''#!/usr/bin/env python3
import json,os,selectors,sys,time
def event(kind,**fields): print(json.dumps({'event':kind}|fields),flush=True)
revision=0; loads=0; buffer=b''; pending_pressure=None
event('ready',revision=revision)
selector=selectors.DefaultSelector(); selector.register(sys.stdin,selectors.EVENT_READ)
while True:
 loads+=64
 event('progress',revision=revision,loads=loads,checksum=loads^357712151618)
 for key,_ in selector.select(.01):
  chunk=os.read(sys.stdin.fileno(),4096)
  if not chunk: raise SystemExit(2)
  buffer+=chunk
  while b'\n' in buffer:
   text,buffer=buffer.split(b'\n',1); line=text.decode().split()
   if line[0]=='stop':
    if pending_pressure: event('pressure',revision=revision,value=pending_pressure)
    event('stopped',revision=revision,loads=loads,checksum=loads^357712151618)
    raise SystemExit(0)
   if line[0]=='profile':
    if sys.argv[0].endswith('reject'):
     event('rejected',reason='profile'); continue
    revision=int(line[1]); event('applied',revision=revision,line=int(line[2]),page=int(line[3]),width=int(line[4]),training=int(line[5]))
   elif line[0]=='pressure':
    if sys.argv[0].endswith('late') and line[1]=='low': pending_pressure='low'
    else: event('pressure',revision=revision,value=line[1])
   else: event('rejected',reason='syntax')
'''

PROBE = r'''#!/usr/bin/env python3
import csv,json,os,signal,subprocess,sys,time
from pathlib import Path
mode=Path(__file__).stem.split('-')[-1]
args=sys.argv
def option(name): return int(args[args.index(name)+1])
Path(__file__).with_suffix('.started').open('a').write(str(os.getpid())+'\n')
if mode=='fail': print('controlled probe failure',file=sys.stderr); raise SystemExit(7)
if mode=='hang':
 signal.signal(signal.SIGTERM,signal.SIG_IGN)
 child=subprocess.Popen([sys.executable,'-c','import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)'])
 Path(__file__).with_suffix('.child').write_text(str(child.pid))
 time.sleep(60)
time.sleep(.1)
writer=csv.writer(sys.stdout)
writer.writerow(['family','variant','bytes','k','stride','train','ahead','rep','n','p10','median','p90'])
sizes=[16384,524288,8388608,option('--mib')*2**20]
ks=[1,2,4,8,12,16,24,32,48,64]
for size in sizes:
 for k in ks:
  for rep in range(2):
   if mode=='incomplete' and size==sizes[-1] and k==64 and rep==1: continue
   value=100 if k==1 else 10 if k==4 else 30
   if mode=='unstable' and option('--seed')%2: value*=2
   if mode=='censored': value=100/k
   if mode=='nan' and size==sizes[-1] and k==4: value=float('nan')
   row=['mlp','base',size,k,0,0,0,rep,3,value*.95,value,value*1.05]
   writer.writerow(row)
   if mode=='duplicate' and size==sizes[-1] and k==4 and rep==1: writer.writerow(row)
if mode=='training':
 for train in [1,2,3,4,6,8,12,16]:
  if (train+8)*option('--line')>=os.sysconf('SC_PAGE_SIZE'): continue
  for rep in range(2):
   for arm,value in [('hot',10),('cold',100),('trained',20 if train>=4 else 95)]:
    writer.writerow(['learning','within-page:'+arm,512*max(65536,4*os.sysconf('SC_PAGE_SIZE')),0,1,train,1,rep,20,value*.95,value,value*1.05])
else: print('prefetch=unsupported-cache-maintenance',file=sys.stderr)
'''

def helper(path, text):
    path.write_text(text)
    path.chmod(0o755)
    return path

def wait(predicate, seconds=5):
    deadline = time.monotonic() + seconds
    while not predicate():
        assert time.monotonic() < deadline, 'check liveness guard expired'
        time.sleep(.01)

def dead(pid):
    # A killed orphan may remain a zombie briefly under the container's init;
    # it is no longer executing or holding the calibration's address space.
    status = Path(f'/proc/{pid}/stat')
    return not status.exists() or status.read_text().split()[2] == 'Z'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=('all', 'known-hit', 'failed-calibration'), default='all',
                        help='Controlled helpers exist only inside this check process')
    options = parser.parse_args()
    assert hasattr(os, 'sched_getaffinity'), 'Run on Linux; use orb -m ubuntu from macOS'
    cpu = min(os.sched_getaffinity(0))
    hardware = startup.discover(cpu)
    line, page = hardware['line_bytes'] or 64, hardware['pages']['base_bytes']
    checked = []
    def report():
        print(json.dumps({'status': 'passed', 'checks': checked, 'helpers': 'controlled lifecycle/inference fixtures only', 'performance_claim': False}))
    with tempfile.TemporaryDirectory(prefix='loom-startup-check-') as temporary:
        root = Path(temporary)
        driver = helper(root / 'driver', DRIVER)
        rejecting = helper(root / 'driver-reject', DRIVER)
        late = helper(root / 'driver-late', DRIVER)
        probes = {mode: helper(root / ('probe-' + mode), PROBE) for mode in
                  ('fail', 'hang', 'partial', 'training', 'incomplete', 'duplicate', 'nan', 'unstable', 'censored')}
        entry = {'status': 'validated', 'probe_format': 1, 'lookup_context': hardware['lookup_context'],
                 'mlp': {'status': 'validated', 'candidate_k': [4, 8], 'right_censored': False,
                         'scope': 'controlled check fixture, not measured evidence'},
                 'consistent_training_suffix': {'1': None}, 'limitations': ['controlled check fixture']}
        table = {'format': 1, 'entries': {hardware['lookup_key']: entry}}

        def invocation(name, mode, database=None, extra=(), signal_cancel=False, selected_driver=None, expected_exit=0):
            lookup = root / (name + '.lookup.json')
            lookup.write_text(json.dumps(database if database is not None else {'format': 1, 'entries': {}}))
            journal = root / (name + '.json')
            command = [sys.executable, str(Path(startup.__file__).resolve()), '--driver', str(selected_driver or driver),
                       '--probe', str(probes[mode]), '--lookup', str(lookup), '--journal', str(journal),
                       '--cpu', str(cpu), '--mib', '16', '--serve-seconds', '.15', '--calibration-seconds', '4', *extra]
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if signal_cancel:
                wait(lambda: probes[mode].with_suffix('.child').exists())
                process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=8)
            assert process.returncode == (130 if signal_cancel else expected_exit), (name, stdout, stderr, journal.read_text() if journal.exists() else '')
            result = json.loads(journal.read_text())
            assert result['children_reaped'] and result['last_progress']['loads'] > 0 and 'native_stopped' in result
            assert result['effective_geometry']['line'] == line and result['effective_geometry']['page'] == page
            assert result['defaults']['width'] == 8 and result['defaults']['training'] == 0
            assert not result['performance_claim']
            checked.append(name)
            return result

        if options.case != 'failed-calibration':
            hit = invocation('known-hit', 'fail', table)
            assert hit['lookup']['status'] == 'hit' and hit['calibration'] is None
            assert not probes['fail'].with_suffix('.started').exists()
            assert hit['profile_sent']['width'] == 4 and hit['profile_sent']['training'] == 0
            assert hit['profile_applied']['revision'] == hit['profile_sent']['revision'] == 1
            assert [e['kind'] for e in hit['events']].index('native_progress') < [e['kind'] for e in hit['events']].index('native_applied')
            if options.case == 'known-hit':
                report(); return
        failure = invocation('failed-calibration', 'fail')
        assert failure['calibration']['status'] == 'failed' and failure['profile_sent'] is None
        assert failure['profile_no_change'] and failure['native_progress_before_calibration_complete']
        if options.case == 'failed-calibration':
            report(); return

        for name, change in [('wrong-version', lambda t: t.update(format=2)),
                             ('wrong-context', lambda t: t['entries'][hardware['lookup_key']]['lookup_context'].update(kernel='changed')),
                             ('wrong-probe-format', lambda t: t['entries'][hardware['lookup_key']].update(probe_format=2))]:
            invalid = deepcopy(table); change(invalid)
            result = invocation(name, 'fail', invalid)
            assert result['lookup']['status'] == 'miss' and result['calibration']['status'] == 'failed'
            assert result['profile_sent'] is None and result['profile_no_change']
            assert result['native_progress_before_calibration_complete']

        unknown = deepcopy(table)
        unknown['entries'][hardware['lookup_key']]['mlp'] = {'status': 'unresolved', 'candidate_k': []}
        result = invocation('unknown-hit', 'fail', unknown)
        assert result['lookup']['status'] == 'hit' and result['profile_sent'] is None and result['profile_no_change']

        partial = invocation('partial-calibration', 'partial', table, ('--force-miss',))
        assert partial['lookup']['status'] == 'forced-miss' and partial['calibration']['status'] == 'complete'
        assert partial['native_progress_before_calibration_complete']
        observation = partial['calibration']['observations']
        assert observation['width'] == 4 and observation['training'] is None and not observation['global_lookup_eligible']
        assert 'foreground-overlap' in observation['mlp']['scope'] and '16 MiB' in observation['mlp']['scope']
        assert partial['profile_sent']['width'] == 4 and partial['profile_sent']['training'] == 0
        assert partial['profile_applied']['revision'] == 1
        assert partial['native_event_counts']['pressure'] == 2
        kinds = [e['kind'] for e in partial['events']]
        assert kinds.index('native_progress') < kinds.index('calibration_requested') < kinds.index('calibration_settled')
        for directory in root.glob('partial-calibration.calibration/seed-*'):
            assert {row['condition'] for row in csv.DictReader((directory / 'samples.csv').open())} == {'foreground-overlap'}

        trained = invocation('training-controls', 'training')
        eligible = [n for n in startup.TRAINS if n >= 4 and (n + 8) * line < page]
        expected_training = 4 if len(eligible) >= 2 else None
        assert trained['calibration']['observations']['training'] == expected_training
        assert trained['profile_sent']['training'] == (expected_training or 0)
        rejected = invocation('rejected-profile', 'partial', selected_driver=rejecting, expected_exit=1)
        assert rejected['status'] == 'failed' and rejected['profile_rejected']['reason'] == 'profile'
        assert rejected['profile_applied'] is None and rejected['last_progress']['revision'] == 0
        drained = invocation('shutdown-pressure-drain', 'fail', selected_driver=late)
        assert drained['native_event_counts']['pressure'] == 2
        assert [e['native']['value'] for e in drained['events'] if e['kind'] == 'native_pressure'] == ['busy', 'low']

        for mode in ('incomplete', 'duplicate', 'nan', 'unstable', 'censored'):
            result = invocation('refuse-' + mode, mode)
            assert result['profile_sent'] is None and result['profile_no_change']
            assert result['calibration']['status'] == ('complete' if mode in ('unstable', 'censored') else 'failed')
            if mode in ('unstable', 'censored'): assert result['calibration']['observations']['width'] is None

        timed = invocation('wall-budget', 'hang', extra=('--calibration-seconds', '.1'))
        assert timed['calibration']['status'] == 'timeout' and timed['profile_sent'] is None
        wait(lambda: all(dead(int(pid)) for pid in probes['hang'].with_suffix('.started').read_text().splitlines()))
        wait(lambda: dead(int(probes['hang'].with_suffix('.child').read_text())))
        probes['hang'].with_suffix('.child').unlink()
        cancelled = invocation('external-cancel', 'hang', signal_cancel=True)
        assert cancelled['status'] == 'cancelled' and cancelled['calibration']['status'] == 'cancelled'
        assert cancelled['profile_sent'] is None
        wait(lambda: dead(int(probes['hang'].with_suffix('.child').read_text())))

        registry = startup.Calibrations(probes['partial'], root / 'coalesced', 16, 4)
        first = registry.request(hardware, line, page)
        second = registry.request(deepcopy(hardware), line, page)
        assert first is second
        result = first.result(timeout=5)
        registry.close()
        assert result['status'] == 'complete' and len(result['commands']) == 2 and len(registry.jobs) == 1 and registry.requests == 2
        checked.append('coalesced-same-context')
    report()

if __name__ == '__main__':
    main()
