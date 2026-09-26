#!/usr/bin/env python3
"""Finite placement/closure counterexamples, not a consensus or timing model."""

import argparse
from dataclasses import dataclass, field
import hashlib
from itertools import combinations
import json
from pathlib import Path


def quorums(members: str, size: int) -> tuple[frozenset[str], ...]:
    return tuple(frozenset(group) for group in combinations(members, size))


def disjoint_example(left, right):
    return next((sorted(a) + ["|"] + sorted(b)
                 for a in left for b in right if not a & b), None)


def placement(name, members, size, domains):
    """A vote is assumed durable on precisely the selected deciding set."""
    deciding = quorums(members, size)
    rows = []
    for domain in sorted(set(domains.values())):
        lost = frozenset(node for node in members if domains[node] == domain)
        erased = next((sorted(group) for group in deciding if group <= lost), None)
        rows.append({
            "lost_domain": domain,
            "metadata_survives_every_deciding_set": erased is None,
            "erased_deciding_set": erased,
            "surviving_members_can_form_quorum": len(set(members) - lost) >= size,
        })
    return {"name": name, "deciding_sets": len(deciding), "domain_losses": rows}


@dataclass
class Closure:
    # Entries are verified exact bytes on known holders. Authority is an explicit
    # dependency below; possessing bytes alone does not establish authority.
    holders: dict[str, frozenset[str]]
    # OR between recipes; AND between dependencies in each recipe.
    recipes: dict[str, tuple[frozenset[str], ...]] = field(default_factory=dict)

    def available(self, unavailable: frozenset[str]) -> frozenset[str]:
        # All remaining holders are assumed reachable/authorized from one recovery
        # site. Surviving bytes on the other side of a partition can still be unusable.
        known = {item for item, holders in self.holders.items()
                 if holders - unavailable}
        while True:
            added = {item for item, recipes in self.recipes.items()
                     if any(recipe <= known for recipe in recipes)} - known
            if not added:
                return frozenset(known)
            known.update(added)


def recipe(*items):
    return frozenset(items)


