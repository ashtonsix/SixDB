#!/usr/bin/env python3
"""Authored handoff counterexamples; no consensus, storage, or timing model."""

import argparse
from dataclasses import dataclass
import hashlib
from itertools import combinations, permutations
import json
from pathlib import Path


Prefix = tuple[str, ...]


def is_prefix(left: Prefix, right: Prefix) -> bool:
    return right[:len(left)] == left


@dataclass(frozen=True)
class Report:
    voter: str
    ballot: int
    sequence: Prefix
    learned: Prefix = ()


def select_sequence(reports: tuple[Report, ...]) -> Prefix:
    """Assume authenticated, complete reports from a valid prepare quorum.

    Sequences represent absolute journals, including any compacted base. This
    checks their selection, not whether their prior protocol execution was legal.
    """
    for left, right in combinations(reports, 2):
        if left.ballot == right.ballot and not (
                is_prefix(left.sequence, right.sequence)
                or is_prefix(right.sequence, left.sequence)):
            raise ValueError("incompatible sequences at one ballot")
    selected = max(reports, key=lambda r: (r.ballot, len(r.sequence))).sequence
    if any(not is_prefix(report.learned, selected) for report in reports):
        raise ValueError("selected sequence omits a reported chosen prefix")
    return selected


def same_ballot_delivery(stored: Prefix, incoming: Prefix) -> Prefix:
    if is_prefix(incoming, stored):
        return stored
    if is_prefix(stored, incoming):
        return incoming
    raise ValueError("incompatible same-ballot delivery")


def dependency_valid(sequence: Prefix) -> bool:
    """One deliberately small application-metadata dependency: depends(x)."""
    seen = set()
    for item in sequence:
        if item == "depends(x)" and "x" not in seen:
            return False
        seen.add(item)
    return True


@dataclass(frozen=True)
class Vote:
    voter: str
    history: str
    configuration: str
    ballot: int
    sequence: Prefix
    durable: bool = True


def early_certificate(leader: Vote, follower: Vote) -> Prefix:
    """Inputs stand for genuine configured-voter evidence, not cached bytes."""
    if (leader.voter == follower.voter or not leader.durable or not follower.durable
            or (leader.history, leader.configuration, leader.ballot)
            != (follower.history, follower.configuration, follower.ballot)):
        return ()
    if is_prefix(leader.sequence, follower.sequence):
        return leader.sequence
    if is_prefix(follower.sequence, leader.sequence):
        return follower.sequence
    raise ValueError("incompatible prefix acceptance evidence")


@dataclass(frozen=True)
class Handoff:
    identity: str
    history: str
    source: str
    target: str
    members: frozenset[str]
    predecessor: Prefix

    @property
    def command(self) -> str:
        # A toy typed-command spelling, not a wire format or content digest.
        return f"Handoff[{self.source}->{self.target};{self.identity}]"

    @property
    def sequence(self) -> Prefix:
        return self.predecessor + (self.command,)


def append_in_configuration(stored: Prefix, additions: Prefix,
                            configuration: str) -> Prefix:
    result = stored + additions
    stops = [index for index, command in enumerate(result)
             if command.startswith(f"Handoff[{configuration}->")]
    if stops and stops != [len(result) - 1]:
        raise ValueError("terminal must end its source configuration")
    return result


@dataclass(frozen=True)
class Initialization:
    voter: str
    handoff: Handoff
    stored: Prefix
    has_source_certificate: bool
    durable: bool = True


def ready_for_election(candidate: Handoff, chosen: tuple[Handoff, ...],
                       initializations: tuple[Initialization, ...]) -> bool:
    """Mock source-chosen terminal certificates and durable local initialization.

    Source certificates are assumed authenticated and fully validated, including
    final prefix, successor and coverage/policy bindings. Readiness does not elect
    a leader: normal successor ballot recovery remains required. These are local
    initialization facts, not votes in a separate ADOPT consensus instance.
    """
    source_seals = {h for h in chosen
                    if (h.history, h.source) == (candidate.history, candidate.source)}
    if source_seals != {candidate}:
        return False
    voters = {i.voter for i in initializations
              if i.voter in candidate.members and i.handoff == candidate
              and i.durable and i.has_source_certificate
              and i.stored == candidate.sequence}
    return len(voters) >= len(candidate.members) // 2 + 1


