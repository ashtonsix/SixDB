#!/usr/bin/env python3
"""Private reuse scopes preserve policy, exclusive reuse compatibility and pending cleanup."""
from copy import deepcopy
import unittest
from pathlib import Path
import tempfile
import threading
from unittest.mock import Mock, patch

import worker
import worker_network as networks


class NetworkCheck(unittest.TestCase):
    def setUp(self):
        self.state = {'id': 'one', 'region': 'us-east-1', 'network': networks.normalize({
            'scope': 'study', 'vpc_id': 'vpc-fixture', 'tcp_ports': [[43000, 43002], [43003, 43004]]}) | {'name': 'scope-fixture'}}
        self.aws = Mock(spec=worker.Aws)
        self.group = None
        self.busy = False
        self.dependency = False
        self.aws.call.side_effect = self.call

    def call(self, service, operation, **kw):
        if operation == 'describe-security-groups':
            return {'SecurityGroups': [deepcopy(self.group)] if self.group else []}
        if operation == 'create-security-group':
            self.group = {'GroupId': 'sg-fixture', 'IpPermissions': [], 'Tags': kw['TagSpecifications'][0]['Tags']}
            return {'GroupId': 'sg-fixture'}
        if operation == 'authorize-security-group-ingress':
            self.group['IpPermissions'] = kw['IpPermissions']
        if operation == 'describe-instances':
            return {'Reservations': [{'Instances': [{'State': {'Name': 'running' if self.busy else 'terminated'}}]}]}
        if operation == 'delete-security-group':
            if self.dependency:
                raise worker.AwsError('(DependencyViolation) when calling DeleteSecurityGroup')
            self.group = None
        return {}

    def test_same_scope_reuses_group_and_normalizes_policy_without_mutating_rules(self):
        self.assertEqual(networks.ensure(self.state, self.aws, self.state.update), 'sg-fixture')
        self.aws.call.reset_mock()
        second = self.state | {'id': 'two'}
        self.assertEqual(networks.ensure(second, self.aws, second.update), 'sg-fixture')
        self.assertEqual([c.args[1] for c in self.aws.call.call_args_list], ['describe-security-groups'])
        self.assertEqual(self.state['network']['tcp_ports'], [[43000, 43004]])

    def test_changed_policy_or_foreign_ownership_never_widens_existing_group(self):
        networks.ensure(self.state, self.aws, self.state.update)
        changed = self.state | {'network': self.state['network'] | {'tcp_ports': [[1, 65535]]}}
        before = deepcopy(self.group)
        with self.assertRaisesRegex(ValueError, 'ownership or policy'):
            networks.ensure(changed, self.aws, changed.update)
        self.assertEqual(self.group, before)
        with self.assertRaisesRegex(ValueError, 'ownership or policy'):
            networks.ensure(self.state | {'network': self.state['network'] | {'scope': 'other'}}, self.aws, self.state.update)

    def test_drifted_ingress_is_rejected_even_when_tags_match(self):
        networks.ensure(self.state, self.aws, self.state.update)
        self.group['IpPermissions'][0]['IpRanges'] = [{'CidrIp': '0.0.0.0/0'}]
        with self.assertRaisesRegex(ValueError, 'ingress differs'):
            networks.ensure(self.state, self.aws, self.state.update)
        self.assertEqual(self.group['IpPermissions'][0]['IpRanges'], [{'CidrIp': '0.0.0.0/0'}])

    def test_busy_scope_or_eni_dependency_preserves_policy_and_reports_pending(self):
        networks.ensure(self.state, self.aws, self.state.update)
        before = deepcopy(self.group)
        self.busy = True
        self.assertFalse(networks.remove(self.state, self.aws, self.state.update))
        self.assertEqual(self.group, before)
        self.assertFalse(self.state['network_removed'])
        self.busy, self.dependency = False, True
        with patch.object(networks.time, 'sleep'):
            self.assertFalse(networks.remove(self.state, self.aws, self.state.update))
        self.assertEqual(self.group, before)
        self.dependency = False
        self.assertTrue(networks.remove(self.state, self.aws, self.state.update))
        self.assertIsNone(self.group)
        self.assertTrue(networks.remove(self.state, self.aws, self.state.update))

    def test_cleanup_waits_for_dispatch_in_another_checkout_on_same_controller(self):
        networks.ensure(self.state, self.aws, self.state.update)
        started, removed = threading.Event(), threading.Event()
        def cleanup():
            started.set()
            with networks.lock(self.state | {'id': 'second-checkout'}):
                networks.remove(self.state, self.aws, self.state.update)
                removed.set()
        with tempfile.TemporaryDirectory() as temp, patch.object(Path, 'home', return_value=Path(temp)):
            with networks.lock(self.state):
                thread = threading.Thread(target=cleanup)
                thread.start()
                self.assertTrue(started.wait(2))
                self.assertFalse(removed.wait(.02))
                self.assertIsNotNone(self.group)
            thread.join(2)
            self.assertFalse(thread.is_alive())
            self.assertTrue(removed.is_set())

    def test_scope_identity_is_shared_across_groups_but_separates_vpc_and_region(self):
        first = networks.identity(self.state)
        self.assertEqual(first, networks.identity(self.state | {'id': 'two'}))
        self.assertNotEqual(first, networks.identity(self.state | {'region': 'us-west-2'}))
        self.assertNotEqual(first, networks.identity(self.state | {'network': self.state['network'] | {'vpc_id': 'other'}}))


if __name__ == '__main__':
    unittest.main()
