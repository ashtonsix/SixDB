#!/usr/bin/env python3
"""Bounded three-AZ cohort for a named cliff intervention plan."""
import argparse,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from worker import Aws
from worker_group import create,identifier

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan');parser.add_argument('--profile',choices=['small','scale','express'],default='small')
    args=parser.parse_args();aws=Aws('us-east-1')
    choices=aws.call('ec2','describe-subnets',Filters=[{'Name':'default-for-az','Values':['true']}])['Subnets']
    subnets=[next(s for s in choices if s['AvailabilityZoneId']==f'use1-az{i}') for i in [1,2,4]]
    require_plan=ROOT/args.plan
    if not require_plan.is_file():raise ValueError('plan must exist in captured source')
    spec={'script':'workbench/spikes/cft-commit-latency/commit/cliff_cloud.sh',
        'config':{'machine':'neoverse-v2','instance_type':'i8g.large' if args.profile=='small' else 'i8g.8xlarge','capacity':'on-demand',
            'threads_per_core':1,'deadline_seconds':3600,'max_age_seconds':3600,'disk_gb':64,'instance_store_count':1,
            'env':{'CFT_NODE':'0','CFT_PROFILE':args.profile,'CFT_PLAN':args.plan}},
        'network':{'vpc_id':subnets[0]['VpcId'],'tcp_ports':[[43000,43001]],'udp_ports':[[43001,43001]],'icmp':True},
        'members':{s['AvailabilityZoneId']:{'config':{'subnets':[s['SubnetId']],'env':{'CFT_NODE':str(i)}}} for i,s in enumerate(subnets)}}
    group=create(spec,ROOT/'build/cft-commit-latency/cliff'/args.profile/identifier())
    group.update(study='CFT repeated-run cliff interventions',plan=args.plan)
    group.launch()
    if args.profile=='express':
        state=group.read();instances=[json.loads((ROOT/'build/workers'/m['job']/'launch.json').read_text())['instance_id'] for m in state['members'].values()]
        interfaces=aws.call('ec2','describe-network-interfaces',Filters=[{'Name':'attachment.instance-id','Values':instances}])['NetworkInterfaces']
        for interface in interfaces:
            aws.call('ec2','modify-network-interface-attribute',NetworkInterfaceId=interface['NetworkInterfaceId'],EnaSrdSpecification={'EnaSrdEnabled':True,'EnaSrdUdpSpecification':{'EnaSrdUdpEnabled':True}})
        group.publish('network-ready',{'express':True,'interfaces':interfaces})
    print(group.receipt,flush=True)

if __name__=='__main__':main()
