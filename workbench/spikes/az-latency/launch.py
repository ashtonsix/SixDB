#!/usr/bin/env python3
"""Launch six dedicated workers with the existing worker lifecycle helper."""
import datetime
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from worker import Aws


def main():
    aws = Aws('us-east-1')
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    directory = ROOT / 'build/az-latency' / run_id
    directory.mkdir(parents=True)
    state = {'run_id': run_id, 'directory': str(directory), 'uri': 's3://calico-fleet-artifacts/sixdb/az-latency/' + run_id, 'jobs': []}
    def save():
        (directory / 'campaign.json').write_text(json.dumps(state, indent=2) + '\n')
    save()
    print(directory, flush=True)
    zones = aws.call('ec2', 'describe-availability-zones', Filters=[{'Name': 'zone-type', 'Values': ['availability-zone']}])['AvailabilityZones']
    zones = sorted(zones, key=lambda z: z['ZoneId'])
    assert len(zones) == 6
    subnets = aws.call('ec2', 'describe-subnets', Filters=[{'Name': 'default-for-az', 'Values': ['true']}])['Subnets']
    chosen = [next(s for s in subnets if s['AvailabilityZone'] == z['ZoneName']) for z in zones]
    assert len({s['VpcId'] for s in chosen}) == 1
    offered = aws.call('ec2', 'describe-instance-type-offerings', LocationType='availability-zone', Filters=[{'Name': 'instance-type', 'Values': ['i4i.xlarge']}])['InstanceTypeOfferings']
    assert {z['ZoneName'] for z in zones} <= {o['Location'] for o in offered}
    state['zones'] = zones
    capture = directory / 'source'
    subprocess.run(['python3', 'workbench/tools/capture.py', 'create', str(capture)], cwd=ROOT, check=True)
    group = aws.call('ec2', 'create-security-group', GroupName='sixdb-az-latency-' + run_id,
                     Description='Temporary SixDB private AZ RTT study', VpcId=chosen[0]['VpcId'],
                     TagSpecifications=[{'ResourceType': 'security-group', 'Tags': [{'Key': 'Project', 'Value': 'SixDB'}, {'Key': 'AZLatencyRun', 'Value': run_id}]}])['GroupId']
    state['security_group_id'] = group
    save()
    try:
        aws.call('ec2', 'authorize-security-group-ingress', GroupId=group, IpPermissions=[
            {'IpProtocol': 'tcp', 'FromPort': 43000, 'ToPort': 43001, 'UserIdGroupPairs': [{'GroupId': group}]},
            {'IpProtocol': 'icmp', 'FromPort': -1, 'ToPort': -1, 'UserIdGroupPairs': [{'GroupId': group}]}])
        for index, (zone, subnet) in enumerate(zip(zones, chosen)):
            overlay = directory / f'node-{index}.json'
            overlay.write_text(json.dumps({'vpc_id': subnet['VpcId'], 'subnets': [subnet['SubnetId']],
                                          'security_group_id': group, 'instance_profile': 'sixdb-worker', 'threads_per_core': 1}))
            command = ['python3', 'workbench/tools/worker.py', '--config', str(overlay), 'run',
                       'workbench/spikes/az-latency/cloud.sh', '--source', str(capture), '--instance-type', 'i4i.xlarge',
                       '--capacity', 'on-demand', '--fresh', '--idle-seconds', '0', '--deadline', '3600', '--max-age', '3600',
                       '--env', f'AZ_NODE={index}', '--env', 'AZ_RUN_URI=' + state['uri'], '--detach']
            proc = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (directory / f'launch-{index}.txt').write_text(proc.stdout)
            print(proc.stdout, flush=True)
            # Recover identity even if submission failed after job creation.
            candidates = [p for p in (ROOT / 'build/workers').glob('*/job.json')
                          if json.loads(p.read_text()).get('config', {}).get('env', {}).get('AZ_RUN_URI') == state['uri']]
            state['jobs'] = sorted({p.parent.name for p in candidates})
            save()
            proc.check_returncode()
        print(json.dumps(state, indent=2), flush=True)
    except BaseException:
        # An interrupted submission can already have created its job receipt.
        candidates = [p for p in (ROOT / 'build/workers').glob('*/job.json')
                      if json.loads(p.read_text()).get('config', {}).get('env', {}).get('AZ_RUN_URI') == state['uri']]
        state['jobs'] = sorted({p.parent.name for p in candidates})
        state['launch_failed'] = True
        save()
        subprocess.run(['python3', 'workbench/spikes/az-latency/collect.py',
                        str(directory / 'campaign.json'), '--abort'], cwd=ROOT)
        raise

if __name__ == '__main__':
    main()
