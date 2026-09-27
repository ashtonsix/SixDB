#!/usr/bin/env python3
"""Opt-in real TLC kill/restore checks: set TLC_TEST_JAR to the documented pinned jar."""
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

import worker_checkpoint as checkpoint
from check_worker_checkpoint import MemoryStore, checkpoint_job

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS / 'tlc'))
import tlc

MODEL = r'''---- MODULE Grid ----
EXTENDS Naturals
CONSTANT N
VARIABLES x, y
Init == x = 0 /\ y = 0
Step == \/ /\ x < N /\ x' = x + 1 /\ UNCHANGED y
        \/ /\ y < N /\ y' = y + 1 /\ UNCHANGED x
Next == Step \/ /\ x = N /\ y = N /\ UNCHANGED <<x,y>>
Spec == Init /\ [][Next]_<<x,y>> /\ WF_<<x,y>>(Step)
TypeOK == x \in 0..N /\ y \in 0..N
NeverFinish == ~(x = N /\ y = N)
Finish == <> (x = N /\ y = N)
ForeverMoving == []<>(x < N)
====
'''


@unittest.skipUnless(os.environ.get('TLC_TEST_JAR'), 'set TLC_TEST_JAR; no automatic downloads or cloud runs')
class TLCRecovery(unittest.TestCase):
    def test_killed_and_restored_matches_uninterrupted_invariants_and_temporal_outcomes(self):
        jar = Path(os.environ['TLC_TEST_JAR']).resolve()
        self.assertEqual(tlc.digest(jar), tlc.JAR_SHA256)
        # A million-state finite grid runs past TLC's first checkpoint opportunity.
        for name, declarations, expected in (
            ('fair', 'INVARIANT TypeOK\nPROPERTY Finish\n', 'Model checking completed. No error has been found.'),
            ('invariant-failure', 'INVARIANT NeverFinish\nPROPERTY Finish\n', 'Invariant NeverFinish is violated'),
            ('temporal-failure', 'INVARIANT TypeOK\nPROPERTY ForeverMoving\n', 'Temporal property ForeverMoving was violated.')):
            if os.environ.get('TLC_TEST_CASE') and os.environ['TLC_TEST_CASE'] != name:
                continue
            with self.subTest(case=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / 'Grid.tla').write_text(MODEL)
                (root / 'Grid.cfg').write_text('CONSTANT N = 1000\nSPECIFICATION Spec\n' + declarations)
                args = [sys.executable, str(TOOLS / 'tlc/tlc.py'), 'run', '-workers', '1',
                        '-lncheck', 'final', '-config', 'Grid.cfg', 'Grid.tla']
                common = os.environ | {'TLC_JAR': str(jar), 'TLC_JAVA_OPTS': '-Xmx256m -XX:+UseParallelGC',
                                      'SIXDB_DATA_CACHE': str(root / 'cache')}
                def environment(attempt, resume=None):
                    env = common | {'SIXDB_CHECKPOINT_STATE': str(root / attempt / 'state'),
                                    'SIXDB_RESULTS': str(root / attempt / 'results')}
                    env.pop('SIXDB_RESUME', None)
                    if resume:
                        env['SIXDB_RESUME'] = str(resume)
                    return env
                baseline = subprocess.run(args, cwd=root, env=environment('baseline'),
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=90)
                baseline_log = (root / 'baseline/results/tlc.log').read_text()
                self.assertTrue(expected in baseline_log, baseline_log[-3000:] + baseline.stderr)
                interrupted = environment('interrupted')
                child = subprocess.Popen(args, cwd=root, env=interrupted, stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL, start_new_session=True)
                try:
                    end = time.monotonic() + 15
                    log = root / 'interrupted/results/tlc.log'
                    while not log.exists() or b'Finished computing initial states' not in log.read_bytes():
                        self.assertIsNone(child.poll(), 'TLC ended before checkpoint request')
                        self.assertLess(time.monotonic(), end, 'TLC startup timed out')
                        time.sleep(0.02)
                    snapshot = root / 'snapshot'; snapshot.mkdir()
                    subprocess.run([sys.executable, str(TOOLS / 'tlc/tlc.py'), 'checkpoint', str(snapshot)],
                        cwd=root, env=interrupted, check=True, timeout=45, stdout=subprocess.DEVNULL)
                    self.assertIn('Checkpointing completed', (snapshot / 'tlc.log').read_text())
                finally:
                    os.killpg(child.pid, signal.SIGCONT)
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                store = MemoryStore(); value = checkpoint_job()
                value['source']['digest'] = checkpoint.sha((root / 'Grid.tla').read_bytes() + (root / 'Grid.cfg').read_bytes())
                ref = checkpoint.publish(store, snapshot, value, 'spot-interruption')
                restored = root / 'restored'
                checkpoint.restore(store, ref, restored, value)
                # Remove the original machine's files; recovery uses only stored bytes.
                shutil.rmtree(root / 'interrupted'); shutil.rmtree(snapshot)
                resumed = subprocess.run(args, cwd=root, env=environment('resumed', restored),
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=90)
                result = (root / 'resumed/results/tlc.log').read_text()
                self.assertEqual(resumed.returncode, baseline.returncode, result[-3000:] + resumed.stderr)
                self.assertTrue(expected in result, result[-3000:])
                self.assertIn('Recovery completed.', result)
                found = re.search(r'Recovery completed\. (\d+) states examined', result)
                self.assertIsNotNone(found)
                self.assertTrue(0 < int(found[1]) < 1002001)
                if name != 'invariant-failure':
                    self.assertIn('1002001 distinct states found', result)
                    self.assertIn('1002001 distinct states found', baseline_log)
                print(f'{name}: outcome={resumed.returncode}, recovered={found[1]} states, checkpoint={ref["bytes"]:,} bytes', flush=True)


if __name__ == '__main__':
    unittest.main()
