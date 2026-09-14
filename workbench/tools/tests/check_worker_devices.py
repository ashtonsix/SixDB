#!/usr/bin/env python3
"""Offline disk identity, root exclusion, launch mapping and reuse checks; no device writes."""
import argparse
import base64
import copy
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import artifacts
import check_worker
import worker
import worker_pool as pool
import worker_runtime as runtime


def config(**overlay):
    return worker.configuration(argparse.Namespace(), worker.settings(
        {'instance_profile': 'fixture', 'security_group_id': 'sg-fixture'} | overlay))


def resolve(value, *, nvme=True, hypervisor='nitro', mappings=()):
    responses = {
        'describe-instance-types': {'InstanceTypes': [{'Hypervisor': hypervisor,
            'VCpuInfo': {'DefaultThreadsPerCore': 1}, 'MemoryInfo': {},
            'ProcessorInfo': {'SupportedArchitectures': ['x86_64']},
            'InstanceStorageInfo': {'NvmeSupport': 'required' if nvme else 'unsupported', 'Disks': [{'Count': 2}]}}]},
        'describe-images': {'Images': [{'State': 'available', 'Architecture': 'x86_64', 'Name': 'fixture',
            'RootDeviceName': '/dev/xvda1', 'BlockDeviceMappings': list(mappings)}]},
        'describe-subnets': {'Subnets': [{'SubnetId': 'subnet-a', 'AvailabilityZone': 'us-east-1a',
                                       'AvailableIpAddressCount': 2, 'MapPublicIpOnLaunch': True}]},
        'describe-instance-type-offerings': {'InstanceTypeOfferings': [{'Location': 'us-east-1a'}]}}
    aws = Mock(spec=worker.Aws)
    aws.call.side_effect = lambda service, operation, **kw: responses[operation]
    return worker.resolve(value | {'vpc_id': 'vpc-a'}, aws)


class ConfigurationTests(unittest.TestCase):
    def test_new_volumes_only_and_actionable_configuration_errors(self):
        good = {'name': 'wal', 'type': 'gp3', 'size_gib': 32}
        for bad in [good | {'name': 1}, good | {'name': '../root'}, good | {'size_gib': True},
                    good | {'size_gib': 0}, good | {'type': 'io2'}, good | {'iops': -1},
                    good | {'attachment': '/dev/sda'}, good | {'volume_id': 'vol-existing'},
                    good | {'SnapshotId': 'snap-old'}, good | {'type': 'io2', 'iops': 3000, 'throughput_mib_s': 125}]:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                config(data_volumes=[bad])
        with self.assertRaises(ValueError):
            config(data_volumes=[good, good])
        for count in (-1, True, '1'):
            with self.assertRaises(ValueError):
                config(instance_store_count=count)
        with self.assertRaises(ValueError):
            worker.environment(['SIXDB_DEVICES=/dev/root'])

    def test_resolved_launch_keeps_root_and_new_volumes_separate(self):
        value = resolve(config(data_volumes=[{'name': 'bulk', 'type': 'gp3', 'size_gib': 32},
            {'name': 'wal', 'type': 'io2', 'size_gib': 16, 'iops': 3000}]),
            mappings=[{'DeviceName': '/dev/sda1'}, {'DeviceName': '/dev/xvdf'}])
        request = worker.launch_request(check_worker.job() | {'config': value}, value['subnets'][0], 'spot')
        root, bulk, wal = request['BlockDeviceMappings']
        self.assertEqual(root['DeviceName'], '/dev/xvda1')
        self.assertEqual(root['Ebs']['VolumeSize'], 24)
        self.assertEqual(bulk, {'DeviceName': '/dev/sdg', 'Ebs': {'VolumeSize': 32, 'VolumeType': 'gp3',
            'Iops': 3000, 'Throughput': 125, 'Encrypted': True, 'DeleteOnTermination': True}})
        self.assertEqual(wal, {'DeviceName': '/dev/sdh', 'Ebs': {'VolumeSize': 16, 'VolumeType': 'io2',
            'Iops': 3000, 'Encrypted': True, 'DeleteOnTermination': True}})
        script = base64.b64decode(request['UserData']).decode()
        self.assertIn(' amazon-ec2-utils', script)
        subprocess.run(['bash', '-n'], input=script, text=True, check=True)

    def test_instance_store_capability_and_legacy_launch_mapping(self):
        inherited = [{'DeviceName': '/dev/sdb', 'VirtualName': 'ephemeral0'},
                     {'DeviceName': '/dev/sdc', 'VirtualName': 'ephemeral1'}]
        value = resolve(config(instance_store_count=1), nvme=False, hypervisor='xen', mappings=inherited)
        self.assertEqual(value['instance_store_mappings'], [{'name': 'ephemeral0', 'attachment': '/dev/sdb'}])
        request = worker.launch_request(check_worker.job() | {'config': value}, value['subnets'][0], 'spot')
        self.assertEqual(request['BlockDeviceMappings'][1], {'DeviceName': '/dev/sdb', 'VirtualName': 'ephemeral0'})
        self.assertEqual(resolve(config(instance_store_count=1), nvme=False, hypervisor='xen')['instance_store_mappings'],
                         value['instance_store_mappings'])
        with self.assertRaisesRegex(ValueError, 'overlaps the root'):
            resolve(config(instance_store_count=1), nvme=False, mappings=[{'DeviceName': '/dev/sda', 'VirtualName': 'ephemeral0'}])
        self.assertEqual(resolve(config(instance_store_count=1))['instance_store_mappings'], [])
        with self.assertRaisesRegex(ValueError, 'local-disk count'):
            resolve(config(instance_store_count=3))
        with self.assertRaisesRegex(ValueError, 'Nitro'):
            resolve(config(data_volumes=[{'name': 'data', 'type': 'gp3', 'size_gib': 32}]), hypervisor='xen')

    def test_changed_disk_requirements_cannot_reuse_the_same_profile(self):
        source = {'runtime_sha256': 'r', 'pool_sha256': 'p', 'setup_sha256': 's'}
        value = resolve(config(data_volumes=[{'name': 'data', 'type': 'gp3', 'size_gib': 32}]))
        original = pool.profile(value, source)
        for change in ({'size_gib': 64}, {'iops': 6000}, {'throughput_mib_s': 250}, {'name': 'other'}, {'type': 'io2'}):
            altered = value | {'data_volumes': [value['data_volumes'][0] | change]}
            self.assertNotEqual(pool.profile(altered, source), original)
        self.assertEqual(pool.profile(copy.deepcopy(value), source), original)
        self.assertNotEqual(pool.profile(resolve(config(instance_store_count=1)), source),
                            pool.profile(resolve(config(instance_store_count=2)), source))
        plain = config()
        self.assertEqual(pool.profile(plain, source), pool.profile(plain | {'data_volumes': [], 'instance_store_count': 0}, source))


