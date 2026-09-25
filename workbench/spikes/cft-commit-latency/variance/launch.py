#!/usr/bin/env python3
"""Fixed-tuple variance study; wide by default, optional dense two-AZ control."""
import argparse
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from worker import Aws
from worker_group import create,identifier


def main():
    p=argparse.ArgumentParser()
    mode=p.add_mutually_exclusive_group()
    mode.add_argument('--focused',action='store_true',help='four hosts per AZ in az2/az4; 16 tuples, four rounds')
    mode.add_argument('--port-sampling',action='store_true',help='equal-budget consecutive vs scattered ephemeral ports')
    args=p.parse_args()
    aws=Aws('us-east-1')
    subnets=aws.call('ec2','describe-subnets',Filters=[{'Name':'default-for-az','Values':['true']}])['Subnets']
    selected=sorted((s for s in subnets if not args.focused or s['AvailabilityZoneId'] in ('use1-az2','use1-az4')),key=lambda s:s['AvailabilityZoneId'])
    assert len(selected)==(2 if args.focused else 6) and len({s['VpcId'] for s in selected})==1
    suffixes='abcd' if args.focused else 'ab'
    env={'ONEWAY_COUNT':'100' if args.focused else '80',
         'ONEWAY_NODES':'8' if args.focused else '12',
         'ONEWAY_FLOWS':'16' if args.focused else '4',
         'ONEWAY_LAYOUT':'bipartite' if args.focused else 'full',
         'ONEWAY_PORT_MODE':'both' if args.focused else 'lower-only',
         'ONEWAY_ROUNDS':'4' if args.focused else '5'}
    if args.port_sampling:
        env.update(ONEWAY_COUNT='60', ONEWAY_FLOWS='64', ONEWAY_PORT_MODE='port-sampling', ONEWAY_PACE_US='2000')
    spec={'script':'workbench/spikes/cft-commit-latency/oneway/cloud.sh',
          'config':{'instance_type':'m7i.xlarge','capacity':'on-demand','threads_per_core':1,
                    'setup':'minimal','deadline_seconds':2700 if args.port_sampling else 1800,'idle_seconds':0,'max_age_seconds':3600,
                    'env':env},
          'network':{'scope':'cft-variance-20260924','vpc_id':selected[0]['VpcId'],
                     'tcp_ports':[[43400,43400]],'udp_ports':[[48000,48015],[48100,48115]]},
          'members':{s['AvailabilityZoneId']+suffix:{'config':{'subnets':[s['SubnetId']],
                     'instance_type':'i4i.xlarge' if s['AvailabilityZoneId']=='use1-az3' else 'm7i.xlarge',
                     'env':{'AZ_NODE':str(az*len(suffixes)+index)}}}
                     for az,s in enumerate(selected) for index,suffix in enumerate(suffixes)}}
    if args.port_sampling:
        spec['network'].update(scope='cft-port-sampling-20260925', udp_ports=[[48100,48100],[49152,65535]])
    group=create(spec,ROOT/'build/az-variance'/identifier())
    group.launch()
    print(f'Collect: python3 workbench/tools/worker_group.py wait {group.receipt}')


if __name__=='__main__':main()
