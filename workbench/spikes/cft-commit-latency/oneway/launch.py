#!/usr/bin/env python3
"""Small clock preflight or six-AZ measurement; shared tools own resources."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from worker import Aws
from worker_group import create, identifier


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preflight', action='store_true')
    parser.add_argument('--phc', action='store_true')
    parser.add_argument('--samples',type=int,default=1000)
    args = parser.parse_args()
    aws = Aws('us-east-1')
    subnets = aws.call('ec2', 'describe-subnets', Filters=[{'Name': 'default-for-az', 'Values': ['true']}])['Subnets']
    subnets = sorted(subnets, key=lambda s: s['AvailabilityZoneId'])
    assert len(subnets) == 6 and len({s['VpcId'] for s in subnets}) == 1
    selected = [s for s in subnets if s['AvailabilityZoneId'] in ('use1-az2', 'use1-az3')] if args.preflight else subnets
    spec = {
        'script': 'workbench/spikes/cft-commit-latency/oneway/' + ('preflight.sh' if args.preflight else 'cloud.sh'),
        'config': {'instance_type': 'm7i.xlarge', 'capacity': 'on-demand', 'threads_per_core': 1,
                   'setup': 'minimal', 'deadline_seconds': 900 if args.preflight else 1800,
                   'idle_seconds': 600 if args.preflight else 0, 'max_age_seconds': 5400,
                   'env': {'ENABLE_PHC': '1' if args.phc else '0','ONEWAY_COUNT':str(args.samples)}},
        'network': {'scope': 'cft-oneway-20260924', 'vpc_id': subnets[0]['VpcId'],
                    'tcp_ports': [[43400, 43400]], 'udp_ports': [[43401, 43401]]},
        'members': {s['AvailabilityZoneId'] + suffix: {'config': {
            'instance_type': 'i4i.xlarge' if s['AvailabilityZoneId'] == 'use1-az3' else 'm7i.xlarge',
            'subnets': [s['SubnetId']], 'env': {'AZ_NODE': str(2 * (int(s['AvailabilityZoneId'][-1]) - 1) + index)}}}
            for s in selected for index, suffix in enumerate([''] if args.preflight else ['a', 'b'])},
    }
    group = create(spec, ROOT / 'build/az-oneway' / identifier())
    group.launch()
    print(f'Collect: python3 workbench/tools/worker_group.py wait {group.receipt}')


if __name__ == '__main__':
    main()
