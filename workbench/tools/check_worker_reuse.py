#!/usr/bin/env python3
"""Exercise reuse races and real cache/source handoff without cloud resources."""
import copy
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import time
import unittest
from unittest.mock import patch

from check_worker import job
import check_worker
import artifacts
import worker
import worker_pool as pool
import worker_runtime as runtime


def reusable_job():
    value = job()
    value['format'] = 2
    value['config'] |= {'fresh': False, 'idle_seconds': 300, 'max_age_seconds': 14400, 'actual_capacity': 'on-demand'}
    value['source'] |= {'pool_sha256': 'pool', 'setup_sha256': 'setup'}
    value['profile'] = pool.profile(value['config'], value['source'])
    value['worker'] = {'id': value['id'], 'subnet_id': 'subnet-a', 'reused': False}
    return value


class MemoryStore:
    def __init__(self):
        self.values = {}
        self.serial = 0
    def read(self, key):
        return copy.deepcopy(self.values.get(key, (None, None)))
    def replace(self, key, value, etag):
        if self.read(key)[1] != etag:
            return False
        self.serial += 1
        self.values[key] = (copy.deepcopy(value), str(self.serial))
        return True


def idle(value):
    return {'state': 'idle', 'profile': value['profile'], 'capacity': 'on-demand', 'worker_id': 'warm',
            'instance_id': 'i-warm', 'subnet_id': 'subnet-a', 'max_end': time.time() + 10000,
            'idle_until': time.time() + 300, 'job_id': 'previous', 'reuse_count': 0}