def disk(name, *, model='Amazon Elastic Block Store', serial='vol0123', size=32 * 1024**3, mounts=None, children=()):
    return {'name': '/dev/' + name, 'type': 'disk', 'size': size, 'model': model, 'serial': serial,
            'mountpoints': mounts or [None], 'children': list(children)}


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.root = disk('nvme7n1', serial='volabcd', children=[{
            'name': '/dev/nvme7n1p1', 'type': 'part', 'mountpoints': [None], 'children': [
                {'name': '/dev/mapper/root', 'type': 'lvm', 'mountpoints': ['/']}]}])
        self.ebs = disk('nvme2n1')
        self.local = disk('nvme0n1', model='Amazon EC2 NVMe Instance Storage', serial='AWSLOCAL')
        self.inventory = [self.root, self.local, self.ebs]
        self.ids = {'/dev/nvme2n1': 'Volume ID: vol-0123\nsdf\n'}
        self.value = {'data_volumes': [{'name': 'data', 'attachment': '/dev/sdf', 'type': 'gp3',
                                      'size_gib': 32, 'iops': 3000, 'throughput_mib_s': 125}]}

    def discover(self, **config):
        def read(command, **kw):
            self.assertTrue(kw['check'])
            if command[0] == 'lsblk':
                output = json.dumps({'blockdevices': self.inventory})
            elif command[0] == 'ebsnvme-id':
                output = self.ids[command[1]]
            else:
                raise AssertionError(f'unexpected device command: {command}')
            return subprocess.CompletedProcess(command, 0, output, '')
        with patch.object(runtime.subprocess, 'run', side_effect=read):
            return runtime.data_devices(config or self.value)

    def test_shuffled_nvme_names_and_root_ancestry(self):
        result = self.discover(instance_store_count=1, instance_store_nvme=True, **self.value)
        self.assertEqual(result['root_devices'], ['/dev/nvme7n1'])
        self.assertEqual(result['ebs']['data']['device'], '/dev/nvme2n1')
        self.assertEqual(result['ebs']['data']['volume_id'], 'vol-0123')
        self.assertEqual([d['device'] for d in result['instance_store']], ['/dev/nvme0n1'])
        self.assertFalse(any('plp' in k.lower() for k in result['ebs']['data']))
        self.ids['/dev/nvme2n1'] = 'Volume ID: vol-0123\n/dev/sdf\n'
        self.assertEqual(self.discover()['ebs']['data']['attachment'], '/dev/sdf')

    def test_only_requested_ebs_is_exposed_and_mounted_data_is_reported(self):
        self.inventory.append(disk('nvme3n1', serial='vol4567'))
        self.ids['/dev/nvme3n1'] = 'Volume ID: vol-4567\nsdg\n'
        self.ebs['children'] = [{'name': '/dev/nvme2n1p1', 'type': 'part', 'mountpoints': ['/mnt/study']}]
        result = self.discover()
        self.assertEqual(set(result['ebs']), {'data'})
        self.assertEqual(result['ebs']['data']['mountpoints'], ['/mnt/study'])

    def test_missing_ambiguous_and_wrong_identity_fail_closed(self):
        for change in ({'serial': 'volffff'}, {'size': 4096}):
            with self.subTest(change=change):
                previous = self.ebs.copy()
                self.ebs.update(change)
                with self.assertRaisesRegex(ValueError, 'identity/size mismatch'):
                    self.discover()
                self.ebs.update(previous)
        self.inventory.append(disk('nvme4n1'))
        self.ids['/dev/nvme4n1'] = self.ids['/dev/nvme2n1']
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            self.discover()
        self.inventory = [self.root, self.local]
        with self.assertRaisesRegex(ValueError, 'missing or excluded as root'):
            self.discover()
        self.inventory = [self.ebs]
        with self.assertRaisesRegex(ValueError, 'root-disk ancestry'):
            self.discover()

    def test_root_cannot_be_requested_as_local_or_ebs_data(self):
        self.ebs['mountpoints'] = ['/']
        with self.assertRaisesRegex(ValueError, 'excluded as root'):
            self.discover()
        self.local['mountpoints'] = ['/boot']
        with self.assertRaisesRegex(ValueError, 'non-root NVMe'):
            self.discover(instance_store_count=1, instance_store_nvme=True)

    def test_legacy_ephemeral_mapping_uses_imds_and_whole_disk_alias(self):
        self.inventory = [disk('xvda', mounts=['/']), disk('xvdb', model='', serial='')]
        value = {'instance_store_count': 1, 'instance_store_nvme': False,
                 'instance_store_mappings': [{'name': 'ephemeral0', 'attachment': '/dev/sdb'}]}
        with patch.object(runtime, 'metadata', return_value='sdb') as metadata:
            result = self.discover(**value)
            metadata.assert_called_once_with('meta-data/block-device-mapping/ephemeral0', text=True)
        self.assertEqual(result['instance_store'][0]['device'], '/dev/xvdb')
        self.assertEqual(result['instance_store'][0]['identity'], 'imds:ephemeral0')
        with patch.object(runtime, 'metadata', return_value='sda'), self.assertRaisesRegex(ValueError, 'mismatch'):
            self.discover(**value)
        value['instance_store_mappings'][0]['attachment'] = '/dev/sda'
        with patch.object(runtime, 'metadata', return_value='sda'), self.assertRaisesRegex(ValueError, 'root or ambiguous'):
            self.discover(**value)

    def test_metadata_supports_json_and_plain_mapping_responses(self):
        for payload, options, expected in [(b'{"instanceId":"i-a"}', {}, {'instanceId': 'i-a'}),
                                           (b'sdb\n', {'text': True}, 'sdb')]:
            with patch.object(runtime.urllib.request, 'urlopen', side_effect=[io.BytesIO(b'token'), io.BytesIO(payload)]):
                self.assertEqual(runtime.metadata('fixture', **options), expected)


