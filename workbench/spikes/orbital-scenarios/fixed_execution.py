"""Experimental execution at a position fixed before reading database inputs.

This is deliberately a different protocol from BRIEF2, not its fixed-position
ablation. The caller first acquires ALL output-only gates, then reserves local
position bounds, chooses one position, and publishes that exact position at all
outputs. Only then may the program read/compute. Partially acquired gates must
not create these read-blocking promises. Complete output coverage is required;
changed coverage requires release/restart. Values are sealed after computation.

The experiment retains read certificates, atomic outcomes and pending versions.
It removes source renewal by executing at the already fixed commit position.
It does not implement durable recovery, dynamic output coverage, or transport.
"""

from certification import Promise, Store, next_owned


class FixedStore(Store):
    def __init__(self, initial, scopes):
        super().__init__(initial, scopes)
        self.coverage = {}
        self.published = {}
        self.reservation_bounds = {}
        self.sealed = set()
        self.resolved = {}

    def begin_fixed(self, owner, floor, keys, grants, gate_owner):
        keys = frozenset(keys)
        assert keys and keys <= self.versions.keys()
        assert all(grants.get(key) == gate_owner for key in keys), 'all output gates precede bounds'
        attempt = self.begin(owner, floor)
        self.coverage[owner] = keys
        self.published[owner] = set()
        self.reservation_bounds[owner] = {}
        self.resolved[owner] = set()
        return attempt

    def reserve_position(self, attempt, keys, *, in_flight=False):
        keys = tuple(sorted(set(keys)))
        assert keys and set(keys) <= self.coverage[attempt.owner]
        if attempt.decision == 'commit':
            assert in_flight and keys in self.reservation_bounds[attempt.owner]
            self._count(attempt, 'duplicate_position_reservations')
            return True
        if not self._active(attempt, in_flight):
            return False
        for previous in self.promises[attempt.owner]:
            if previous.keys == keys:
                self._count(attempt, 'duplicate_position_reservations')
                return True
        assert not attempt.observations and not attempt.writes
        assert attempt.c is None
        assert not any(key in self.claims for key in keys), 'all writers must honor output gates'
        scopes = {scope for key in keys for scope in self.key_scopes[key]}
        strict = [self.versions[key][-1].position for key in keys]
        strict += [self.R[scope] for scope in scopes]
        minimum = next_owned(attempt.s[0], attempt.owner, strict_after=strict)
        claim = Promise(attempt.owner, keys, minimum, set(keys))
        self.promises[attempt.owner].append(claim)
        self.reservation_bounds[attempt.owner][keys] = minimum
        for key in keys:
            self.claims[key] = claim
        self._count(attempt, 'position_groups_reserved')
        self._event(attempt, 'reserve_position', keys=keys, minimum=minimum)
        return True

    def fix_position(self, attempt):
        assert self._active(attempt)
        assert not attempt.observations and not attempt.writes
        covered = {key for claim in self.promises[attempt.owner] for key in claim.keys}
        assert covered == self.coverage[attempt.owner]
        if attempt.c is None:
            attempt.c = max([attempt.s, *(p.minimum for p in self.promises[attempt.owner])])
            # The former s was only an allocation floor: no input was captured.
            attempt.s = attempt.c
            self._count(attempt, 'fixed_positions_chosen')
            self._event(attempt, 'fix_position', c=attempt.c)
        return attempt.c

    def publish_position(self, attempt, keys, *, in_flight=False):
        keys = tuple(sorted(set(keys)))
        if attempt.decision == 'commit':
            assert in_flight and keys in self.reservation_bounds[attempt.owner]
            assert set(keys) <= self.published[attempt.owner]
            self._count(attempt, 'duplicate_position_publications')
            return True
        if not self._active(attempt, in_flight):
            return False
        assert attempt.c == attempt.s and attempt.c is not None
        claim = next(p for p in self.promises[attempt.owner] if p.keys == keys)
        assert claim.minimum <= attempt.c
        # This only shrinks the blocked interval. No read was allowed at/after c
        # under the temporary bound; it is still forbidden until resolution.
        claim.minimum = attempt.c
        self.published[attempt.owner].update(keys)
        self._count(attempt, 'position_groups_published')
        self._event(attempt, 'publish_position', keys=keys, c=attempt.c)
        return True

    def capture(self, attempt, scope, *, in_flight=False, wait=False):
        if attempt.owner not in self.coverage:
            if wait and self._active(attempt, in_flight):
                assert attempt.manifest is None and attempt.c is None
                self.register_read(attempt, scope)
            return super().capture(attempt, scope, in_flight=in_flight, wait=wait)
        if not self._active(attempt, in_flight):
            return False
        assert self.published[attempt.owner] == self.coverage[attempt.owner]
        assert attempt.c == attempt.s and attempt.owner not in self.sealed
        self.register_read(attempt, scope)
        self._count(attempt, 'capture_calls')
        attempt.last_wait_reason, attempt.wait_blockers = None, []
        blockers = self._blocking(attempt, scope, attempt.s)
        if blockers:
            if not wait:
                return self._reject(attempt, 'capture_promise', scope=scope, blockers=blockers)
            attempt.last_wait_reason, attempt.wait_blockers = 'capture_promise', blockers
            self._count(attempt, 'capture_waits')
            self._event(attempt, 'capture_wait', scope=scope, blockers=blockers)
            return False
        values = self.snapshot(scope, attempt.s)
        overlay = {key: attempt.writes[key] for key in values if key in attempt.writes}
        values.update(overlay)
        attempt.reads[scope] = dict(values)
        attempt.observations.append({'scope': scope, 'position': attempt.s,
                                     'values': dict(values), 'overlay': overlay})
        self.R[scope] = max(self.R[scope], attempt.s)
        self._count(attempt, 'captured_scopes')
        self._count(attempt, 'captured_rows', len(values))
        self._event(attempt, 'capture', scope=scope, values=values)
        return True

    def register_read(self, attempt, scope):
        # Register the intended position before waiting. Already frozen earlier
        # promises may finish; new writers must choose a later position. Without
        # this, a blocked broad read can keep gaining new lower-position writers.
        self.R[scope] = max(self.R[scope], attempt.s)
        self._count(attempt, 'read_positions_registered')
        self._event(attempt, 'register_read', scope=scope, s=attempt.s)

    def seal_values(self, attempt):
        assert self._active(attempt)
        assert self.published[attempt.owner] == self.coverage[attempt.owner]
        assert set(attempt.writes) <= self.coverage[attempt.owner], 'new outputs require a restart'
        self._manifest(attempt)
        self.sealed.add(attempt.owner)
        self._event(attempt, 'seal_values', keys=sorted(attempt.writes))

    def decide(self, attempt, commit):
        if commit and attempt.owner in self.coverage:
            assert attempt.owner in self.sealed
            assert self.published[attempt.owner] == self.coverage[attempt.owner]
        return super().decide(attempt, commit)

    def install(self, attempt, keys):
        if attempt.owner not in self.coverage:
            return super().install(attempt, keys)
        assert attempt.decision == 'commit'
        keys = set(keys)
        assert keys <= self.coverage[attempt.owner]
        super().install(attempt, keys.intersection(attempt.writes))
        unused = keys.difference(attempt.writes, self.resolved[attempt.owner])
        for key in sorted(unused):
            assert self.claims[key].owner == attempt.owner
            assert self.claims[key].minimum == attempt.c
            self.claims[key].remaining.remove(key)
            del self.claims[key]
        self.resolved[attempt.owner].update(keys)
        self._count(attempt, 'unused_outputs_resolved', len(unused))
        self._event(attempt, 'resolve_outputs', keys=sorted(keys), unused=sorted(unused))

    def check_serial(self, require_installed=True):
        state = super().check_serial(require_installed)
        if require_installed:
            for owner, coverage in self.coverage.items():
                if self.attempts[owner].decision == 'commit':
                    assert self.resolved[owner] == coverage, 'unused output promises also need resolution'
        return state

    def release(self, attempt, keys):
        if attempt.owner not in self.coverage:
            return super().release(attempt, keys)
        assert attempt.decision == 'abort'
        keys = tuple(sorted(set(keys)))
        assert set(keys) <= self.coverage[attempt.owner]
        released = []
        for key in keys:
            if key in self.claims and self.claims[key].owner == attempt.owner:
                self.claims[key].remaining.remove(key)
                del self.claims[key]
                released.append(key)
        self._count(attempt, 'released_keys', len(released))
        self._event(attempt, 'release', keys=released)