def run():
    local = frozenset("abc")
    remote = frozenset("de")
    every_case = []

    def check(name, actual, expected, explanation):
        if actual != expected:
            raise AssertionError(f"{name}: {actual!r} != {expected!r}")
        every_case.append({"name": name, "result": actual,
                           "expected": expected, "explanation": explanation})

    layouts = [
        placement("2-of-3 in one region", "abc", 2,
                  {"a": "r1", "b": "r1", "c": "r1"}),
        placement("3-of-5 across regions 3+2", "abcde", 3,
                  {"a": "r1", "b": "r1", "c": "r1", "d": "r2", "e": "r2"}),
        placement("3-of-5 across regions 2+2+1", "abcde", 3,
                  {"a": "r1", "b": "r1", "c": "r2", "d": "r2", "e": "r3"}),
    ]
    check("three-local-metadata-loss",
          layouts[0]["domain_losses"][0]["metadata_survives_every_deciding_set"],
          False, "Every possible deciding pair is within the failed region.")
    check("five-witness-count-is-not-region-protection",
          layouts[1]["domain_losses"][0]["metadata_survives_every_deciding_set"],
          False, "ABC can decide without either remote witness.")
    check("five-witness-3+2-loses-write-quorum",
          layouts[1]["domain_losses"][0]["surviving_members_can_form_quorum"],
          False, "Two surviving remote members cannot form a 3-of-5 quorum.")
    check("five-witness-2+2+1-metadata-and-quorum",
          all(row["metadata_survives_every_deciding_set"] and
              row["surviving_members_can_form_quorum"]
              for row in layouts[2]["domain_losses"]), True,
          "All single-region losses preserve some bytes and a quorum; this is only placement.")

    old = quorums("abc", 2)
    replacements = []
    for label, new in [("survivors AB, 2-of-2", quorums("ab", 2)),
                       ("replace C by D, 2-of-3 ABD", quorums("abd", 2)),
                       ("new pair AD, 2-of-2", quorums("ad", 2)),
                       ("expand to ABCDE, 3-of-5", quorums("abcde", 3))]:
        replacements.append({"new_rule": label,
                             "disjoint_old_new_example": disjoint_example(old, new)})
    check("even-one-replacement-needs-a-transition",
          replacements[1]["disjoint_old_new_example"] is not None, True,
          "Old BC and new AD can be disjoint; timeout membership substitution is unsafe.")
    check("survivor-pair-adds-no-copy",
          set(quorums("ab", 2)) == {group for group in old if "c" not in group},
          True, "The only reachable old quorum already requires both A and B.")
    check("emergency-pair-can-ignore-old-decision",
          replacements[2]["disjoint_old_new_example"] is not None, True,
          "A's empty log cannot rule out a chosen decision on B and C.")
    check("expansion-needs-a-transition-too",
          replacements[3]["disjoint_old_new_example"] is not None, True,
          "Adding members is not by itself a safe bridge from the old rule.")

    remote_pairs = tuple(group for group in quorums("abcd", 2) if group & {"d"})
    check("remote-required-pairs-preserve-metadata-bytes",
          all(group - local for group in remote_pairs), True,
          "This checks holder coverage only, not a legal consensus construction.")
    check("remote-required-pairs-are-not-an-arbitrary-election-rule",
          disjoint_example(remote_pairs, quorums("abc", 2)) is not None, True,
          "AD can be disjoint from a BC election: domains do not replace a consensus proof.")

    graph = Closure({"metadata": frozenset("ad"), "payload": frozenset("ab")},
                    {"admitted-event": (recipe("metadata", "payload"),)})
    check("remote-witness-local-payload-is-incomplete",
          "admitted-event" in graph.available(local), False,
          "A remote metadata copy does not contain the input bytes.")
    graph.holders["payload"] = frozenset("abd")
    check("copying-payload-closes-this-small-recipe",
          "admitted-event" in graph.available(local), True,
          "Both explicit dependencies survive; no code or transaction state is modeled here.")

    graph = Closure({"old-meta": frozenset("ab"), "old-input": frozenset("ab"),
                     "new-meta": frozenset("ad"), "new-input": frozenset("ad")},
                    {"new-obligation": (recipe("new-meta", "new-input"),),
                     "whole-history": (recipe("old-meta", "old-input", "new-obligation"),)})
    check("new-policy-covers-new-obligation",
          "new-obligation" in graph.available(local), True,
          "New writes can meet the new placement before old history does.")
    check("new-policy-does-not-retroactively-cover-history",
          "whole-history" in graph.available(local), False,
          "Old metadata and payload still reside only in the threatened region.")
    graph.holders["old-meta"] = frozenset("abd")
    check("historical-metadata-copy-alone-is-insufficient",
          "whole-history" in graph.available(local), False,
          "Historical coverage advances only after all dependencies are covered.")
    graph.holders["old-input"] = frozenset("abd")
    check("historical-recipe-fully-repaired",
          "whole-history" in graph.available(local), True,
          "The explicit old and new dependencies are now outside the failed domain.")

    # B denotes blob storage; c/d/e denote consumer peers. Only exact verified
    # copies appear here. Keys/authority/code are needed even when object bytes exist.
    graph = Closure({"manifest": frozenset("Bd"), "checkpoint": frozenset("Bc"),
                     "dictionary": frozenset("Be"), "code": frozenset("Bd"),
                     "key": frozenset("Bd"), "journal-tail": frozenset("Bd")},
                    {"restored-cut": (recipe("manifest", "checkpoint", "dictionary",
                                              "code", "key", "journal-tail"),)})
    check("blob-down-distributed-cache-closure",
          "restored-cut" in graph.available(frozenset("B")), True,
          "No one consumer has the full recipe; peer union covers it.")
    check("blob-and-only-dictionary-peer-down",
          "restored-cut" in graph.available(frozenset("Be")), False,
          "Cached checkpoint bytes cannot replace their missing dictionary.")
    graph.holders["dictionary"] = frozenset("Bde")
    check("dictionary-repair-restores-closure",
          "restored-cut" in graph.available(frozenset("Be")), True,
          "One verified extra artifact copy restores this recipe.")
    graph.holders["manifest"] = frozenset("B")
    check("cached-bytes-without-authoritative-manifest",
          "restored-cut" in graph.available(frozenset("B")), False,
          "Peer popularity does not establish the requested committed cut.")
    graph.holders["manifest"] = frozenset("Bd")
    graph.holders["key"] = frozenset("B")
    check("cached-ciphertext-without-key",
          "restored-cut" in graph.available(frozenset("B")), False,
          "Payload survival is distinct from access to interpretation dependencies.")

    graph = Closure({"certified-snapshot": remote, "checkpoint-code": remote},
                    {"state": (recipe("old-log", "old-code"),
                               recipe("certified-snapshot", "checkpoint-code"))})
    check("alternative-complete-recipe-is-enough",
          "state" in graph.available(local), True,
          "This declared state obligation needs either recipe, not every historical byte.")
    graph = Closure({}, {"decision": (recipe("state"),),
                         "state": (recipe("decision"),)})
    check("cyclic-recovery-dependencies-do-not-bootstrap-themselves",
          "state" in graph.available(frozenset()), False,
          "A dependency cycle with no available seed proves no recoverable state.")

    return {"scope": "Finite set coverage and declared recipe closure, no protocol or timing model",
            "assumption": "All nonfailed holders are reachable and authorized from the recovery site",
            "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "checks": every_case, "placements": layouts,
            "configuration_counterexamples": replacements}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(result['checks'])} checks passed; "
          f"{len(result['placements'])} placement comparisons. "
          "No detector calibration or consensus proof.")


if __name__ == "__main__":
    main()