class RuntimeIntegrationTests(unittest.TestCase):
    def test_device_receipt_reaches_script_and_discovery_failure_prevents_execution(self):
        for failed in (False, True):
            with self.subTest(failed=failed), tempfile.TemporaryDirectory() as temp:
                running, events, publish = check_worker.RuntimeTests().fixture(Path(temp),
                    'cp "$SIXDB_DEVICES" "$SIXDB_RESULTS/seen.json"\n')
                running.config['instance_store_count'] = 1
                receipt = {'format': 1, 'ebs': {}, 'instance_store': [{'device': '/dev/xvdb'}]}
                with patch.object(runtime, 'data_devices', side_effect=ValueError('ambiguous disk') if failed else None,
                                  return_value=receipt), patch.object(artifacts, 'publish', side_effect=publish), \
                        patch.object(runtime, 'metadata', return_value={}):
                    self.assertEqual(running.run(), int(failed))
                recovered = Path(temp) / 'recovered'
                if failed:
                    self.assertFalse((recovered / 'seen.json').exists())
                    self.assertEqual(events[-1]['error'], 'ambiguous disk')
                    self.assertIsNone(events[-1]['script_returncode'])
                else:
                    self.assertEqual(json.loads((recovered / 'seen.json').read_text()), receipt)
                    self.assertEqual(json.loads((recovered / 'devices.json').read_text()), receipt)


if __name__ == '__main__':
    unittest.main()
