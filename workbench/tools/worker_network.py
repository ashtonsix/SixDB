"""Owned private networks for worker groups; scoped networks outlive one cohort."""
from contextlib import nullcontext
import hashlib
import json
from pathlib import Path
import re
import time

import storage
import worker


def normalize(network):
    if not isinstance(network, dict) or not network.get('vpc_id'):
        raise ValueError('network needs a VPC ID and TCP/UDP port ranges within 1..65535')
    if set(network) - {'vpc_id', 'scope', 'tcp_ports', 'udp_ports', 'icmp'}:
        raise ValueError('unknown network setting')
    result = dict(network)
    for protocol in ('tcp', 'udp'):
        ranges = network.get(protocol + '_ports', [])
        if not isinstance(ranges, list) or any(not (isinstance(p, list) and len(p) == 2
                and all(type(n) is int for n in p) and 0 < p[0] <= p[1] <= 65535) for p in ranges):
            raise ValueError('network needs a VPC ID and TCP/UDP port ranges within 1..65535')
        merged = []
        for a, b in sorted(ranges):
            if merged and a <= merged[-1][1] + 1:
                merged[-1][1] = max(b, merged[-1][1])
            else:
                merged.append([a, b])
        result[protocol + '_ports'] = merged
    if type(network.get('icmp', False)) is not bool:
        raise ValueError('network icmp must be a boolean')
    result['icmp'] = network.get('icmp', False)
    if 'scope' in network and (not isinstance(network['scope'], str) or
            not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', network['scope'])):
        raise ValueError('network scope uses 1..80 letters, digits, underscore or hyphen')
    return result


def identity(state):
    network = state['network']
    return hashlib.sha256(json.dumps([state['region'], network['vpc_id'], network['scope']]).encode()).hexdigest()[:24]


def lock(state):
    # Shared by checkouts/worktrees on this Linux controller, including cleanup.
    # Concurrent controllers on different hosts should use distinct scopes.
    if not (state.get('network') or {}).get('scope'):
        return nullcontext()
    return storage.lock(Path.home() / '.cache/sixdb/worker-network-locks' / (identity(state) + '.lock'))


def policy(network):
    return hashlib.sha256(json.dumps({k: network.get(k, False if k == 'icmp' else [])
        for k in ('tcp_ports', 'udp_ports', 'icmp')}, sort_keys=True).encode()).hexdigest()


def tags(state):
    network = state['network']
    return ({'Project': 'SixDB', 'SixDBWorkerScope': network['scope'], 'SixDBWorkerPolicy': policy(network)}
            if network.get('scope') else {'Project': 'SixDB', 'SixDBWorkerGroup': state['id']})


def find(state, aws):
    network = state['network']
    return aws.call('ec2', 'describe-security-groups', Filters=[
        {'Name': 'group-name', 'Values': [network['name']]},
        {'Name': 'vpc-id', 'Values': [network['vpc_id']]}])['SecurityGroups']


def verify(state, group):
    actual = {t['Key']: t['Value'] for t in group.get('Tags', [])}
    expected = tags(state)
    # Older temporary groups were already identified by this ownership tag.
    keys = expected if state['network'].get('scope') else ['SixDBWorkerGroup']
    if any(actual.get(k) != expected[k] for k in keys):
        raise ValueError('security group ownership or policy differs; preserved (choose a new scope for a new policy)')


def rules(network, group_id):
    result = [{'IpProtocol': protocol, 'FromPort': a, 'ToPort': b,
               'UserIdGroupPairs': [{'GroupId': group_id}]}
              for protocol in ('tcp', 'udp') for a, b in network.get(protocol + '_ports', [])]
    if network.get('icmp'):
        result.append({'IpProtocol': 'icmp', 'FromPort': -1, 'ToPort': -1,
                       'UserIdGroupPairs': [{'GroupId': group_id}]})
    return result


def permissions(values):
    # Ignore AWS-added group/account descriptions; retain every actual ingress source.
    return sorted((str(p['IpProtocol']), p.get('FromPort'), p.get('ToPort'),
                   tuple(sorted(g['GroupId'] for g in p.get('UserIdGroupPairs', []))),
                   json.dumps(p.get('IpRanges', []), sort_keys=True),
                   json.dumps(p.get('Ipv6Ranges', []), sort_keys=True),
                   json.dumps(p.get('PrefixListIds', []), sort_keys=True)) for p in values)


def ensure(state, aws, record):
    network = dict(state['network'])
    existing = find(state, aws) if network.get('scope') else []
    if existing:
        group, = existing
        verify(state, group)
        if permissions(group.get('IpPermissions', [])) != permissions(rules(network, group['GroupId'])):
            raise ValueError('security group ingress differs; preserved (inspect it or choose a new scope)')
        network['id'] = group['GroupId']
        record(network=network, network_removed=False)
        return network['id']
    group_id = aws.call('ec2', 'create-security-group', GroupName=network['name'],
        Description='SixDB worker network', VpcId=network['vpc_id'],
        TagSpecifications=[{'ResourceType': 'security-group', 'Tags': [
            {'Key': k, 'Value': v} for k, v in tags(state).items()]}])['GroupId']
    network['id'] = group_id
    record(network=network, network_removed=False)
    ingress = rules(network, group_id)
    if ingress:
        aws.call('ec2', 'authorize-security-group-ingress', GroupId=group_id, IpPermissions=ingress)
    return group_id


def remove(state, aws, record):
    """Delete only owned, unused networks. ENI dependencies remain a pending cleanup."""
    network = state.get('network')
    if not network:
        return True
    if state.get('network_removed') and not network.get('scope'):
        return True
    try:
        for group in find(state, aws):
            verify(state, group)
            group_id = group['GroupId']
            for attempt in range(1 if network.get('scope') else 60):
                reservations = aws.call('ec2', 'describe-instances', Filters=[
                    {'Name': 'instance.group-id', 'Values': [group_id]}])['Reservations']
                if not any(i['State']['Name'] != 'terminated' for r in reservations for i in r['Instances']):
                    break
                if network.get('scope'):
                    raise RuntimeError('scope still has instances; leave them reusable or retry after they expire')
                time.sleep(5)
            else:
                raise RuntimeError('instances still reference the security group; retry cleanup later')
            # Delete directly: do not dismantle an intact policy if an ENI still depends on it.
            for attempt in range(12):
                try:
                    aws.call('ec2', 'delete-security-group', GroupId=group_id)
                    break
                except worker.AwsError as error:
                    if error.code == 'InvalidGroup.NotFound':
                        break
                    if error.code != 'DependencyViolation' or attempt == 11:
                        raise
                    time.sleep(5)
        record(network_removed=True, network_error=None, network_checked_at=time.time())
        return True
    except Exception as error:
        record(network_removed=False, network_error=str(error), network_checked_at=time.time())
        return False
