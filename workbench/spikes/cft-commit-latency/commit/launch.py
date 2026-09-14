#!/usr/bin/env python3
"""Launch four Graviton/NVMe workers for good and bad durable quorum paths."""
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from worker import Aws
from worker_group import create,identifier


def main():
    aws=Aws('us-east-1')
    subnets=aws.call('ec2','describe-subnets',Filters=[{'Name':'default-for-az','Values':['true']}])['Subnets']
    selected=[next(s for s in subnets if s['AvailabilityZoneId']==f'use1-az{i}') for i in [1,2,4,6]]
    assert len({s['VpcId'] for s in selected})==1
    spec={'script':'workbench/spikes/cft-commit-latency/commit/cloud.sh',
          'config':{'machine':'neoverse-v2','instance_type':'i8g.large','capacity':'on-demand','threads_per_core':1,
                    'deadline_seconds':7200,'max_age_seconds':7200,'instance_store_count':1,
                    'data_volumes':[{'name':'gp3','type':'gp3','size_gib':32,'iops':3000,'throughput_mib_s':125},
                                    {'name':'io2','type':'io2','size_gib':32,'iops':3000}],
                    'env':{'CFT_RUN_URI':'{group_uri}'}},
          'network':{'vpc_id':selected[0]['VpcId'],'tcp_ports':[[43000,43001]],'udp_ports':[[43001,43001]],'icmp':True},
          'members':{s['AvailabilityZoneId']:{'config':{'subnets':[s['SubnetId']],
                     'env':{'CFT_NODE':str(index)}}} for index,s in enumerate(selected)}}
    group=create(spec,ROOT/'build/cft-commit-latency/commit'/identifier())
    group.update(study='durable stable-leader CFT commit fast path',az_ids=[s['AvailabilityZoneId'] for s in selected])
    group.launch()
    print(f'Collect: python3 workbench/tools/worker_group.py wait {group.receipt}',flush=True)


if __name__=='__main__':main()
