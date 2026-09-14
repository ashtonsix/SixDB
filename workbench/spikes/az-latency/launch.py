#!/usr/bin/env python3
"""Choose the AZ cohort; shared worker-group tooling owns its lifecycle."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from worker import Aws
from worker_group import create, identifier


def main():
    aws = Aws('us-east-1')
    zones = aws.call('ec2', 'describe-availability-zones', Filters=[{'Name': 'zone-type', 'Values': ['availability-zone']}])['AvailabilityZones']
    zones = sorted(zones, key=lambda z: z['ZoneId'])
    assert len(zones) == 6
    subnets = aws.call('ec2', 'describe-subnets', Filters=[{'Name': 'default-for-az', 'Values': ['true']}])['Subnets']
    chosen = [next(s for s in subnets if s['AvailabilityZone'] == z['ZoneName']) for z in zones]
    assert len({s['VpcId'] for s in chosen}) == 1
    spec = {
        'script': 'workbench/spikes/az-latency/cloud.sh',
        'config': {'instance_type': 'i4i.xlarge', 'capacity': 'on-demand', 'threads_per_core': 1,
                   'deadline_seconds': 3600, 'max_age_seconds': 3600,
                   'env': {'AZ_RUN_URI': '{group_uri}'}},
        'network': {'vpc_id': chosen[0]['VpcId'], 'tcp_ports': [[43000, 43001]], 'icmp': True},
        'members': {zone['ZoneId']: {'config': {'subnets': [subnet['SubnetId']], 'env': {'AZ_NODE': str(index)}}}
                    for index, (zone, subnet) in enumerate(zip(zones, chosen))},
    }
    group = create(spec, ROOT / 'build/az-latency' / identifier())
    group.update(zones=zones)
    group.launch()
    print(f'Collect: python3 workbench/tools/worker_group.py wait {group.receipt}')


if __name__ == '__main__':
    main()
