#!/usr/bin/env python3
"""Shared payload/queue experiment. Time and service costs are synthetic, not benchmarks."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
import hashlib
import heapq
import json
import math
from pathlib import Path

from arbitration import Arb, Claim
from certification import Store
from comparison_inputs import cases
from priority_locks import PriorityLocks
from write_admission import Admission

ROOT = Path(__file__).resolve().parent
POLICIES = ('arbitration', 'wound-wait', 'wait-die', 'occ', 'certification',
            'snapshot-wait', 'ordered-writes', 'fixed-position-control')


def source_identity():
    files = ['comparison.py', 'comparison_inputs.py', 'certification.py',
             'arbitration.py', 'priority_locks.py', 'write_admission.py', 'batch.py', 'model.py']
    return {f: hashlib.sha256((ROOT / f).read_bytes()).hexdigest() for f in files}


def shard(key):
    return key.split('/')[0]


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def evaluate(spec, inputs):
    keys = spec['writes']
    if spec['op'] == 'report':
        return {}
    if spec['op'] == 'blind':
        return {k: spec.get('value', 1) for k in keys}
    if spec['op'] == 'increment':
        return {k: inputs[k] + 1 for k in keys}
    values = [v for k, v in inputs.items() if v is not None and k not in keys]
    value = max(values, default=0) if spec['op'] == 'max' else sum(values)
    return {k: value for k in keys}


@dataclass
class Transaction:
    spec: dict
    generation: int = 0
    attempts: int = 0
    state: str = 'future'
    phase: str = ''
    captured: dict = field(default_factory=dict)
    writes: dict = field(default_factory=dict)
    part: int = 0
    attempt: object = None
    computed: bool = False
    compute_spent: int = 0
    completion: int | None = None
    started: int | None = None
    pending: set = field(default_factory=set)
    serial_position: object = None
    rejections: Counter = field(default_factory=Counter)
    historical_omissions: int = 0
    bound_versions: dict = field(default_factory=dict)
    bound_promises: list = field(default_factory=list)
    aborted_participants: set = field(default_factory=set)
    admission_part: int = 0


class Simulation:
    """Same queue, payload, offered trace and network for all policies.

    Read stages form a chain (one stage per shard); later computation depends on
    every preceding read. This makes the old policy's dependent invalidation
    executable without inventing semantics for arbitrary authored DAGs.
    """
    def __init__(self, case, policy):
        assert policy in POLICIES
        self.case, self.policy = case, policy
        self.new = policy not in ('arbitration', 'wound-wait', 'wait-die')
        self.time = 0
        self.sequence = 0
        self.events = []
        self.trace = []
        self.counts = Counter()
        self.tx = {s['id']: Transaction(s) for s in case['transactions']}
        self.scopes = {k: tuple(v) for k, v in case['scopes'].items()}
        for key in case['initial']:
            self.scopes.setdefault(key, (key,))
        self.data = dict(case['initial'])
        self.store = Store(self.data, self.scopes)
        self.arb = (PriorityLocks(policy) if policy in ('wound-wait', 'wait-die')
                    else Arb(solver=case.get('solver', 'age')))
        self.admission = Admission({tid: (tx.spec['arrival'], tid) for tid, tx in self.tx.items()})
        self.queues = defaultdict(deque)
        self.servicing = set()
        self.clocks = Counter()
        self.decisions = []
        self.decision_serial = 0
        self.capacity = case.get('capacity', 64)
        self.horizon = case['horizon']
        self.retry_delay = case.get('retry_delay', 3)
        self.collect_period = case.get('collect_period', 5)
        for tx in self.tx.values():
            self.schedule(tx.spec['arrival'], self.start, tx)
        if not self.new:
            self.schedule(self.collect_period, self.collect)

    def schedule(self, when, fn, *args):
        assert when >= self.time
        self.sequence += 1
        heapq.heappush(self.events, (when, self.sequence, fn, args))

    def note(self, event, tx=None, **kw):
        self.trace.append({'t': self.time, 'event': event,
                           **({'id': tx.spec['id'], 'generation': tx.generation} if tx else {}), **kw})

    def link(self, source, destination):
        return 0 if source == destination else self.case.get('link_delay', 5)

    def groups(self, tx):
        groups = defaultdict(list)
        for scope in tx.spec['reads']:
            owners = {shard(k) for k in self.scopes[scope]}
            assert len(owners) == 1, 'each observation scope belongs to one shard'
            groups[next(iter(owners))].append(scope)
        if not self.new and tx.spec.get('early_writes', True):
            # Known write targets belong to the old C1 part even for a blind
            # producer. A late-reservation sensitivity can disable this choice.
            for key in tx.spec['writes']:
                groups[shard(key)]
        coord = tx.spec['coordinator']
        return sorted(groups.items(), key=lambda item: (item[0] != coord, item[0]))

    def output_groups(self, tx):
        result = defaultdict(list)
        for key in tx.spec['writes']:
            result[shard(key)].append(key)
        coord = tx.spec['coordinator']
        return sorted(result.items(), key=lambda item: (item[0] != coord, item[0]))

    def input_keys(self, tx):
        return set().union(*(set(self.scopes[s]) for s in tx.spec['reads'])) if tx.spec['reads'] else set()

    def cost(self, tx):
        return tx.spec.get('cpu', 1 + len(self.input_keys(tx)) + len(tx.spec['writes']))

    def live(self, tx, generation):
        return tx.generation == generation and tx.state not in ('complete', 'failed', 'future')

    def submit(self, owner, tx, fn, *args, cost=1, kind='protocol'):
        # A service unit is an input, not a measured CPU time. Long jobs share
        # each tick with short jobs rather than forming an unpreemptible queue head.
        self.queues[owner].append([tx, tx.generation, fn, args, max(1, cost), kind])
        if owner not in self.servicing:
            self.servicing.add(owner)
            self.schedule(self.time + 1, self.service, owner)

    def service(self, owner):
        queue = self.queues[owner]
        budget = self.capacity
        quantum = max(1, self.capacity // 8)
        skipped = 0
        while queue and budget:
            task = queue.popleft()
            tx, generation, fn, args, remaining, kind = task
            if not self.live(tx, generation) or (tx.state in ('aborting', 'retrying') and kind == 'compute'):
                continue
            if fn == self.local:
                # Cheap denied probes do not execute a scan. A successful short
                # local program fits in this epoch's remaining budget and runs
                # atomically, identically for both policies.
                if remaining > budget:
                    queue.append(task)
                    skipped += 1
                    if skipped >= len(queue):
                        break
                    continue
                fn(tx, *args)
                used = remaining if tx.computed else 1
                budget -= used
                self.counts['service_units'] += used
                self.counts['compute_units' if tx.computed else 'probe_units'] += used
                if tx.computed:
                    tx.compute_spent += used
                    if tx.state == 'aborting':
                        self.counts['discarded_compute_units'] += used
                skipped = 0
                continue
            used = min(remaining, quantum, budget)
            budget -= used
            task[4] -= used
            self.counts['service_units'] += used
            self.counts[kind + '_units'] += used
            if kind == 'compute':
                tx.compute_spent += used
            if task[4]:
                queue.append(task)
            else:
                fn(tx, *args)
            skipped = 0
        if queue:
            self.schedule(self.time + 1, self.service, owner)
        else:
            self.servicing.remove(owner)

    def request(self, tx, destination, fn, *args, cost=1):
        source = tx.spec['coordinator']
        if source != destination:
            self.counts['network_messages'] += 1
        generation = tx.generation
        def arrive():
            if self.live(tx, generation):
                self.submit(destination, tx, fn, destination, *args, cost=cost)
        self.schedule(self.time + self.link(source, destination), arrive)

    def reply(self, tx, source, fn, *args):
        destination = tx.spec['coordinator']
        if source != destination:
            self.counts['network_messages'] += 1
        generation = tx.generation
        def arrive():
            if self.live(tx, generation):
                self.submit(destination, tx, fn, *args)
        self.schedule(self.time + self.link(source, destination), arrive)

    def begin_attempt(self, tx):
        coord = tx.spec['coordinator']
        owner = f"{tx.spec['id']}#{tx.attempts}"
        local = [s for owner, scopes in self.groups(tx) if owner == coord for s in scopes]
        observed = [self.store.W[s][0] for s in local] + list(tx.bound_versions.values())
        floor = max([self.clocks[coord], tx.spec.get('causal_floor', 0)] + observed) + 1
        self.clocks[coord] = floor
        position = (floor, owner)
        # Remote bounds come from delivered replies, not the simulator's global
        # present. New writes between that probe and capture can reject capture.
        if tx.spec.get('allow_old'):
            assert not tx.spec['writes']
            covered = set().union(*(set(self.scopes[s]) for s in local)) if local else set()
            bounds = [p.minimum for promises in self.store.promises.values() for p in promises
                      if covered.intersection(p.remaining)]
            bounds += tx.bound_promises
            if bounds:
                upper = min(bounds)
                candidate = (upper[0] if owner < upper[1] else upper[0] - 1, owner)
                position = min(position, candidate)
            if position < (tx.spec.get('causal_floor', 0), ''):
                return False
        tx.attempt = self.store.begin(owner, position)
        return True

    def start(self, tx):
        tx.attempts += 1
        tx.generation += 1
        tx.state, tx.phase = 'active', 'capture'
        tx.part = 0
        tx.captured, tx.writes = {}, {}
        tx.computed, tx.compute_spent = False, 0
        tx.pending = set()
        tx.attempt = None
        tx.bound_versions, tx.bound_promises = {}, []
        tx.aborted_participants = set()
        tx.admission_part = 0
        tx.started = self.time
        self.note('attempt', tx, attempt=tx.attempts)
        local = all(s == tx.spec['coordinator'] for s, _ in self.groups(tx) + self.output_groups(tx))
        fast = local and tx.spec.get('delay', 0) == 0 and self.cost(tx) <= self.capacity
        if self.policy == 'ordered-writes' and tx.spec['writes'] and not fast:
            tx.phase = 'admission'
            self.next_admission(tx)
        else:
            self.begin_evaluation(tx, fast)

    def begin_evaluation(self, tx, fast=False):
        tx.phase = 'capture'
        if fast:
            # All reads/effects of short local work occur in one logical step;
            # neither policy is forced to read the epoch-start image.
            self.submit(tx.spec['coordinator'], tx, self.local, cost=self.cost(tx), kind='compute')
        elif self.new and any(s != tx.spec['coordinator'] for s, _ in self.groups(tx)):
            tx.pending = {s for s, _ in self.groups(tx)}
            for owner in sorted(tx.pending):
                self.request(tx, owner, self.bound_request)
        else:
            if self.new and not self.begin_attempt(tx):
                self.abort(tx, 'no_admissible_snapshot')
            else:
                self.next_read(tx)

    def next_admission(self, tx):
        groups = sorted(self.output_groups(tx))
        if tx.admission_part == len(groups):
            self.begin_evaluation(tx)
            return
        owner, keys = groups[tx.admission_part]
        self.request(tx, owner, self.admit, tuple(keys), tx.admission_part, cost=1 + len(keys))

    def admit(self, tx, owner, keys, part):
        if tx.state != 'active' or tx.phase != 'admission' or tx.admission_part != part:
            return
        if not self.admission.request(tx.spec['id'], keys):
            self.counts['admission_wait_probes'] += 1
            generation = tx.generation
            def poll():
                if self.live(tx, generation) and tx.state == 'active':
                    self.submit(owner, tx, self.admit, owner, keys, part)
            self.schedule(self.time + self.retry_delay, poll)
            return
        self.note('write_admission', tx, owner=owner, keys=len(keys))
        self.reply(tx, owner, self.admitted, part)

    def admitted(self, tx, part):
        if tx.state == 'active' and tx.phase == 'admission' and tx.admission_part == part:
            tx.admission_part += 1
            self.next_admission(tx)

    def bound_request(self, tx, owner):
        scopes = next(scopes for node, scopes in self.groups(tx) if node == owner)
        covered = set().union(*(set(self.scopes[s]) for s in scopes))
        version = max(self.store.W[s][0] for s in scopes)
        promises = [p.minimum for groups in self.store.promises.values() for p in groups
                    if covered.intersection(p.remaining)]
        self.reply(tx, owner, self.bound_reply, owner, version, promises)

    def bound_reply(self, tx, owner, version, promises):
        if tx.phase != 'capture':
            return
        tx.pending.discard(owner)
        tx.bound_versions[owner] = version
        tx.bound_promises.extend(promises)
        if not tx.pending:
            if not self.begin_attempt(tx):
                self.abort(tx, 'no_admissible_snapshot')
            else:
                self.next_read(tx)

    def local(self, tx):
        if tx.state != 'active':
            return
        if self.new:
            if self.policy == 'ordered-writes' and tx.spec['writes']:
                if not self.admission.request(tx.spec['id'], tuple(tx.spec['writes'])):
                    self.counts['admission_wait_probes'] += 1
                    self.schedule(self.time + self.retry_delay, self.retry_local, tx, tx.generation)
                    return
            if tx.attempt is None and not self.begin_attempt(tx):
                self.abort(tx, 'no_admissible_snapshot')
                return
            for scope in tx.spec['reads']:
                if scope in tx.captured:
                    continue
                if not self.store.capture(tx.attempt, scope, wait=self.policy in ('snapshot-wait', 'ordered-writes')):
                    if self.policy in ('snapshot-wait', 'ordered-writes') and not tx.attempt.rejected:
                        self.capture_wait(tx, tx.spec['coordinator'])
                    else:
                        self.abort(tx, tx.attempt.rejection_reason)
                    return
                tx.captured[scope] = dict(tx.attempt.reads[scope])
            self.compute_values(tx)
            if tx.writes and not self.store.promise(tx.attempt, tuple(tx.writes)):
                self.abort(tx, tx.attempt.rejection_reason)
                return
            self.choose_position(tx)
            self.clocks[tx.spec['coordinator']] = max(self.clocks[tx.spec['coordinator']], tx.attempt.c[0])
            if self.policy == 'fixed-position-control' and tx.attempt.c != tx.attempt.s:
                self.abort(tx, 'fixed_position')
                return
            if tx.attempt.c != tx.attempt.s:
                for scope in tx.spec['reads']:
                    if not self.store.renew(tx.attempt, scope):
                        self.abort(tx, tx.attempt.rejection_reason)
                        return
            self.store.decide(tx.attempt, True)
            self.record_decision(tx, tx.attempt.c)
            self.store.install(tx.attempt, tuple(tx.writes))
            if self.policy == 'ordered-writes':
                self.admission.release_all(tx.spec['id'])
            self.complete(tx)
        else:
            claim = Claim(tx.spec['id'], 0, frozenset(self.input_keys(tx)),
                          frozenset(tx.spec['writes']), tx.generation)
            if not self.arb.request(claim):
                self.counts['protection_retries'] += 1
                self.schedule(self.time + self.retry_delay, self.retry_local, tx, tx.generation)
                return
            tx.captured = {s: {k: self.data[k] for k in self.scopes[s]} for s in tx.spec['reads']}
            self.compute_values(tx)
            self.decision_serial += 1
            self.record_decision(tx, self.decision_serial)
            self.data.update(tx.writes)
            self.arb.release(tx.spec['id'])
            self.complete(tx)

    def retry_local(self, tx, generation):
        if self.live(tx, generation) and tx.state == 'active':
            self.submit(tx.spec['coordinator'], tx, self.local, cost=self.cost(tx), kind='compute')

    def capture_wait(self, tx, owner, scopes=None, part=None):
        if tx.state != 'active':
            return
        self.counts['capture_wait_probes'] += 1
        if self.time - tx.started >= self.case.get('read_wait_ticks', 200):
            self.reply(tx, owner, self.abort, 'read_wait_timeout')
            return
        generation = tx.generation
        if scopes is None:
            self.schedule(self.time + self.retry_delay, self.retry_local, tx, generation)
        else:
            def poll():
                if self.live(tx, generation) and tx.state == 'active':
                    self.submit(owner, tx, self.capture, owner, scopes, part)
            self.schedule(self.time + self.retry_delay, poll)

    def next_read(self, tx):
        if tx.state != 'active':
            return
        groups = self.groups(tx)
        if tx.part == len(groups):
            self.submit(tx.spec['coordinator'], tx, self.compute,
                        cost=self.cost(tx), kind='compute')
            return
        owner, scopes = groups[tx.part]
        self.request(tx, owner, self.capture, tuple(scopes), tx.part)

    def capture(self, tx, owner, scopes, part):
        inflight_abort = self.new and tx.state == 'aborting' and owner not in tx.aborted_participants
        if not inflight_abort and (tx.state != 'active' or tx.phase != 'capture' or tx.part != part):
            return
        if self.new:
            self.clocks[owner] = max(self.clocks[owner], tx.attempt.s[0])
            for scope in scopes:
                if scope in tx.captured:
                    continue
                if not self.store.capture(tx.attempt, scope, in_flight=True,
                                          wait=self.policy in ('snapshot-wait', 'ordered-writes')):
                    if self.policy in ('snapshot-wait', 'ordered-writes') and not tx.attempt.rejected:
                        self.capture_wait(tx, owner, scopes, part)
                    else:
                        self.reply(tx, owner, self.abort, tx.attempt.rejection_reason)
                    return
                tx.captured[scope] = dict(tx.attempt.reads[scope])
        else:
            keys = set().union(*(set(self.scopes[s]) for s in scopes))
            writes = {k for k in tx.spec['writes'] if shard(k) == owner} if tx.spec.get('early_writes', True) else set()
            if not self.arb.request(Claim(tx.spec['id'], part, frozenset(keys), frozenset(writes), tx.generation)):
                self.counts['protection_retries'] += 1
                self.schedule(self.time + self.retry_delay, self.retry_read, tx, tx.generation)
                return
            for scope in scopes:
                tx.captured[scope] = {k: self.data[k] for k in self.scopes[scope]}
        self.note('capture', tx, scopes=scopes)
        self.reply(tx, owner, self.read_reply, part)

    def retry_read(self, tx, generation):
        if self.live(tx, generation) and tx.phase == 'capture' and tx.state == 'active':
            self.next_read(tx)

    def read_reply(self, tx, part):
        if tx.state == 'active' and tx.phase == 'capture' and tx.part == part:
            tx.part += 1
            self.next_read(tx)

    def compute_values(self, tx):
        inputs = {k: v for values in tx.captured.values() for k, v in values.items()}
        tx.writes = evaluate(tx.spec, inputs)
        tx.computed = True
        if self.new:
            tx.attempt.writes.update(tx.writes)

    def compute(self, tx):
        if tx.state != 'active' or tx.phase != 'capture':
            return
        self.compute_values(tx)
        tx.phase = 'compute_delay'
        generation = tx.generation
        def finish():
            if self.live(tx, generation) and tx.state == 'active':
                self.prepare(tx)
        self.schedule(self.time + tx.spec.get('delay', 0), finish)

    def prepare(self, tx):
        tx.phase = 'prepare'
        outputs = self.output_groups(tx)
        if not outputs:
            self.choose(tx)
            return
        tx.pending = {s for s, _ in outputs}
        for i, (owner, keys) in enumerate(outputs):
            if not self.new:
                held = set().union(*(set(c.writes) for c in self.arb.grants.values()
                                     if c.owner == tx.spec['id'])) if self.arb.grants else set()
                if set(keys) <= held:
                    self.prepared(tx, owner)
                    continue
            self.request(tx, owner, self.promise, tuple(keys), len(self.groups(tx)) + i,
                         cost=1 + len(keys))

    def promise(self, tx, owner, keys, part):
        inflight_abort = self.new and tx.state == 'aborting' and owner not in tx.aborted_participants
        if not inflight_abort and (tx.state != 'active' or tx.phase != 'prepare'):
            return
        if self.new:
            if not self.store.promise(tx.attempt, keys, in_flight=True):
                self.reply(tx, owner, self.abort, tx.attempt.rejection_reason)
                return
            self.clocks[owner] = max(self.clocks[owner], self.store.claims[keys[0]].minimum[0])
        elif not self.arb.request(Claim(tx.spec['id'], part, frozenset(), frozenset(keys), tx.generation)):
            self.counts['protection_retries'] += 1
            generation = tx.generation
            def retry():
                if self.live(tx, generation) and tx.state == 'active' and tx.phase == 'prepare':
                    self.request(tx, owner, self.promise, keys, part, cost=1 + len(keys))
            self.schedule(self.time + self.retry_delay, retry)
            return
        self.note('promise', tx, owner=owner, keys=len(keys))
        self.reply(tx, owner, self.prepared, owner)

    def prepared(self, tx, owner):
        if tx.state != 'active' or tx.phase != 'prepare':
            return
        tx.pending.discard(owner)
        if not tx.pending:
            self.choose(tx)

    def choose(self, tx):
        tx.phase = 'renew'
        if self.new:
            self.choose_position(tx)
            self.clocks[tx.spec['coordinator']] = max(self.clocks[tx.spec['coordinator']], tx.attempt.c[0])
            if self.policy == 'fixed-position-control' and tx.attempt.c != tx.attempt.s:
                self.abort(tx, 'fixed_position')
                return
            if tx.attempt.c != tx.attempt.s and tx.spec['reads']:
                self.counts['promotions'] += 1
                tx.pending = {s for s, _ in self.groups(tx)}
                for owner, scopes in self.groups(tx):
                    self.request(tx, owner, self.renew, tuple(scopes), cost=len(scopes))
                return
        self.submit(tx.spec['coordinator'], tx, self.commit)

    def choose_position(self, tx):
        minimum = ((tx.attempt.s[0] + 1, tx.attempt.owner)
                   if self.policy == 'occ' and tx.writes else None)
        self.store.choose(tx.attempt, minimum=minimum)

    def renew(self, tx, owner, scopes):
        inflight_abort = tx.state == 'aborting' and owner not in tx.aborted_participants
        if not inflight_abort and (tx.state != 'active' or tx.phase != 'renew'):
            return
        for scope in scopes:
            self.clocks[owner] = max(self.clocks[owner], tx.attempt.c[0])
            self.counts['renewal_scopes'] += 1
            if not self.store.renew(tx.attempt, scope, in_flight=True):
                self.reply(tx, owner, self.abort, tx.attempt.rejection_reason)
                return
        self.reply(tx, owner, self.renewed, owner)

    def renewed(self, tx, owner):
        if tx.state == 'active' and tx.phase == 'renew':
            tx.pending.discard(owner)
            if not tx.pending:
                self.submit(tx.spec['coordinator'], tx, self.commit)

    def record_decision(self, tx, position):
        tx.serial_position = position
        tx.state, tx.phase = 'committed', 'install'
        self.decisions.append({'id': tx.spec['id'], 'position': position,
                               'reads': {s: dict(v) for s, v in tx.captured.items()},
                               'writes': dict(tx.writes), 'time': self.time})
        self.counts['commits'] += 1
        self.note('commit', tx, position=position)
        if self.new and not tx.writes:
            tx.historical_omissions = sum(self.store.versions[k][-1].position > tx.attempt.s
                                         for k in self.input_keys(tx))

    def commit(self, tx):
        if tx.state != 'active' or tx.phase != 'renew':
            return
        if self.new:
            self.store.decide(tx.attempt, True)
            position = tx.attempt.c
        else:
            self.decision_serial += 1
            position = self.decision_serial
        self.record_decision(tx, position)
        # Old read participants also receive the outcome to release protection.
        owners = {s for s, _ in self.output_groups(tx)}
        if not self.new:
            owners |= {s for s, _ in self.groups(tx)}
        tx.pending = owners
        if not owners:
            self.complete(tx)
        for owner in sorted(owners):
            keys = tuple(k for k in tx.writes if shard(k) == owner)
            self.request(tx, owner, self.install, keys, cost=1 + len(keys))

    def install(self, tx, owner, keys):
        if tx.state != 'committed':
            return
        if self.new:
            self.store.install(tx.attempt, keys)
            self.clocks[owner] = max(self.clocks[owner], tx.attempt.c[0])
            if self.policy == 'ordered-writes':
                self.admission.release(tx.spec['id'], keys)
        else:
            self.data.update({k: tx.writes[k] for k in keys})
            for ref, claim in list(self.arb.grants.items()):
                if claim.owner == tx.spec['id'] and any(shard(k) == owner for k in claim.reads | claim.writes):
                    del self.arb.grants[ref]
        self.note('install', tx, owner=owner)
        self.reply(tx, owner, self.installed, owner)

    def installed(self, tx, owner):
        if tx.state == 'committed':
            tx.pending.discard(owner)
            if not tx.pending:
                if not self.new:
                    self.arb.release(tx.spec['id'])
                self.complete(tx)

    def complete(self, tx):
        tx.state, tx.phase = 'complete', ''
        tx.completion = self.time
        self.note('complete', tx, latency=self.time - tx.spec['arrival'])

    def abort(self, tx, reason):
        if tx.state != 'active':
            return
        tx.state, tx.phase = 'aborting', 'abort'
        tx.rejections[str(reason)] += 1
        self.counts['aborts'] += 1
        self.counts['discarded_compute_units'] += tx.compute_spent
        self.note('abort', tx, reason=reason)
        if tx.attempt:
            self.store.decide(tx.attempt, False)
        owners = {s for s, _ in self.output_groups(tx)} or {tx.spec['coordinator']}
        tx.pending = owners
        for owner in sorted(owners):
            keys = tuple(k for k in tx.spec['writes'] if shard(k) == owner)
            source, generation = tx.spec['coordinator'], tx.generation
            if source != owner:
                self.counts['network_messages'] += 1
            def arrive(owner=owner, keys=keys, generation=generation):
                if self.live(tx, generation):
                    self.submit(owner, tx, self.release_abort, owner, keys)
            self.schedule(self.time + self.link(source, owner), arrive)

    def release_abort(self, tx, owner, keys):
        if tx.state != 'aborting':
            return
        tx.aborted_participants.add(owner)
        if self.policy == 'ordered-writes':
            self.admission.release(tx.spec['id'], keys)
        if tx.attempt:
            self.store.release(tx.attempt, tuple(k for k in keys if k in tx.attempt.writes))
        self.reply(tx, owner, self.aborted, owner)

    def aborted(self, tx, owner):
        if tx.state != 'aborting':
            return
        tx.pending.discard(owner)
        if not tx.pending:
            if tx.attempts >= self.case.get('retry_limit', 4):
                tx.state, tx.phase = 'failed', ''
                tx.completion = self.time
                self.note('failed', tx)
            else:
                tx.state = 'retrying'
                self.schedule(self.time + self.retry_delay, self.start, tx)

    def collect(self):
        priority = {tid: (tx.spec['arrival'], tid) for tid, tx in self.tx.items()}
        irrevocable = {tid for tid, tx in self.tx.items() if tx.state in ('committed', 'complete')}
        plans = self.arb.collect(priority, irrevocable,
                                 weights={tid: tx.compute_spent for tid, tx in self.tx.items()})
        for plan in plans:
            self.note('component', members=sorted(plan['members']), keys=len(plan['keys']))
            local = len(plan['shards']) == 1 and all(self.tx[t].spec['coordinator'] in plan['shards'] for t in plan['members'])
            delay = 0 if local else self.case.get('arbitration_delay', 10)
            # An explicit sensitivity can charge graph processing. Default zero
            # favors the earlier model's ideal complete collector.
            extra = self.case.get('arbitration_edge_ticks', 0) * plan.get('edges', 0)
            self.schedule(self.time + delay + extra, self.verdict, plan)
        self.schedule(self.time + self.collect_period, self.collect)

    def verdict(self, plan):
        irrevocable = {tid for tid, tx in self.tx.items() if tx.state in ('committed', 'complete')}
        victims = self.arb.verdict(plan, irrevocable=irrevocable)
        if victims is None:
            return
        for tid, part in victims:
            tx = self.tx[tid]
            assert tx.state == 'active'
            self.arb.release(tid, from_part=part)
            tx.generation += 1
            self.counts['yielded_chains'] += 1
            self.note('yield', tx, part=part)
            groups = self.groups(tx)
            if self.policy in ('wound-wait', 'wait-die'):
                self.counts['discarded_compute_units'] += tx.compute_spent
                tx.compute_spent = 0
                tx.captured, tx.writes = {}, {}
                tx.computed, tx.part, tx.phase = False, 0, 'capture'
                generation = tx.generation
                def restart(tx=tx, generation=generation):
                    if self.live(tx, generation) and tx.state == 'active':
                        self.start(tx)
                self.schedule(self.time + self.retry_delay, restart)
                continue
            generation = tx.generation
            def resume(tx=tx, generation=generation, part=part):
                if self.live(tx, generation) and tx.state == 'active':
                    if part < len(self.groups(tx)):
                        self.next_read(tx)
                    else:
                        self.prepare(tx)
            if part < len(groups):
                for _, scopes in groups[part:]:
                    for scope in scopes:
                        tx.captured.pop(scope, None)
                self.counts['discarded_compute_units'] += tx.compute_spent
                tx.compute_spent = 0
                tx.writes = {}
                tx.computed = False
                tx.part, tx.phase = part, 'capture'
                self.schedule(self.time + self.retry_delay, resume)
            else:
                # A yielded output part can retain its inputs and computation.
                self.schedule(self.time + self.retry_delay, resume)

    def occupancy(self, delta):
        if self.new:
            keys = set(self.store.claims)
            reads = set()
            fences = set()
        else:
            keys = set().union(*(set(c.writes) for c in self.arb.grants.values())) if self.arb.grants else set()
            reads = set().union(*(set(c.reads) for c in self.arb.grants.values())) if self.arb.grants else set()
            fences = set().union(*(set(f['scope'][0] | f['scope'][1]) for f in self.arb.fences.values())) if self.arb.fences else set()
        for name, covered in [('write', keys), ('read', reads), ('fence', fences),
                              ('admission', set(self.admission.grants))]:
            self.counts[name + '_key_ticks'] += delta * len(covered)
            self.counts['peak_' + name + '_keys'] = max(self.counts['peak_' + name + '_keys'], len(covered))

    def check_serial(self):
        state = dict(self.case['initial'])
        for record in sorted(self.decisions, key=lambda r: r['position']):
            assert set(record['reads']) == set(self.tx[record['id']].spec['reads']), record['id']
            assert set(record['writes']) == set(self.tx[record['id']].spec['writes']), record['id']
            for scope, observed in record['reads'].items():
                expected = {k: state[k] for k in self.scopes[scope]}
                assert observed == expected, (self.case['name'], self.policy, record['id'], scope, observed, expected)
            inputs = {k: v for values in record['reads'].values() for k, v in values.items()}
            assert evaluate(self.tx[record['id']].spec, inputs) == record['writes']
            state.update(record['writes'])
        # A commit may still be installing at the horizon. Those keys remain
        # protected/promised; check the visible, unpromised portion exactly.
        if self.new:
            pending = {k for k, p in self.store.claims.items()
                       if self.store.attempts[p.owner].decision == 'commit'}
        else:
            pending = {k for claim in self.arb.grants.values()
                       if self.tx[claim.owner].state == 'committed' for k in claim.writes}
        actual = ({k: self.store.versions[k][-1].value for k in state} if self.new else self.data)
        assert all(actual[k] == state[k] for k in state if k not in pending)
        return digest(state)

    def run(self):
        while self.events:
            when, _, fn, args = heapq.heappop(self.events)
            if when > self.horizon:
                self.occupancy(self.horizon - self.time)
                self.time = self.horizon
                break
            self.occupancy(when - self.time)
            self.time = when
            fn(*args)
            if all(tx.state in ('complete', 'failed') for tx in self.tx.values()):
                break
        final_hash = self.check_serial()
        cohorts = {}
        for group in sorted({tx.spec['group'] for tx in self.tx.values()}):
            members = [tx for tx in self.tx.values() if tx.spec['group'] == group and tx.spec['arrival'] <= self.time]
            completed = [tx for tx in members if tx.state == 'complete']
            latencies = sorted(tx.completion - tx.spec['arrival'] for tx in completed)
            pending = [tx for tx in members if tx.state not in ('complete', 'failed')]
            reasons = Counter()
            for tx in members:
                reasons.update(tx.rejections)
            cohorts[group] = {'offered': len(members), 'complete': len(completed),
                              'failed': sum(tx.state == 'failed' for tx in members), 'pending': len(pending),
                              'attempts': sum(tx.attempts for tx in members),
                              'p50_ticks': latencies[(len(latencies)-1)//2] if latencies else None,
                              'p99_ticks': latencies[math.ceil(.99 * len(latencies))-1] if latencies else None,
                              'oldest_pending_ticks': max((self.time-tx.spec['arrival'] for tx in pending), default=0),
                              'historical_omitted_keys': sum(tx.historical_omissions for tx in completed),
                              'rejections': dict(reasons)}
        return {'case': self.case['name'], 'policy': self.policy, 'time_ticks': self.time,
                'cohorts': cohorts, 'counts': dict(self.counts),
                'arbitration': dict(self.arb.counts) if not self.new else {},
                'certification': dict(self.store.counters) if self.new else {},
                'write_admission': dict(self.admission.counters) if self.policy == 'ordered-writes' else {},
                'retained_versions_no_gc': sum(map(len, self.store.versions.values())) if self.new else None,
                'serial_check': True, 'logical_state_sha256': final_hash,
                'trace_sha256': digest(self.trace), 'input_sha256': digest(self.case)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seeds', default='7')
    parser.add_argument('--widths', default='64')
    parser.add_argument('--case', default='')
    parser.add_argument('--policies', default=','.join(POLICIES[:-1]))
    parser.add_argument('--set', action='append', default=[], metavar='NAME=VALUE',
                        help='override a numeric case parameter; recorded in evidence')
    parser.add_argument('--reserve-writes', choices=('early', 'late'))
    parser.add_argument('--solver', choices=('age', 'work', 'bounded'))
    parser.add_argument('--quiet', action='store_true')
    parser.add_argument('--full', action='store_true')
    args = parser.parse_args()
    identity_before = source_identity()
    overrides = {}
    allowed = {'capacity', 'link_delay', 'arbitration_delay', 'retry_delay', 'retry_limit',
               'collect_period', 'read_wait_ticks', 'horizon', 'arbitration_edge_ticks'}
    for item in args.set:
        name, value = item.split('=', 1)
        positive = {'capacity', 'retry_limit', 'collect_period', 'horizon'}
        if name not in allowed or int(value) < (1 if name in positive else 0):
            parser.error(f'unsupported override: {item}')
        overrides[name] = int(value)
    if any(p not in POLICIES for p in args.policies.split(',')):
        parser.error('unknown policy')
    rows = []
    for seed in map(int, args.seeds.split(',')):
        for width in map(int, args.widths.split(',')):
            for case in cases(seed=seed, width=width):
                if args.case and args.case not in case['name']:
                    continue
                case.update(overrides)
                if args.solver:
                    case['solver'] = args.solver
                if args.reserve_writes:
                    for spec in case['transactions']:
                        spec['early_writes'] = args.reserve_writes == 'early'
                for policy in args.policies.split(','):
                    simulation = Simulation(case, policy)
                    row = simulation.run()
                    row.update(seed=seed, width=width)
                    if args.full:
                        row.update(input=case, trace=simulation.trace)
                    rows.append(row)
                    if not args.quiet:
                        print(f"{case['name']} {policy}: " + ', '.join(
                            f"{g} {v['complete']}/{v['offered']} done, {v['failed']} failed, {v['pending']} pending"
                            for g,v in row['cohorts'].items()), flush=True)
    assert identity_before == source_identity(), 'sources changed during run; discard and rerun'
    result = {'source_sha256': identity_before,
              'parameters': {'seeds': args.seeds, 'widths': args.widths, 'case_filter': args.case,
                             'policies': args.policies, 'overrides': overrides,
                             'reserve_writes': args.reserve_writes, 'solver': args.solver},
              'assumptions': 'Synthetic ticks and queue work units; authored scopes and linear read dependencies; failure-free transport; ideal complete arbitration collection; no measured latency/throughput.',
              'rows': rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'Wrote {len(rows)} comparisons to {args.output}')


if __name__ == '__main__':
    main()
