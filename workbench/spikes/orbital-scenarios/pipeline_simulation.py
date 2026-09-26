#!/usr/bin/env python3
"""Release writer admission after exact-position publication, before computation.

The frozen scheduler still charges allocation, position exchanges, final-value
staging and installation. A separate delivered release request is charged.
Pending versions remain per owner; this is no permission to read missing values.
SQL membership/constraint semantics are examined by separate program probes.
"""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from certification import Promise, Version, ZERO, next_owned
from comparison import digest
from comparison_inputs import cases
from envelope_locality import workload, EnvelopeSimulation
from fixed_execution import FixedStore
from fixed_simulation import FixedSimulation, evaluate_program, source_identity


class PipelineStore(FixedStore):
    def __init__(self, initial, scopes):
        super().__init__(initial, scopes)
        self.pending_versions = {key: {} for key in initial}
        self.allocation_heads = dict.fromkeys(initial, ZERO)

    def reserve_position(self, attempt, keys, *, in_flight=False):
        keys = tuple(sorted(set(keys)))
        if attempt.decision == 'commit':
            assert in_flight and keys in self.reservation_bounds[attempt.owner]
            return True
        if not self._active(attempt, in_flight):
            return False
        if keys in self.reservation_bounds[attempt.owner]:
            return True
        assert not attempt.observations and not attempt.writes and attempt.c is None
        assert set(keys) <= self.coverage[attempt.owner]
        # Admission cannot be released while a prior position is provisional.
        for key in keys:
            for owner in self.pending_versions[key]:
                assert self.published[owner] == self.coverage[owner]
        scopes = {scope for key in keys for scope in self.key_scopes[key]}
        strict = [self.versions[key][-1].position for key in keys]
        strict += [self.allocation_heads[key] for key in keys]
        strict += [self.R[scope] for scope in scopes]
        minimum = next_owned(attempt.s[0], attempt.owner, strict_after=strict)
        claim = Promise(attempt.owner, keys, minimum, set(keys))
        self.promises[attempt.owner].append(claim)
        self.reservation_bounds[attempt.owner][keys] = minimum
        for key in keys:
            self.pending_versions[key][attempt.owner] = claim
            self.claims[key] = claim  # Compatibility view for occupancy only.
        self._count(attempt, 'position_groups_reserved')
        self._event(attempt, 'reserve_position', keys=keys, minimum=minimum)
        return True

    def publish_position(self, attempt, keys, *, in_flight=False):
        result = super().publish_position(attempt, keys, in_flight=in_flight)
        if result:
            for key in keys:
                self.allocation_heads[key] = max(self.allocation_heads[key], attempt.c)
        return result

    def _blocking(self, attempt, scope, position):
        blockers = set()
        for key in self.scopes[scope]:
            visible = max(v.position for v in self.versions[key] if v.position <= position)
            pending = list(self.pending_versions[key].values())
            if key in self.claims and self.claims[key].owner not in self.pending_versions[key]:
                pending.append(self.claims[key])
            for claim in pending:
                if claim.owner != attempt.owner and visible < claim.minimum <= position:
                    blockers.add(claim.owner)
        return sorted(blockers)

    def decide(self, attempt, commit):
        if attempt.owner not in self.coverage:
            return super().decide(attempt, commit)
        decision = 'commit' if commit else 'abort'
        if attempt.decision is not None:
            assert attempt.decision == decision
            self._count(attempt, 'duplicate_decisions')
            return
        if commit:
            assert attempt.owner in self.sealed and not attempt.rejected
            assert self.published[attempt.owner] == self.coverage[attempt.owner]
            assert all(attempt.owner in self.pending_versions[key] for key in attempt.writes)
            assert attempt.c == attempt.s
        attempt.decision = decision
        self._count(attempt, 'commit_decisions' if commit else 'abort_decisions')
        self._event(attempt, 'decide', decision=decision, c=attempt.c)

    def remove_pending(self, attempt, key):
        claim = self.pending_versions[key].pop(attempt.owner)
        claim.remaining.remove(key)
        if self.pending_versions[key]:
            self.claims[key] = max(self.pending_versions[key].values(), key=lambda item: item.minimum)
        else:
            self.claims.pop(key, None)

    def install(self, attempt, keys):
        if attempt.owner not in self.coverage:
            return super().install(attempt, keys)
        assert attempt.decision == 'commit' and attempt.c is not None
        self._manifest(attempt)
        keys = set(keys)
        assert keys <= self.coverage[attempt.owner]
        fresh = keys - self.resolved[attempt.owner]
        assert all(attempt.owner in self.pending_versions[key] for key in fresh)
        for key in sorted(fresh):
            if key in attempt.writes:
                assert all(v.position != attempt.c for v in self.versions[key])
                self.versions[key].append(Version(attempt.c, attempt.writes[key]))
                self.versions[key].sort(key=lambda version: version.position)
                for scope in self.key_scopes[key]:
                    self.W[scope] = max(self.W[scope], attempt.c)
                attempt.installed.add(key)
            self.remove_pending(attempt, key)
        self.resolved[attempt.owner].update(keys)
        self._count(attempt, 'installed_keys', len(fresh & attempt.writes.keys()))
        self._count(attempt, 'unused_outputs_resolved', len(fresh - attempt.writes.keys()))
        self._event(attempt, 'resolve_outputs', keys=sorted(keys), unused=sorted(fresh - attempt.writes.keys()))

    def release(self, attempt, keys):
        if attempt.owner not in self.coverage:
            return super().release(attempt, keys)
        assert attempt.decision == 'abort'
        released = []
        for key in sorted(set(keys)):
            if attempt.owner in self.pending_versions[key]:
                self.remove_pending(attempt, key)
                released.append(key)
        self._count(attempt, 'released_keys', len(released))
        self._event(attempt, 'release', keys=released)


