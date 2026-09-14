#!/usr/bin/env python3
"""Recover a campaign and remove its SG; observer errors do not cancel workers."""
import argparse
import concurrent.futures
import json
from pathlib import Path
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import worker


def observe(job, directory, aws):
    while True:
        try:
            # A normal return follows worker.py's terminal/deadline path and
            # collection. Exceptions are failed observations, not job failures.
            return worker.wait(job, directory, aws)
        except Exception as error:
            print(f"{job['id']}: observer error; retrying existing job: {error}", flush=True)
            if time.time() > job['created_at'] + job['config']['deadline_seconds'] + 300:
                raise RuntimeError('observation failed beyond the worker deadline') from error
            time.sleep(10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path)
    parser.add_argument('--abort', action='store_true', help='cancel this recorded campaign, including a partial launch')
    args = parser.parse_args()
    campaign = args.campaign
    state = json.loads(campaign.read_text())
    aws = worker.Aws('us-east-1')
    lock = threading.Lock()
    state.setdefault('collection', {})
    state.setdefault('cleanup_errors', [])
    def save():
        temp = campaign.with_suffix('.next.json')
        temp.write_text(json.dumps(state, indent=2) + '\n')
        temp.replace(campaign)
    def wait(job_id):
        directory = ROOT / 'build/workers' / job_id
        job = json.loads((directory / 'job.json').read_text())
        try:
            result = {'exit_code': observe(job, directory, worker.Aws('us-east-1')), 'utc': time.time()}
        except Exception as error:
            result = {'error': str(error), 'utc': time.time()}
        with lock:
            state['collection'][job_id] = result
            save()
        print(f'{job_id}: {result}', flush=True)
        return result
    if args.abort:
        state['intentional_abort'] = True
        save()
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            list(pool.map(wait, state['jobs']))
    # Observation exceptions are retried until the deadline. At this point each
    # job ended, exceeded its own deadline, or was intentionally aborted.
    for job in state['jobs']:
        for attempt in range(3):
            proc = subprocess.run(['python3', 'workbench/tools/worker.py', 'cancel', job], cwd=ROOT, capture_output=True, text=True)
            if proc.returncode == 0:
                break
            state['cleanup_errors'].append({'job': job, 'utc': time.time(), 'error': proc.stderr or proc.stdout})
            save()
            time.sleep(3)
    group = state['security_group_id']
    deleted = False
    try:
        for attempt in range(60):
            instances = aws.call('ec2', 'describe-instances', Filters=[{'Name': 'instance.group-id', 'Values': [group]}])
            active = [i for r in instances['Reservations'] for i in r['Instances'] if i['State']['Name'] != 'terminated']
            if not active:
                break
            time.sleep(5)
        else:
            raise RuntimeError('instances did not terminate; SG retained for retry')
        try:
            groups = aws.call('ec2', 'describe-security-groups', GroupIds=[group])['SecurityGroups']
            if groups[0]['IpPermissions']:
                aws.call('ec2', 'revoke-security-group-ingress', GroupId=group, IpPermissions=groups[0]['IpPermissions'])
            aws.call('ec2', 'delete-security-group', GroupId=group)
            deleted = True
        except worker.AwsError as error:
            if error.code == 'InvalidGroup.NotFound':
                deleted = True
            else:
                raise
        state['cleanup'] = {'utc': time.time(), 'instances_terminated': True, 'security_group_deleted': deleted}
    except Exception as error:
        state['cleanup_errors'].append({'utc': time.time(), 'error': str(error)})
    save()
    if deleted:
        print('Workers terminated and temporary security group removed.', flush=True)
    return int(not deleted or args.abort or any(r.get('exit_code') != 0 for r in state['collection'].values()))

if __name__ == '__main__':
    raise SystemExit(main())