def probes():
    results = []
    initial = {'eu/a': 1, 'us/b': 1, 'eu/marker': 0}
    scopes = {key: (key,) for key in initial}

    def reserve(store, owner, keys, floor=1, publish=True):
        attempt = store.begin_fixed(owner, (floor, owner), keys, dict.fromkeys(keys, owner), owner)
        for key in keys:
            assert store.reserve_position(attempt, [key])
        store.fix_position(attempt)
        if publish:
            for key in keys:
                assert store.publish_position(attempt, [key])
        return attempt

    def finish(store, attempt, writes):
        attempt.writes.update(writes)
        store.seal_values(attempt)
        store.decide(attempt, True)
        store.install(attempt, tuple(store.coverage[attempt.owner]))

    db = FixedStore(initial, scopes)
    first = reserve(db, 'first', ['eu/a'])
    second = reserve(db, 'second', ['us/b'], floor=2)
    assert db.capture(first, 'us/b', wait=True)
    assert first.reads['us/b']['us/b'] == 1
    assert not db.capture(second, 'eu/a', wait=True)
    finish(db, first, {'eu/a': 0})
    assert db.capture(second, 'eu/a', wait=True)
    # Conditional off-call: retain the second doctor when the first is off.
    finish(db, second, {'us/b': 0} if second.reads['eu/a']['eu/a'] else {})
    assert db.check_serial()['us/b'] == 1
    assert not db.counters['renew_calls']
    results.append('fixed positions order conditional disjoint-output dependencies')

    db = FixedStore(initial, scopes)
    broad = reserve(db, 'broad', ['eu/marker'])
    assert db.capture(broad, 'eu/a', wait=True)
    writer = reserve(db, 'writer', ['eu/a'], floor=2)
    finish(db, writer, {'eu/a': 9})
    reader = db.begin('reader', (10, 'reader'))
    assert not db.capture(reader, 'eu/marker', wait=True)
    finish(db, broad, {'eu/marker': broad.reads['eu/a']['eu/a']})
    assert db.capture(reader, 'eu/marker', wait=True)
    db.choose(reader)
    db.decide(reader, True)
    assert db.check_serial()['eu/marker'] == 1
    results.append('source writers continue while output readers wait')

    db = FixedStore(initial, scopes)
    # A prior read at the second participant forces the chosen position up.
    observer = db.begin('observer', (20, 'observer'))
    assert db.capture(observer, 'us/b')
    db.choose(observer)
    db.decide(observer, True)
    owner = reserve(db, 'owner', ['eu/a', 'us/b'], publish=False)
    assert owner.c > observer.c
    between = db.begin('between', (10, 'between'))
    assert not db.capture(between, 'eu/a', wait=True)
    db.publish_position(owner, ['eu/a'])
    assert db.capture(between, 'eu/a', wait=True)
    db.choose(between)
    db.decide(between, True)
    db.publish_position(owner, ['us/b'])
    assert db.capture(owner, 'eu/a', wait=True)
    finish(db, owner, {'eu/a': 2, 'us/b': 2})
    db.check_serial()
    results.append('publishing exact positions safely releases conservative read waits')

    db = FixedStore(initial, scopes)
    owner = reserve(db, 'owner', ['eu/a'])
    db.decide(owner, False)
    db.release(owner, ['eu/a'])
    assert not db.claims and db.head() == initial
    results.append('abort before values exist releases coverage without installing placeholders')
    return results


if __name__ == '__main__':
    for result in probes():
        print('PASS:', result)
