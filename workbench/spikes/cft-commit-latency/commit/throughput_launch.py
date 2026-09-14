#!/usr/bin/env python3
"""Launch three Graviton/NVMe workers for the latency/throughput frontier."""
from pathlib import Path
import sys
import argparse
import json
import subprocess
import time

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from worker import Aws
from worker_group import create,identifier


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--scale',action='store_true');parser.add_argument('--express',action='store_true');args=parser.parse_args()
    scaled=args.scale or args.express
    aws=Aws('us-east-1')
    subnets=aws.call('ec2','describe-subnets',Filters=[{'Name':'default-for-az','Values':['true']}])['Subnets']
    selected=[next(s for s in subnets if s['AvailabilityZoneId']==f'use1-az{i}') for i in [1,2,4]]
    assert len({s['VpcId'] for s in selected})==1
    spec={'script':'workbench/spikes/cft-commit-latency/commit/throughput_cloud.sh',
          'config':{'machine':'neoverse-v2','instance_type':'i8g.8xlarge' if scaled else 'i8g.large','capacity':'on-demand','threads_per_core':1,
                    'deadline_seconds':5400,'max_age_seconds':5400,'instance_store_count':1,
                    'env':{'CFT_RUN_URI':'{group_uri}','CFT_PROFILE':'express' if args.express else ('scale' if scaled else 'small')}},
          'network':{'vpc_id':selected[0]['VpcId'],'tcp_ports':[[43000,43001]],'udp_ports':[[43001,43001]],'icmp':True},
          'members':{s['AvailabilityZoneId']:{'config':{'subnets':[s['SubnetId']],
                     'env':{'CFT_NODE':str(index)}}} for index,s in enumerate(selected)}}
    group=create(spec,ROOT/'build/cft-commit-latency'/('throughput-express' if args.express else ('throughput-scale' if scaled else 'throughput'))/identifier())
    group.update(study='durable CFT commit latency/throughput frontier',az_ids=[s['AvailabilityZoneId'] for s in selected])
    group.launch()
    if args.express:
        state=group.read();instances=[]
        for member in state['members'].values():
            launch=json.loads((ROOT/'build/workers'/member['job']/'launch.json').read_text());instances.append(launch['instance_id'])
        interfaces=aws.call('ec2','describe-network-interfaces',Filters=[{'Name':'attachment.instance-id','Values':instances}])['NetworkInterfaces']
        assert len(interfaces)==3
        for interface in interfaces:
            aws.call('ec2','modify-network-interface-attribute',NetworkInterfaceId=interface['NetworkInterfaceId'],EnaSrdSpecification={'EnaSrdEnabled':True,'EnaSrdUdpSpecification':{'EnaSrdUdpEnabled':True}})
        for attempt in range(20):
            interfaces=aws.call('ec2','describe-network-interfaces',NetworkInterfaceIds=[i['NetworkInterfaceId'] for i in interfaces])['NetworkInterfaces']
            settings=[i.get('Attachment',{}).get('EnaSrdSpecification',{}) for i in interfaces]
            if all(s.get('EnaSrdEnabled') and s.get('EnaSrdUdpSpecification',{}).get('EnaSrdUdpEnabled') for s in settings):break
            time.sleep(1)
        else:raise RuntimeError('ENA Express activation not confirmed; cohort remains gated')
        activation={'interfaces':interfaces,'utc':time.time(),'tcp':True,'udp':True}
        path=group.directory/'ena-express-ready.json';path.write_text(json.dumps(activation,indent=2)+'\n')
        group.update(ena_express=activation)
        subprocess.run(['aws','s3','cp','--only-show-errors',str(path),state['uri']+'/ena-express-ready.json'],check=True)
    print(f'Collect: python3 workbench/tools/worker_group.py wait {group.receipt}',flush=True)


if __name__=='__main__':main()
