#!/usr/bin/env python3
"""Compare seven data devices on six disposable workers in use1-az4."""
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from worker import Aws
from worker_group import create,identifier


def main():
    aws=Aws('us-east-1')
    zones=aws.call('ec2','describe-availability-zones')['AvailabilityZones']
    zone=next(z for z in zones if z['ZoneId']=='use1-az4')
    subnets=aws.call('ec2','describe-subnets',Filters=[{'Name':'default-for-az','Values':['true']}])['Subnets']
    subnet=next(s for s in subnets if s['AvailabilityZoneId']==zone['ZoneId'])
    members={
        'ebs-zen5':{'config':{'instance_type':'c8a.xlarge','data_volumes':[
            {'name':'gp3','type':'gp3','size_gib':32,'iops':3000,'throughput_mib_s':125},
            {'name':'io2','type':'io2','size_gib':32,'iops':3000}]}},
        'hdd':{'config':{'instance_type':'d3.xlarge','instance_store_count':1,'env':{'DEVICE_CLASS':'hdd'}}},
        'sata-ssd':{'config':{'instance_type':'i2.xlarge','instance_store_count':1}},
        'nvme-i4i':{'config':{'instance_type':'i4i.xlarge','instance_store_count':1}},
        'nvme-i7i':{'config':{'instance_type':'i7i.xlarge','instance_store_count':1}},
        'nvme-arm':{'config':{'machine':'neoverse-v2','instance_type':'i8g.large','instance_store_count':1}},
    }
    spec={'script':'workbench/spikes/cft-commit-latency/persistence/cloud.sh',
          'config':{'capacity':'on-demand','threads_per_core':1,'deadline_seconds':5400,
                    'max_age_seconds':5400,'subnets':[subnet['SubnetId']]},
          'network':{'vpc_id':subnet['VpcId']},'members':members}
    group=create(spec,ROOT/'build/cft-commit-latency/storage'/identifier())
    group.update(study='power-safe persistence',zone=zone)
    group.launch()
    print(f'Collect: python3 workbench/tools/worker_group.py wait {group.receipt}',flush=True)


if __name__=='__main__':main()