class PipelineSimulation(EnvelopeSimulation):
    def __init__(self, case, release_early=True):
        super().__init__(case)
        self.release_early = release_early
        self.store = PipelineStore(self.data, self.scopes)

    def local(self, tx):
        keys = self.input_keys(tx) | set(tx.spec['writes'])
        if self.release_early and tx.spec['writes'] and any(self.store.pending_versions[key] for key in keys):
            # Preserve the common harness's one-step local execution contract.
            # A pending predecessor does not itself make a blind put remote.
            outputs = tuple(sorted(tx.spec['writes']))
            if not self.admission.request(tx.spec['id'], outputs):
                self.counts['admission_wait_probes'] += 1
                self.schedule(self.time + self.retry_delay, self.retry_local, tx, tx.generation)
                return
            work = self.output_work[tx.spec['id']]
            work.gate_keys = outputs
            work.gate_groups = [(tx.spec['coordinator'], outputs)]
            work.gates_complete, work.refreshing, work.fast = True, True, False
            assert tx.attempt is None
            self.begin_attempt(tx)
            self.store.reserve_position(tx.attempt, outputs)
            self.store.fix_position(tx.attempt)
            self.store.publish_position(tx.attempt, outputs)
            self.clocks[tx.spec['coordinator']] = max(self.clocks[tx.spec['coordinator']], tx.attempt.c[0])
            self.fixed_work[tx.spec['id']].position_started = True
            self.fixed_work[tx.spec['id']].position_ready = True
            self.admission.release_all(tx.spec['id'])
            for scope in tx.spec['reads']:
                if not self.store.capture(tx.attempt, scope, wait=True):
                    tx.phase = 'capture'
                    self.next_read(tx)
                    return
                tx.captured[scope] = dict(tx.attempt.reads[scope])
            self.compute_values(tx)
            self.store.seal_values(tx.attempt)
            self.fixed_work[tx.spec['id']].staged[tx.spec['coordinator']] = dict(tx.writes)
            self.store.decide(tx.attempt, True)
            self.record_decision(tx, tx.attempt.c)
            self.store.install(tx.attempt, outputs)
            self.complete(tx)
            return
        super().local(tx)

    def position_published(self, tx, owner):
        if tx.state != 'active' or tx.phase != 'position_publish':
            return
        tx.pending.discard(owner)
        if tx.pending:
            return
        if self.release_early:
            for node, keys in self.output_work[tx.spec['id']].gate_groups:
                self.request(tx, node, self.release_allocation, keys)
        self.fixed_work[tx.spec['id']].position_ready = True
        tx.phase = 'capture'
        self.note('fixed_capture_ready', tx, position=tx.attempt.s)
        self.next_read(tx)

    def release_allocation(self, tx, owner, keys):
        assert self.store.published[tx.attempt.owner] == self.store.coverage[tx.attempt.owner]
        self.admission.release(tx.spec['id'], keys)
        self.note('allocation_released', tx, owner=owner, keys=list(keys))

    def check_serial(self):
        state = dict(self.case['initial'])
        for record in sorted(self.decisions, key=lambda item: item['position']):
            spec = self.tx[record['id']].spec
            assert set(record['reads']) == set(spec['reads'])
            for scope, observed in record['reads'].items():
                expected = {key: state[key] for key in self.scopes[scope]}
                assert observed == expected, (record['id'], scope, observed, expected)
            inputs = {key: value for values in record['reads'].values() for key, value in values.items()}
            assert record['writes'] == evaluate_program(spec, inputs)
            assert set(record['writes']) <= set(spec['writes'])
            state.update(record['writes'])
        pending = {key for key, claims in self.store.pending_versions.items()
                   if any(self.store.attempts[owner].decision == 'commit' for owner in claims)}
        assert all(self.store.head()[key] == state[key] for key in state if key not in pending)
        return digest(state)

    def occupancy(self, delta):
        super().occupancy(delta)
        sizes = [len(claims) for claims in self.store.pending_versions.values()]
        self.counts['peak_pending_versions'] = max(self.counts['peak_pending_versions'], sum(sizes))
        self.counts['peak_pending_versions_per_key'] = max(self.counts['peak_pending_versions_per_key'], max(sizes, default=0))

    def run(self):
        result = super().run()
        result['policy'] = 'pipeline' if self.release_early else 'pipeline-held-control'
        return result


