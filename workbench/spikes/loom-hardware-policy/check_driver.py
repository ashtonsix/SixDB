#!/usr/bin/env python3
"""Actual native owner-boundary changes, malformed updates and settlement checks."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time


def exercise(driver, mode):
    process = subprocess.Popen([str(driver), 'serve', '--line', '128', '--page', '4096',
                                '--width', '4', '--nodes', '1024'],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, bufsize=1)
    events = []
    incoming = queue.Queue()

    def read():
        for line in process.stdout:
            incoming.put(json.loads(line))
        incoming.put(None)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()

    def receive(name, revision=None):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            event = incoming.get(timeout=max(.01, deadline - time.monotonic()))
            if event is None:
                raise RuntimeError('native service ended before ' + name)
            events.append(event)
            if event['event'] == name and (revision is None or event.get('revision') == revision):
                return event
        raise TimeoutError(name)

    def send(text):
        process.stdin.write(text)
        process.stdin.flush()

    try:
        ready = receive('ready')
        initial = receive('progress')
        assert ready['revision'] == 0 and initial['loads'] > 0
        if mode == 'updates':
            send('pressure busy\nprofile 1 128 4096 8 3\n')
            busy = receive('pressure')
            applied = receive('applied', 1)
            assert applied['loads'] >= initial['loads'] and applied['width'] == 8
            assert busy['procedures'][8:] == ['none'] * 8
            assert applied['procedures'][8:] == ['none'] * 8
            send('profile 1 128 4096 8 3\nprofile 2 64 4096 8 3\n')
            assert receive('rejected')['reason'] == 'profile'
            assert receive('rejected')['reason'] == 'profile'
            send('pressure low\nprofile ')
            # Deliberately fragment a line while the native owner keeps serving.
            time.sleep(.02)
            send('2 128 4096 4 0\n')
            receive('pressure')
            restored = receive('applied', 2)
            assert restored['width'] == 4 and restored['training'] == 0
            assert restored['procedures'][8:] == ['distant-one'] * 8
            send('stop\n')
        elif mode == 'saturated-stop':
            send('pressure busy\n' * 100 + 'stop\n')
        elif mode == 'eof':
            process.stdin.close()
        else:
            raise ValueError(mode)
        stopped = receive('stopped')
        assert stopped['loads'] >= initial['loads']
        process.wait(timeout=10)
        assert process.returncode == 0, process.stderr.read()
        return {'case': mode, 'status': 'passed', 'events': events, 'returncode': process.returncode}
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        reader.join(timeout=1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--driver', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = {'cases': [exercise(args.driver.resolve(), mode) for mode in
                        ['updates', 'saturated-stop', 'eof']],
              'runtime': {key: os.environ.get(key) for key in
                          ['ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS']}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: actual native updates, pressure, stale/invalid profiles, saturated stop and EOF')


if __name__ == '__main__':
    main()