class ReuseTests(unittest.TestCase):
    def test_compatibility_and_budget(self):
        value = reusable_job()
        original = idle(value)
        self.assertTrue(pool.can_claim(original, value))
        for change in ({'capacity': 'spot'}, {'profile': 'other'}, {'subnet_id': 'subnet-other'},
                       {'idle_until': 0}, {'max_end': time.time() + 30}, {'state': 'assigned'}):
            self.assertFalse(pool.can_claim(original | change, value), change)
        changed = copy.deepcopy(value)
        changed['config']['env'] = {'CASE': 'new'}
        self.assertEqual(pool.profile(changed['config'], changed['source']), value['profile'])
        changed['source']['setup_sha256'] = 'different compiler setup'
        self.assertNotEqual(pool.profile(changed['config'], changed['source']), value['profile'])

    def test_atomic_claim_fresh_bypass_and_no_fallback_after_uncertain_write(self):
        value = reusable_job()
        store = MemoryStore()
        store.replace(pool.state_key('warm'), idle(value), None)
        class Aws:
            def call(self, *args, **kwargs):
                return {'Reservations': [{'Instances': [{'InstanceId': 'i-warm',
                    'Tags': [{'Key': 'SixDBWorkerSession', 'Value': 'warm'}]}]}]}
            def upload(self, *args): pass
        with tempfile.TemporaryDirectory() as temp, patch.object(pool, 'Store', return_value=store):
            directory = Path(temp)
            worker.save(directory / 'job.json', value)
            self.assertTrue(worker.reuse(value, directory, Aws()))
            self.assertFalse(worker.reuse(value, directory, Aws()))
            self.assertEqual(store.read(pool.state_key('warm'))[0]['job_id'], value['id'])
            with patch.object(Aws, 'call', side_effect=AssertionError('fresh must bypass discovery')):
                value['config']['fresh'] = True
                self.assertFalse(worker.reuse(value, directory, Aws()))
            value['config']['fresh'] = False
            state, etag = store.read(pool.state_key('warm'))
            store.replace(pool.state_key('warm'), idle(value), etag)
            with patch.object(store, 'replace', side_effect=RuntimeError('uncertain')):
                with self.assertRaisesRegex(RuntimeError, 'uncertain'):
                    worker.reuse(value, directory, Aws())

    def test_lost_put_response_is_resolved_or_stops_dispatch(self):
        store = pool.Store('bucket', 'region')
        observed = {}
        def command(*args):
            observed.update(json.loads(Path(args[args.index('--body') + 1]).read_text()))
            return subprocess.CompletedProcess(args, 1, '', 'connection lost')
        with patch.object(store, 'command', side_effect=command), \
                patch.object(store, 'read', side_effect=lambda key: (observed, 'new-etag')):
            self.assertTrue(store.replace('key', {'state': 'assigned'}, 'old-etag'))
        with patch.object(store, 'command', return_value=subprocess.CompletedProcess([], 1, '', 'connection lost')), \
                patch.object(store, 'read', return_value=({'state': 'busy'}, 'later-etag')):
            with self.assertRaisesRegex(RuntimeError, 'Uncertain'):
                store.replace('key', {'state': 'assigned'}, 'old-etag')
        with patch.object(store, 'command', return_value=subprocess.CompletedProcess([], 1, '', '(PreconditionFailed)')), \
                patch.object(store, 'read', return_value=({'state': 'assigned'}, 'competitor-etag')):
            self.assertFalse(store.replace('key', {'state': 'assigned'}, 'old-etag'))

    def test_cancel_old_job_cannot_kill_new_tenant_or_win_expiry_race(self):
        value = reusable_job()
        store = MemoryStore()
        key = pool.state_key('warm')
        store.replace(key, idle(value) | {'state': 'busy', 'job_id': 'new-tenant'}, None)
        instance = {'InstanceId': 'i-warm', 'State': {'Name': 'running'},
                    'Tags': [{'Key': 'SixDBWorkerSession', 'Value': 'warm'}]}
        class Aws:
            def __init__(self): self.terminated = []
            def call(self, *args, **kwargs): self.terminated += kwargs['InstanceIds']
        aws = Aws()
        with patch.object(pool, 'Store', return_value=store), patch.object(worker, 'instances', return_value=[instance]):
            worker.cancel(value, aws)
            self.assertEqual(aws.terminated, [])
            state, etag = store.read(key)
            store.replace(key, state | {'job_id': value['id']}, etag)
            with patch.object(store, 'replace', return_value=False):
                with self.assertRaisesRegex(RuntimeError, 'ownership changed'):
                    worker.cancel(value, aws)
            self.assertEqual(aws.terminated, [])
            worker.cancel(value, aws)
            self.assertEqual(aws.terminated, ['i-warm'])
            self.assertEqual(store.read(key)[0]['state'], 'retiring')

    def test_wait_returns_verified_result_without_cancel(self):
        value = reusable_job()
        class Aws: pass
        with tempfile.TemporaryDirectory() as temp, patch.object(worker, 'status', return_value={'state': 'complete'}), \
                patch.object(worker, 'instances', return_value=[]), patch.object(worker, 'fetch') as fetch, \
                patch.object(worker, 'cancel', side_effect=AssertionError('completed session job must not cancel')):
            self.assertEqual(worker.wait(value, Path(temp), Aws()), 0)
            fetch.assert_called_once()

    def test_two_real_jobs_share_cache_and_build_but_replace_source_and_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            fixture = check_worker.RuntimeTests()
            first, events, publish = fixture.fixture(directory, '''mkdir -p build "$SIXDB_DATA_CACHE"
echo cached > "$SIXDB_DATA_CACHE/input"
echo object > build/object
echo stale > stale-file
echo first > "$SIXDB_RESULTS/first-only"
''')
            first.job = reusable_job() | {'source': first.job['source'] | {'setup_sha256': 'setup'}}
            first.job['worker']['reused'] = False
            first = runtime.Worker(first.job, first.base)
            store = directory / 'store'
            def aws(*args, **kwargs):
                if args[1] == 'cp' and args[-2].startswith('s3:'):
                    import shutil
                    shutil.copyfile(store / args[-2].rsplit('/', 1)[1], args[-1])
                return subprocess.CompletedProcess(args, 0, '', '')
            first.aws = aws
            with patch.object(artifacts, 'publish', return_value={'sha256': 'verified'}), patch.object(runtime, 'metadata', return_value={}):
                self.assertEqual(first.run(), 0)
                # The next captured script checks persistent cache and clean source/results.
                script = '''test "$(cat "$SIXDB_DATA_CACHE/input")" = cached
test "$(cat build/object)" = object
test ! -e stale-file
test ! -e "$SIXDB_RESULTS/first-only"
test "$SIXDB_WORKER_REUSED" = 1
printf '%s' "$SIXDB_WORKER_ID" > "$SIXDB_RESULTS/session"
'''
                with tarfile.open(store / 'source.tar.gz', 'w:gz') as archive:
                    data = script.encode(); info = tarfile.TarInfo('probe.sh'); info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
                second_job = copy.deepcopy(first.job)
                second_job['id'] = 'second-job'
                second_job['source']['sha256'] = runtime.digest(store / 'source.tar.gz')
                second_job['worker'] |= {'reused': True, 'reuse_count': 1}
                second = runtime.Worker(second_job, first.base); second.aws = aws
                self.assertEqual(second.run(), 0)
            self.assertNotEqual(first.results, second.results)
            self.assertTrue((first.results / 'first-only').exists())
            self.assertEqual((second.results / 'session').read_text(), first.job['worker']['id'])
            self.assertTrue(json.loads((second.results / 'host.json').read_text())['setup_reused'])
            tracked = subprocess.check_output(['git', 'ls-files'], cwd=second.source, text=True)
            self.assertNotIn('build/', tracked)

    def test_refresh_preserves_unchanged_source_mtime_and_drops_removed_files(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp); source = base / 'source'; source.mkdir()
            (source / 'same.cpp').write_text('same'); (source / 'removed.cpp').write_text('old')
            stamp = (source / 'same.cpp').stat().st_mtime_ns
            bundle = base / 'source.tar'
            with tarfile.open(bundle, 'w') as archive:
                info = tarfile.TarInfo('same.cpp'); info.size = 4
                archive.addfile(info, io.BytesIO(b'same'))
            runtime.refresh_source(bundle, source)
            self.assertEqual((source / 'same.cpp').stat().st_mtime_ns, stamp)
            self.assertFalse((source / 'removed.cpp').exists())

    def test_idle_expiry_and_assignment_race(self):
        value = reusable_job(); store = MemoryStore()
        with tempfile.TemporaryDirectory() as temp, patch.object(pool, 'Store', return_value=store), \
                patch.object(runtime, 'metadata', return_value={'instanceId': 'i-warm'}):
            session = runtime.Session(value, Path(temp))
            store.replace(session.key, idle(value) | {'idle_until': 0}, None)
            self.assertIsNone(session.next_job())
            self.assertEqual(store.read(session.key)[0]['state'], 'retiring')
            # Expiry observes old idle state, but a submitting controller wins CAS first.
            next_job = copy.deepcopy(value); next_job['id'] = 'next-job'
            next_job['prefix'] = 'sixdb/workers/next-job'; next_job['uri'] = 's3://test-bucket/' + next_job['prefix']
            payload = (json.dumps(next_job, indent=2, sort_keys=True) + '\n').encode()
            state, etag = store.read(session.key)
            store.replace(session.key, idle(value) | {'idle_until': 0}, etag)
            store.replace('sixdb/workers/next-job/job.json', next_job, None)
            replace = store.replace; first = True
            def race(key, state, etag):
                nonlocal first
                if first:
                    first = False
                    prior, current = store.read(key)
                    replace(key, prior | {'state': 'assigned', 'job_id': next_job['id'], 'job_uri': next_job['uri'],
                        'job_sha256': hashlib.sha256(payload).hexdigest()}, current)
                    return False
                return replace(key, state, etag)
            with patch.object(store, 'replace', side_effect=race):
                self.assertEqual(session.next_job()['id'], next_job['id'])
            self.assertEqual(store.read(session.key)[0]['state'], 'busy')

    def test_each_job_has_its_own_timer_and_boot_timer_is_stopped_once(self):
        value = reusable_job(); store = MemoryStore()
        timers = {'sixdb-boot-deadline.timer'}
        def command(argv, **kwargs):
            if argv[0] == 'systemd-run':
                unit = next(arg.split('=', 1)[1] for arg in argv if arg.startswith('--unit='))
                timers.add(unit + '.timer')
            elif argv[0] == 'systemctl':
                if argv[-1] not in timers:
                    raise subprocess.CalledProcessError(5, argv)  # Systemd unloads inactive transient units.
                timers.remove(argv[-1])
            else:
                job_path = Path(argv[-2])
                (job_path.parent / 'status.json').write_text(json.dumps(
                    {'state': 'complete', 'script_returncode': 0, 'artifact': {'sha256': 'verified'}}))
            return subprocess.CompletedProcess(argv, 0)
        with tempfile.TemporaryDirectory() as temp, patch.object(pool, 'Store', return_value=store), \
                patch.object(runtime, 'metadata', return_value={'instanceId': 'i-warm'}):
            session = runtime.Session(value, Path(temp))
            with patch.object(runtime.subprocess, 'run', side_effect=command):
                self.assertTrue(session.run_job(value, session.state))
                following = copy.deepcopy(value); following['id'] = 'following'
                self.assertTrue(session.run_job(following, session.state | {'reuse_count': 1}))
            self.assertEqual(timers, set())

    def test_supervisor_failure_publishes_error_without_overwriting_completed_result(self):
        for existing in (None, {'state': 'complete', 'artifact': {'sha256': 'verified'}}):
            value = reusable_job(); store = MemoryStore()
            with tempfile.TemporaryDirectory() as temp, patch.object(pool, 'Store', return_value=store), \
                    patch.object(runtime, 'metadata', return_value={'instanceId': 'i-warm'}):
                session = runtime.Session(value, Path(temp))
                key = value['prefix'] + '/status.json'
                if existing:
                    store.replace(key, existing, None)
                with patch.object(session, 'run_job', side_effect=RuntimeError('timer unavailable')):
                    with self.assertRaisesRegex(RuntimeError, 'timer unavailable'):
                        session.run()
                status = store.read(key)[0]
                if existing:
                    self.assertEqual(status, existing)
                else:
                    self.assertEqual(status['state'], 'failed')
                    self.assertIn('timer unavailable', status['error'])

    def test_rejected_assignment_reports_failure_for_that_job(self):
        value = reusable_job(); store = MemoryStore()
        following = copy.deepcopy(value); following['id'] = 'rejected'
        following['prefix'] = 'sixdb/workers/rejected'; following['uri'] = 's3://test-bucket/' + following['prefix']
        with tempfile.TemporaryDirectory() as temp, patch.object(pool, 'Store', return_value=store), \
                patch.object(runtime, 'metadata', return_value={'instanceId': 'i-warm'}):
            session = runtime.Session(value, Path(temp))
            store.replace(session.key, idle(value) | {'state': 'assigned', 'job_id': 'rejected',
                'job_uri': following['uri'], 'job_sha256': 'wrong'}, None)
            store.replace(following['prefix'] + '/job.json', following, None)
            previous = {'state': 'complete', 'artifact': {'sha256': 'verified'}}
            store.replace(value['prefix'] + '/status.json', previous, None)
            with patch.object(session, 'loop', side_effect=session.next_job):
                with self.assertRaisesRegex(ValueError, 'identity/configuration changed'):
                    session.run()
            self.assertEqual(store.read(value['prefix'] + '/status.json')[0], previous)
            self.assertEqual(store.read(following['prefix'] + '/status.json')[0]['state'], 'failed')
            self.assertEqual(store.read(session.key)[0]['state'], 'retiring')

    def test_session_retires_on_upload_failure_and_respects_zero_idle(self):
        for reusable, idle_seconds in [(False, 300), (True, 0)]:
            value = reusable_job(); value['config']['idle_seconds'] = idle_seconds
            store = MemoryStore()
            with tempfile.TemporaryDirectory() as temp, patch.object(pool, 'Store', return_value=store), \
                    patch.object(runtime, 'metadata', return_value={'instanceId': 'i-warm'}):
                session = runtime.Session(value, Path(temp))
                with patch.object(session, 'run_job', return_value=reusable), \
                        patch.object(session, 'next_job', side_effect=AssertionError('must retire')):
                    self.assertEqual(session.run(), 0)
                self.assertEqual(store.read(session.key)[0]['state'], 'retiring')


if __name__ == '__main__':
    unittest.main()