def serial_orders(transactions):
    """Enumerate two authored read/write histories, not a concurrency protocol."""
    valid = []
    for order in permutations(transactions):
        state = {"x": 0, "y": 0}
        for transaction in order:
            if any(state[key] != value for key, value in transaction[1]):
                break
            state.update(transaction[2])
        else:
            valid.append([transaction[0] for transaction in order])
    return valid


def run():
    checks = []

    def check(name, actual, expected, trace):
        if actual != expected:
            raise AssertionError(f"{name}: {actual!r} != {expected!r}")
        checks.append({"name": name, "actual": actual,
                       "expected": expected, "trace": trace})

    def rejection(reports):
        try:
            select_sequence(reports)
        except ValueError as error:
            return str(error)
        return "accepted"

    base = ("base",)
    lower = Report("A", 3, base + ("x", "depends(x)"), base)
    higher = Report("C", 4, base + ("y",), base)
    selected = select_sequence((lower, higher))
    check("highest-ballot-beats-longer-lower-ballot", selected, base + ("y",),
          ["A alone accepted [base,x,depends(x)] at ballot 3.",
           "Ballot 4 prepared on B/C before either had that suffix.",
           "C accepted [base,y] at ballot 4; a later prepare on A/C reports both sequences.",
           "Choose C's whole sequence; its absolute K is 2, not 3."])
    check("same-ballot-selects-longest-compatible-prefix",
          select_sequence((Report("A", 5, base + ("x",)),
                           Report("B", 5, base + ("x", "depends(x)")))),
          base + ("x", "depends(x)"),
          ["One ballot extended [base,x] to [base,x,depends(x)].",
           "Its two prepare reports differ only in accepted length."])
    naive_mix = base + ("y", "depends(x)")
    check("independent-slot-mix-breaks-prerequisite", dependency_valid(naive_mix), False,
          ["Taking the higher ballot at the first suffix slot selects y.",
           "Keeping the only report of the second slot selects depends(x).",
           "The mixed vector has lost its prerequisite x."])
    check("whole-prefix-avoids-invented-dependent-vector", dependency_valid(selected), True,
          ["The selected higher-ballot sequence contains y and no depends(x).",
           "Displaced minority evidence remains evidence; it is not spliced back."])
    check("incompatible-equal-ballot-reports-rejected",
          rejection((Report("A", 5, base + ("x",)),
                     Report("B", 5, base + ("y",)))),
          "incompatible sequences at one ballot",
          ["A and B claim different second entries at one unique leader ballot.",
           "Neither is a prefix of the other: this is invalid evidence."])

    chosen = base + ("x",)
    check("reported-chosen-prefix-preserved",
          is_prefix(chosen, select_sequence((Report("A", 5, chosen, chosen),
                                            Report("B", 6, chosen + ("z",), chosen)))),
          True, ["Previously chosen [base,x] is retained in the higher ballot's extension."])
    check("inconsistent-chosen-prefix-evidence-rejected",
          rejection((Report("A", 5, chosen, chosen),
                     Report("B", 6, base + ("y",), base))),
          "selected sequence omits a reported chosen prefix",
          ["These reports cannot arise from the assumed correct sequence protocol.",
           "A higher ballot number does not excuse losing certified chosen x."])
    check("same-ballot-reordered-shorter-message-does-not-truncate",
          same_ballot_delivery(base + ("x", "z"), base + ("x",)),
          base + ("x", "z"),
          ["Accept of [base,x,z] arrives before an older Accept of [base,x].",
           "Keep the longer accepted sequence; do not append a duplicate x."])
    check("higher-ballot-may-replace-unchosen-tail",
          select_sequence((lower, higher)), higher.sequence,
          ["Only base is certified chosen in A's longer local sequence.",
           "A valid higher-ballot synchronization can replace its unchosen x tail.",
           "A same-ballot append-only check must not forbid this synchronization."])

    seal = Handoff("h1", "H", "C0", "C1", frozenset("DEF"), chosen)
    other = Handoff("h2", "H", "C0", "C2", frozenset("XYZ"), chosen)

    def append_rejection(prefix, additions, configuration):
        try:
            append_in_configuration(prefix, additions, configuration)
        except ValueError as error:
            return str(error)
        return "accepted"

    check("terminal-entry-prevents-source-ballot-extension",
          append_rejection(seal.sequence, ("z",), "C0"),
          "terminal must end its source configuration",
          ["A has proposed P+[Handoff(C0->C1)] in its current ballot.",
           "It cannot append ordinary z after that terminal, even before learning a majority."])
    check("competing-targets-cannot-share-one-source-prefix",
          append_rejection(chosen, (seal.command, other.command), "C0"),
          "terminal must end its source configuration",
          ["Two queued successor nominations are alternatives, not two appendable commands."])
    check("unchosen-terminal-can-be-displaced-by-valid-recovery",
          append_in_configuration(select_sequence((Report("B", 0, chosen),
                                                    Report("C", 0, chosen))),
                                  (other.command,), "C0"),
          other.sequence,
          ["Only A accepted terminal h1 at ballot 7; no other voter saw it.",
           "A new ballot prepares on B/C; their promises exclude future ballot-7 acceptance.",
           "Recovery selects P without h1, so the new leader may propose terminal h2.",
           "A may later replace its unchosen h1 through this valid higher-ballot synchronization."])
    recovered_terminal = select_sequence((Report("A", 7, seal.sequence, chosen),
                                          Report("B", 0, chosen, chosen)))
    check("reported-terminal-is-carried-despite-unknown-choice",
          recovered_terminal, seal.sequence,
          ["Prepare A/B sees A's terminal h1; its majority status is unknown.",
           "Selecting that sequence carries the exact h1; absence of a response cannot justify retargeting."])
    check("recovered-terminal-cannot-be-followed-by-another-target",
          append_rejection(recovered_terminal, (other.command,), "C0"),
          "terminal must end its source configuration",
          ["Recovery selected h1. A new proposal for h2 cannot extend that source sequence."])
    check("chosen-terminal-preserved-by-later-source-recovery",
          select_sequence((Report("B", 7, seal.sequence, chosen),
                           Report("C", 0, chosen, chosen))),
          seal.sequence,
          ["A/B accepted P+h1, but B's learned frontier still ends at P.",
           "A later prepare on B/C retains B's accepted terminal, including the chosen successor."])
    before = (Vote("A", "H", "C0", 7, seal.sequence),)
    after = before + (Vote("B", "H", "C0", 7, seal.sequence),)
    check("terminal-choice-can-complete-after-proposer-crash",
          [len(before) >= 2, len(after) >= 2], [False, True],
          ["A durably self-accepts P+h1, sends it with acceptance evidence, then crashes.",
           "B has promised no higher ballot and receives the delayed complete prefix.",
           "B's durable acceptance completes the majority; no permanent freeze was issued."])
    check("first-follower-can-export-the-late-terminal-certificate",
          early_certificate(*after), seal.sequence,
          ["B combines A's genuine prior acceptance with its own durable acceptance.",
           "The certificate binds the entire P+h1 and can escape without contacting A again."])
    check("inherited-old-terminal-does-not-stop-successor-configuration",
          append_in_configuration(seal.sequence, ("z",), "C1"),
          seal.sequence + ("z",),
          ["C1 starts from certified P+Handoff(C0->C1).",
           "Its inherited terminal stops C0 only; C1 may append after its ordinary ballot recovery."])

    leader = Vote("A", "H", "C0", 7, chosen)
    follower = Vote("B", "H", "C0", 7, chosen + ("z",))
    check("early-follower-certificate-covers-only-common-durable-prefix",
          early_certificate(leader, follower), chosen,
          ["Leader evidence is for [base,x]; B has durably accepted [base,x,z].",
           "The presented evidence certifies x, but cannot certify z.",
           "No leader return hop is needed to combine the two genuine acceptances."])
    check("different-ballots-do-not-form-this-two-vote-certificate",
          early_certificate(leader, Vote("B", "H", "C0", 8, chosen)), (),
          ["Equal bytes accepted at ballots 7 and 8 are not this same-ballot certificate."])
    check("volatile-follower-bytes-do-not-complete-certificate",
          early_certificate(leader, Vote("B", "H", "C0", 7, chosen, False)), (),
          ["B has received the prefix but its persistence obligation has not completed."])

    copied = tuple(Initialization(v, seal, seal.sequence, False) for v in "DE")
    initialized = tuple(Initialization(v, seal, seal.sequence, True) for v in "DE")
    check("staged-prefix-and-terminal-proposal-do-not-authorize-successor",
          ready_for_election(seal, (), copied), False,
          ["D/E durably hold P and proposed h1 but no source chosen-prefix certificate.",
           "Those staged bytes cannot establish that h1 won or that C0 is stopped."])
    check("chosen-terminal-certificate-must-reach-new-voters",
          ready_for_election(seal, (seal,), copied), False,
          ["The source chose P+h1, but its certificate has not reached D/E.",
           "If source evidence is lost here, staged bytes alone cannot close the authority gap."])
    check("one-initialized-member-is-not-a-successor-election-quorum",
          ready_for_election(seal, (seal,), initialized[:1] + copied[1:]), False,
          ["D has durable S and its source certificate; E has only S bytes.",
           "Only D is eligible for ordinary C1 election/recovery."])
    check("initialized-quorum-can-run-normal-successor-election",
          ready_for_election(seal, (seal,), initialized), True,
          ["D/E durably initialized exact S=P+h1 and its verified source certificate.",
           "Normal C1 ballot recovery may proceed without C0; no separate ADOPT consensus is used."])
    hash_only = tuple(Initialization(v, seal, (), True) for v in "DE")
    check("terminal-certificate-without-prefix-bytes-is-not-ready",
          ready_for_election(seal, (seal,), hash_only), False,
          ["D/E have the chosen certificate but no installed complete prefix or certified base/suffix.",
           "A hash of missing history cannot initialize an acceptor."])
    other_initialized = tuple(Initialization(v, other, other.sequence, True) for v in "XY")
    check("slow-chosen-target-does-not-authorize-retargeting",
          ready_for_election(other, (seal,), other_initialized), False,
          ["C0 chose h1 naming C1. Candidate C2 has bytes for a different h2.",
           "Local initialization claims cannot manufacture a source certificate for h2."])
    check("conflicting-source-certificates-are-not-a-target-selection-rule",
          ready_for_election(seal, (seal, other), initialized), False,
          ["Two purported chosen terminals naming different successors violate sequence consensus.",
           "The probe refuses to select a target from inconsistent assumed certificate facts."])

    t = ("T@20", (("x", 0),), (("y", 1),))
    u_bad = ("U@10", (("y", 0),), (("x", 1),))
    u_good = ("U@21", (("y", 1),), (("x", 1),))
    check("forgotten-completed-read-bound-allows-a-nonserial-history",
          serial_orders((t, u_bad)), [],
          ["T@20 reads x=0 and commits y=1.",
           "Recovery loses T's observation bound; later U is assigned 10, reads y=0 and writes x=1.",
           "T's x observation requires T before U; U's y observation requires U before T."])
    check("retained-read-floor-has-a-serial-positive-control",
          serial_orders((t, u_good)), [["T@20", "U@21"]],
          ["The retained floor places U after T; U observes y=1.",
           "This is a semantic comparison, not an implementation of floor allocation."])

    return {
        "scope": "Authored sequence-selection, certificate-boundary, and two-transaction histories",
        "limits": [
            "No exhaustive state exploration or consensus/reconfiguration proof.",
            "Reports, votes, chosen-terminal certificates, and initialization are explicit assumed facts.",
            "No authentication, disk writes, transport, failure detector, or SOS control.",
            "Terminal payload and coverage/policy bindings are assumed; no receipt or manifest format.",
            "Full prefixes stand in for correctly anchored compacted bases and suffixes.",
            "Recovery recipes, transaction state, result gates, and performance are not implemented.",
        ],
        "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "sequence_reference": "https://arxiv.org/pdf/2008.13456",
        "checks": checks,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(result['checks'])} authored checks passed. "
          "No consensus proof or timing model; certificates are assumed facts.")


if __name__ == "__main__":
    main()
