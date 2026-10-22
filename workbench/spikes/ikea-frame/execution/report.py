"""Retain every measured arm, replacing verbose checked groups with count/hash."""
import argparse
import copy
import hashlib
import json
from pathlib import Path


def compact(document):
    result = copy.deepcopy(document)
    result["selection"] = ("All queries, programs, raw repetitions and counters; "
                           "full checked group arrays replaced by count and canonical SHA-256. "
                           "Full output remains in the recovery archive.")
    for case in result["cases"]:
        for record in case["records"]:
            if record["type"] != "result":
                continue
            groups = record.pop("groups")
            encoded = json.dumps(groups, sort_keys=True, separators=(",", ":")).encode()
            record["group_count"] = len(groups)
            record["groups_sha256"] = hashlib.sha256(encoded).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(compact(json.loads(args.input.read_text())), indent=2) + "\n")


if __name__ == "__main__":
    main()