def identity():
    root = Path(__file__).parent
    return {**source_identity(), **{name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in ('pipeline_simulation.py', 'envelope_locality.py')}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seeds', default='7')
    parser.add_argument('--case', default='')
    parser.add_argument('--read-wait-ticks', type=int)
    parser.add_argument('--horizon', type=int)
    args = parser.parse_args()
    before = identity()
    rows = []
    for seed in map(int, args.seeds.split(',')):
        offered_cases = cases(seed=seed, width=64)
        offered_cases += [workload(mode, 16) for mode in ('exact', 'target', 'shard-negative-control')]
        for case in offered_cases:
            if args.case and args.case not in case['name']:
                continue
            if args.read_wait_ticks is not None:
                case['read_wait_ticks'] = args.read_wait_ticks
            if args.horizon is not None:
                case['horizon'] = args.horizon
            for release in (False, True):
                sim = PipelineSimulation(deepcopy(case), release)
                row = sim.run()
                row.update(seed=seed, input=case)
                if not release:
                    baseline = EnvelopeSimulation(deepcopy(case)).run()
                    assert row['cohorts'] == baseline['cohorts'], case['name']
                    assert row['logical_state_sha256'] == baseline['logical_state_sha256']
                rows.append(row)
                print(case['name'], row['policy'], {name: (c['complete'],c['failed'],c['pending'],c['p99_ticks'])
                    for name,c in row['cohorts'].items()}, flush=True)
    assert before == identity()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'source_sha256':before,'rows':rows,
        'limits':['Synthetic costs and failure-free links; no measured performance.',
                  'Known finite coverage; no distributed index authority or GC implementation.',
                  'Separate delivered allocation-release messages are charged.',
                  'Read/deadline policy retained; pipelining can grow queues and deadline failures.',
                  'Held-control cohort outcomes and final serial state equal the frozen backend.']},
        indent=2,sort_keys=True)+'\n')


if __name__ == '__main__':
    main()
