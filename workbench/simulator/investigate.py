"""Captured, programmatic experiments beyond the default Orbital workload.

Assemblies live in native clients; these small grids vary one explicit question
at a time. Import cases() or campaign.run_cases() to author another experiment.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

if __package__:
    from .campaign import grid, run_cases
else:
    from campaign import grid, run_cases


BINARIES = {
    "regional": "simulator_regional_run",
    "ordering": "simulator_regional_run",
    "hosted": "simulator_hosted_recovery_run",
    "retention": "simulator_retention_pressure_run",
}


def cases(suite):
    # These named grids reproduce the historical ordered-policy comparisons.
    for case in historical_cases(suite):
        if suite in {"regional", "ordering"}:
            case["policy"] = "eligible" if case.pop("eligible-first", False) else "ordered"
        yield case


def historical_cases(suite):
    if suite == "regional":
        # A WAN transaction does not move local replicas across the ocean.
        # Keep the local offered population identical in every matched pair.
        for bridge in ({}, {"bridge": True}, {"bridge-cut": True}):
            yield from grid({"points": 12, "until": 400_000_000, "retry": 80_000_000, **bridge},
                            seed=(1, 7, 19), region=(1_000, 20_000_000),
                            shared=(False, True), interval=(100_000, 2_000_000))
        for bridge in ({"bridge": True}, {"bridge-cut": True}):
            yield from grid({"interval": 20_000, "until": 400_000_000,
                             "region": 20_000_000, "retry": 80_000_000, **bridge},
                            seed=(1, 7, 19), points=(48, 192), shared=(False, True))
        # Both retry candidates get the same complete observation window and a
        # sufficient dispatch budget. A short censored prefix is not a failure.
        yield from grid({"points": 12, "interval": 100_000, "until": 400_000_000,
                         "region": 20_000_000, "bridge": True, "events": 2_000_000},
                        seed=(1, 7, 19), retry=(100_000, 80_000_000))
    elif suite == "hosted":
        yield from grid({}, seed=(1, 7, 19), mode=("none", "process", "host"),
                        memory=(16_384, 24_576, 65_536))
    elif suite == "ordering":
        for points, interval in ((12, 100_000), (48, 20_000)):
            for bridge in ({"bridge": True}, {"bridge-cut": True}):
                yield from grid({"points": points, "interval": interval, "region": 20_000_000,
                                 "until": 400_000_000, "retry": 80_000_000, **bridge},
                                seed=(1, 7, 19), shared=(False, True),
                                **{"eligible-first": (False, True)})
        yield from grid({}, seed=(1, 7, 19), until=(10_000_000, 40_000_000),
                        **{"progress-rounds": (8, 16, 32), "eligible-first": (False, True)})
    elif suite == "retention":
        for memory, storage in ((4096, 8192), (8192, 16384)):
            yield from grid({"memory": memory, "storage": storage}, seed=(1, 7, 19),
                            incident=("none", "checkpoint-reset", "cursor-reset"))
    else:
        raise ValueError(f"unknown investigation {suite}")


def captured(suites, output, workspace):
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / "workbench/tools"))
    from experiment import Run
    run = Run(root, output, {"investigations": suites}, workspace=workspace)
    failure = None
    try:
        run.step("configure", ["cmake", "-S", str(run.source_root), "-B", str(run.build_dir),
                               "-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo"])
        run.step("build", ["cmake", "--build", str(run.build_dir), "--target",
                           "simulator_validate", *dict.fromkeys(BINARIES[suite] for suite in suites), "-j", "4"])
        for suite in suites:
            run.step(suite, [sys.executable, str(run.source_root / "workbench/simulator/investigate.py"),
                             "--suite", suite, "--binary", str(run.build_dir / "workbench/simulator" / BINARIES[suite]),
                             "--output", str(output / suite)])
            run.step(f"select-{suite}", [sys.executable, str(run.source_root / "workbench/simulator/experiments/select.py"),
                                        str(output / suite / "summary.json"), str(output / suite / "selected.json")])
        run.compact([f"{suite}/selected.json" for suite in suites], [])
    except Exception as error:
        failure = error
    failure = run.finish(failure)
    if failure:
        raise failure


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=(*BINARIES, "all"), default="all")
    parser.add_argument("--binary", type=Path, help="existing executable; requires one suite")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, default=Path("build/workspaces/simulator-investigation"))
    args = parser.parse_args()
    if args.binary:
        if args.suite == "all":
            parser.error("--binary requires a single suite")
        raise SystemExit(run_cases(args.binary, cases(args.suite), args.output.resolve(), suite=args.suite))
    captured(list(BINARIES) if args.suite == "all" else [args.suite],
             args.output.resolve(), args.workspace.resolve())
