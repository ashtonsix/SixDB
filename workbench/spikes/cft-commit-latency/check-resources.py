#!/usr/bin/env python3
"""Read-only AWS receipt check for this study's instances, volumes and private groups."""
import argparse,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from worker import Aws


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('groups',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 aws=Aws('us-east-1');jobs=[];networks=[];receipts=[]
 for path in a.groups:
  g=json.loads(path.read_text());jobs += [m['job'] for m in g['members'].values()]
  if g.get('network'):networks.append(g['network']['id'])
  receipts.append(dict(group=g['id'],network_removed=g.get('network_removed',False),members={name:member.get('collection') for name,member in g['members'].items()}))
 filters=[{'Name':'tag:SixDBWorkerJob','Values':jobs}]
 instances=aws.call('ec2','describe-instances',Filters=filters)['Reservations']
 instance_rows=[dict(id=i['InstanceId'],state=i['State']['Name'],job=next(t['Value'] for t in i['Tags'] if t['Key']=='SixDBWorkerJob')) for r in instances for i in r['Instances']]
 volumes=aws.call('ec2','describe-volumes',Filters=filters)['Volumes']
 groups=aws.call('ec2','describe-security-groups',Filters=[{'Name':'group-id','Values':networks}])['SecurityGroups'] if networks else []
 result=dict(utc=time.time(),jobs=jobs,instances=instance_rows,remaining_volumes=[v['VolumeId'] for v in volumes],remaining_private_groups=[g['GroupId'] for g in groups],receipts=receipts)
 result['clean']=not volumes and not groups and all(i['state']=='terminated' for i in instance_rows)
 a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
