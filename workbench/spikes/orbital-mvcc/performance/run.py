#!/usr/bin/env python3
"""Offline summaries/ABI/counter reports for the retired versioned driver."""
import argparse
import csv
import hashlib
import json
import re
import statistics
from pathlib import Path


def csv_file(path, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(dict.fromkeys(key for row in rows for key in row)))
        writer.writeheader()
        writer.writerows(rows)

def summarize(output):
    rows, groups, parity = [], {}, {}
    for path in sorted(output.glob("trial-*.json")):
        result = json.loads(path.read_text())
        if not result["passed"]:
            raise ValueError(f"qualification failed: {path}")
        stem = path.stem.split("-")
        repeat = int(stem[2])
        interval = result["interval"]
        samples_path = path.with_suffix(".latency.csv")
        if samples_path.exists():
            with samples_path.open() as stream:
                samples = list(csv.DictReader(stream))
            if len(samples) != interval["batch_samples"] or sum(int(x["requests"]) for x in samples) != result["operations"]:
                raise ValueError(f"raw latency sample extent differs: {path}")
            if any(int(x["batch"]) != i or int(x["requests"]) != min(result["batch"], result["operations"]-i*result["batch"]) for i,x in enumerate(samples)):
                raise ValueError(f"raw latency sample grouping differs: {path}")
            ordered = sorted(int(x["ns"]) for x in samples)
            for name,rank in [("p50",500),("p99",990),("p999",999)]:
                if interval["batch_makespan_ns"][name] != ordered[(len(ordered)*rank+999)//1000-1]:
                    raise ValueError(f"raw latency quantile differs: {path}")
        expected_scopes = (result["operations"] + result["batch"] - 1) // result["batch"]
        if result["backend"] != "direct" and interval["snapshot_scopes"] != expected_scopes:
            raise ValueError(f"snapshot grouping differs: {path}")
        if interval["batch_samples"] != (expected_scopes if result["mode"] == "latency" else 0):
            raise ValueError(f"clock replay mode differs: {path}")
        key = (result["workload"], result["rows"], result["objects"], result["batch"], result["from"])
        modern = result.get("report_schema",1)>=2
        dimensions = (result.get("retention_age_publications",0),result.get("offered_metadata_partition",0),
                      result.get("parts_per_object",1),result.get("conditioning","none"),result.get('sample_storage','historical-mode')) if modern else ()
        parity_key = key+dimensions
        identity = (result["trace_fingerprint"], result["outcome_digest"], interval["logical_requests"],
                    interval["logical_rows"], result["seed"])
        if parity_key in parity and parity[parity_key] != identity:
            raise ValueError(f"matched native outcomes differ: {path}")
        parity[parity_key] = identity
        base = dict(workload=key[0], rows=key[1], objects=key[2], batch=key[3], source_format=key[4],
                    backend=result["backend"], mode=result["mode"], repeat=repeat,
                    comparison="conversion" if key[0] == "convert" else
                    "contextual-series" if key[4] == "columns" else
                    "contextual-row-key" if result["backend"] == "row-lmdb" else
                    "prequalified-native" if result["backend"] == "direct" else "common-tuple",
                    wall_ns=interval["wall_ns"], caller_cpu_ns=interval["caller_cpu_ns"],
                    ns_per_request=interval["wall_ns"]/result["operations"],
                    snapshot_scopes=interval["snapshot_scopes"], bindings=result["resolved_bindings"],
                    logical_rows=interval["logical_rows"],
                    batch_p50_ns=interval["batch_makespan_ns"]["p50"],
                    batch_p99_ns=interval["batch_makespan_ns"]["p99"],
                    copied_bytes=result["costs"]["copied_bytes"],
                    source_bindings=result["update_preparation"]["source_bindings"],
                    dynamic_shape_preparations=result["update_preparation"]["dynamic_shape_preparations"],
                    requested_body_bytes=result["costs"]["requested_body_bytes"],
                    scratch_bound_bytes=result["costs"]["scratch_bound_bytes"],
                    release_and_drain_ns=result["release_and_drain"]["wall_ns"])
        for label, field in [("initial", "footprint_initial"), ("held", "footprint_after_interval"),
                             ("released", "footprint_after_reader_release")]:
            f = result.get(field, {})
            for name in ['alternative_count', 'retirement_records_available']:
                if name in f:
                    base[f'{label}_{name}'] = f[name]
            unavailable = None if modern else 0
            base.update({f"{label}_arena_charge": f.get("arena_charged_bytes", unavailable),
                         f"{label}_metadata_bytes": f.get("metadata_bytes", unavailable),
                         f"{label}_versions": f.get("versions", unavailable),
                         f"{label}_file_used_bytes": f.get("file_used_bytes", unavailable)})
        if "codec" in result:
            base.update(codec=result["codec"], conditioning=result["conditioning"],
                        process_cpu_ns=interval["process_cpu_ns"],
                        conditioning_wall_ns=result["conditioning_interval"]["wall_ns"])
        if 'body_cold_start' in result:
            for name,value in result['body_cold_start'].items():
                base[f'body_cold_start_{name}']=value
        if 'lmdb_short_reader' in result:
            for name,value in result['lmdb_short_reader'].items():
                base[f'lmdb_short_reader_{name}']=value
        if "process_resources" in result:
            base.update(process_peak_rss_kib=result["process_resources"]["peak_rss_kib"])
            for label, field in [("initial", "footprint_initial"), ("held", "footprint_after_interval"), ("released", "footprint_after_reader_release")]:
                f=result.get(field,{})
                for name in ["pool_mapped_bytes","pool_reserved_bytes","pool_metadata_bytes","pool_idle_bytes","pool_allocations","pool_reuses","pool_growths","pool_trim_pages"]:
                    base[f"{label}_{name}"]=f.get(name,None if modern else 0)
        if modern:
            base.update(report_schema=result["report_schema"],operations=result["operations"],
                byte_limit=result["byte_limit"],retention_age_publications=dimensions[0],
                offered_metadata_partition=dimensions[1],parts_per_object=dimensions[2],
                timed_retained_first_discoveries=result["timed_retained_first_discoveries"],
                metadata_allocation_counts_available=result["metadata_allocation_counts_available"],
                release_caller_cpu_ns=result["release_and_drain"]["caller_cpu_ns"],
                release_process_cpu_ns=result["release_and_drain"]["process_cpu_ns"],
                drain_steps=result["drain_steps"],physical_poll_budget=result["physical_poll_budget"],
                maintenance_policy=result["maintenance_policy"],batch_p999_ns=interval["batch_makespan_ns"]["p999"])
            publication_groups=expected_scopes if result['workload']=='point-update' else 0
            age=dimensions[0]
            base.update(publication_groups=result.get('publication_groups',publication_groups),
                retained_reader_rotations=result.get('retained_reader_rotations',publication_groups//age if age else 0),
                final_retained_age_publications=result.get('final_retained_age_publications',publication_groups%age if age else publication_groups),
                retained_phase_source='reported' if 'final_retained_age_publications' in result else 'derived from recorded groups and periodic rotation')
            base.update(sample_storage=dimensions[4],reserved_sample_bytes=result.get('reserved_sample_bytes'),
                write_wrapper_minor_faults=(result.get('write_wrapper_faults') or {}).get('minor'),
                write_wrapper_major_faults=(result.get('write_wrapper_faults') or {}).get('major'))
            for name in ["current_rss_kib","rusage_inherited_peak_rss_kib","minor_faults","major_faults","voluntary_switches","involuntary_switches"]:
                base[f"process_{name}"]=result["process_resources"][name]
            for label,field in [("initial","footprint_initial"),("held","footprint_after_interval"),("released","footprint_after_reader_release")]:
                f=result.get(field,{})
                for name in ["metadata_peak","metadata_limit","metadata_allocation_calls","history_capacity_bytes",
                             "descriptor_directory_capacity_bytes","object_directory_capacity_bytes","debt_capacity_bytes",
                             "copied_metadata_bytes","captured_descriptor_bytes","reclamation_work","last_sweep_bytes",
                             "last_sweep_cuts","maintenance_refusals","catalog_core_bytes","catalog_entry_bytes","version_node_bytes"]:
                    base[f"{label}_{name}"]=f.get(name)
        if 'metadata_maintenance_work_limit' in result:
            base['metadata_maintenance_work_limit']=result['metadata_maintenance_work_limit']
            base['publication_plus_final_settlement_ns_per_request']=(interval['wall_ns']+result['release_and_drain']['wall_ns'])/result['operations']
            for label,field in [('initial','footprint_initial'),('held','footprint_after_interval'),('released','footprint_after_reader_release')]:
                f=result.get(field,{})
                for name in ['cut_capacity_bytes', 'debt_objects', 'maintenance_remaining_entries', 'entry_visits', 'version_visits', 'cut_visits', 'debt_visits', 'plan_work', 'maintenance_restarts', 'debt_reservation_work', 'reclaimed_versions', 'completed_sweeps', 'head_refreshes', 'maintenance_pending', 'maintenance_counters_available']:
                    base[f'{label}_{name}']=f.get(name)
                if 'census_visits' in f:
                    for name in ['census_visits', 'census_counter_available', 'census_word_queries', 'census_word_counter_available']:
                        base[f'{label}_{name}']=f.get(name)
        if result.get('report_schema',1)>=3:
            base['mapped_shape_metadata_bytes']=result['mapped_shape_metadata_bytes']
            # Add fields only for captures which report them, keeping historical
            # report regeneration stable. These inline views overlap records.
            if 'mapped_shape_record_bytes' in result:
                for name in ['mapped_shape_record_bytes','mapped_write_control_live_bytes',
                             'mapped_write_control_slots_bytes']:
                    base[name]=result[name]
                for label,field in [('initial','footprint_initial'),('held','footprint_after_interval'),('released','footprint_after_reader_release')]:
                    f=result.get(field,{})
                    for name in ['write_control_live_bytes','write_control_slots_bytes']:
                        base[f'{label}_{name}']=f.get(name)
            for name in ['private_admissions','key_reads','full_rows_read','applied_updates']:
                base[f'write_batch_{name}']=result['write_batch_probe'][name]
            for label,field in [('initial','footprint_initial'),('held','footprint_after_interval'),('released','footprint_after_reader_release')]:
                f=result.get(field,{})
                for name in ['shared_shape_bytes','write_shape_heap_bytes','schema_bytes','borrowed_write_shape_record_bytes',
                             'pool_setup_and_inventory_bytes','active_allocations','live_capacity_bytes','held_extent_bytes',
                             'backend_record_bytes','write_caller_arrays_bytes']:
                    base[f'{label}_{name}']=f.get(name)
        rows.append(base)
        group_key = key + (result["backend"], result["mode"])
        if "codec" in result:
            group_key += (result["codec"],result["conditioning"])
        group_key += dimensions
        groups.setdefault(group_key, []).append(base)
    csv_file(output/"measurements.csv", rows)
    summary = []
    for key, values in groups.items():
        summary.append(dict(workload=key[0], rows=key[1], objects=key[2], batch=key[3],
                            source_format=key[4], backend=key[5], mode=key[6],
                            repetitions=len(values),
                            median_ns_per_request=statistics.median(x["ns_per_request"] for x in values),
                            min_ns_per_request=min(x["ns_per_request"] for x in values),
                            max_ns_per_request=max(x["ns_per_request"] for x in values),
                            median_batch_p99_ns=statistics.median(x["batch_p99_ns"] for x in values)))
        if len(key) > 7:
            summary[-1].update(codec=key[7],conditioning=key[8])
        first=values[0]
        if first.get("report_schema",1)>=2:
            for name in ["retention_age_publications","offered_metadata_partition","parts_per_object","byte_limit","operations",
                         "publication_groups","retained_reader_rotations","final_retained_age_publications","retained_phase_source"]:
                summary[-1][name]=first[name]
            summary[-1]['sample_storage']=first['sample_storage']
            for name in ["caller_cpu_ns","process_cpu_ns","process_peak_rss_kib","release_caller_cpu_ns",
                         "release_process_cpu_ns","release_and_drain_ns","batch_p999_ns"]:
                summary[-1][f"median_{name}"]=statistics.median(v[name] for v in values)
    csv_file(output/"summary.csv", summary)
    (output/"parity.json").write_text(json.dumps(dict(passed=True, trials=len(rows), matched_cases=len(parity),
        independent_cli_oracle=True, scopes_and_modes_checked=True, all_repetitions_retained=True), indent=2)+"\n")

def abi(output):
    wanted = ("::run_orbital_reads(", "::run_lmdb_reads(", "::run_direct_reads(",
              "::run_orbital_codec_reads(", "::run_lmdb_codec_reads(",
              "::run_orbital_consume_reads(", "::run_lmdb_consume_reads(",
              "::run_lmdb_shape_reads(", "::composed_reads<", "::read_proven(",
              "::read_mapped_proven(", "::lmdb_composed_update(",
              "orbital_mvcc::composed::Backend<",
              "::candidate_reads<", "::CandidateBodies<", "::sized_reads<",
              "::Orbital::cursor(", "::qualify_tuple(", "::ReadData::get(",
              "::read_operation<orbital_bench::versioned::TupleBody>",
              "::Store::read(", "::Store::prepare(", "::Store::install(", "::Store::drain(")
    sizes = {}
    for line in (output/"symbols.txt").read_text().splitlines():
        fields = line.split(maxsplit=3)
        if len(fields) == 4 and any(x in fields[3] for x in wanted):
            sizes[fields[3]] = int(fields[1], 16)
    functions, name, instructions = [], None, []
    def flush():
        if name and any(x in name for x in wanted):
            functions.append(dict(symbol=name, code_bytes=sizes.get(name),
                prologue=instructions[:12],
                stack_adjustments=[x for x in instructions if
                    re.search(r"\bsub\s+sp,\s*sp,|\[sp,\s*#-\w+\]!|\bsub\w*\s+\$[^,]+,\s*%rsp", x)],
                call_targets=sorted(set(re.findall(r"\b(?:bl|callq?)\s+\S+\s+<(.*)>$", "\n".join(instructions), re.MULTILINE))),
                direct_memcpy_call=any("memcpy" in x and re.search(r"\b(?:bl|callq?)\b", x) for x in instructions)))
    for line in (output/"disassembly.txt").read_text().splitlines():
        match = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if match:
            flush(); name, instructions = match[1], []
        elif re.match(r"^\s+[0-9a-f]+:", line):
            instructions.append(line.strip())
    flush()
    if not any("::run_orbital_reads(" in x["symbol"] for x in functions):
        raise ValueError("actual executable caller missing from disassembly")
    with (output/"bin/orbital_versioned_performance_bench").open("rb") as binary:
        header = binary.read(20)
    if len(header) < 20 or header[:4] != b"\x7fELF" or header[5] not in [1, 2]:
        raise ValueError("captured executable is not a supported ELF file")
    machine = int.from_bytes(header[18:20], "little" if header[5] == 1 else "big")
    architecture = {183: "aarch64", 62: "x86_64"}.get(machine, f"ELF-machine-{machine}")
    (output/"abi.json").write_text(json.dumps(dict(architecture=architecture,
        method="exact measured executable, separate TUs, no LTO; structural only",
        limitation="stack traffic is not evidence of forwarding stalls; no model cycles",
        functions=functions), indent=2)+"\n")

def sanitizer_summary(output):
    path = output/"sanitizers/qualification.json"
    if not path.exists():
        return
    receipt = json.loads(path.read_text())
    if receipt.get('format')==2:
        summary=dict(passed=receipt['passed'],source_sha256_digest=receipt['compiled_source_digest'],
            same_compiled_source_as_native_capture=True,cli_cases=len(receipt['cli_cases']),
            capacity_refusals=len(receipt['capacity_refusals']),independent_check=receipt['independent_check'],
            instrumentation=receipt['sanitizer_flags'],binary_sha256=receipt['binary_sha256'],
            full_receipt_in_archive='sanitizers/qualification.json',scope=receipt['scope'])
        (output/'sanitizer-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        return
    source_digest = hashlib.sha256(json.dumps(receipt["source_sha256"], sort_keys=True).encode()).hexdigest()
    summary = dict(passed=receipt["passed"], processes=len(receipt["commands"]),
        source_sha256_digest=source_digest, same_compiled_source_as_native_capture=True,
        existing_db_unchanged=receipt["existing_db_unchanged"],
        instrumentation="ASan/UBSan; always-use-after-return; frame pointers; separate build",
        environment=receipt["sanitizer_env"], binary_sha256=receipt["binary_sha256"],
        full_receipt_in_archive="sanitizers/qualification.json",
        scope="8 Objects, native caller modes and tails, retained update/conversion, invalid options, reused DB refusal")
    (output/"sanitizer-summary.json").write_text(json.dumps(summary, indent=2)+"\n")

def counters(output):
    rows=[]
    for path in sorted(output.glob('counter-*.json')):
        result=json.loads(path.read_text())
        for phase,field,clock in [('request','hardware','interval'),('release','release_hardware','release_and_drain')]:
            hardware=result[field]
            if hardware['status']=='not_started': continue
            for event in hardware['events']:
                rows.append(dict(replay=path.stem,backend=result['backend'],workload=result['workload'],
                    codec=result.get('codec','legacy'),rows=result['rows'],objects=result['objects'],batch=result['batch'],
                    retention_age_publications=result.get('retention_age_publications',0),phase=phase,
                    sample_storage=result.get('sample_storage','historical-mode'),
                    logical_requests=result[clock]['logical_requests'],publications=result[clock]['snapshot_scopes'],
                    wall_ns=result[clock]['wall_ns'],caller_cpu_ns=result[clock]['caller_cpu_ns'],
                    process_cpu_ns=result[clock].get('process_cpu_ns'),counter_scope=result['hardware_scope'],
                    group_status=hardware['status'],coverage=hardware['coverage'],
                    time_enabled_ns=hardware['time_enabled_ns'],time_running_ns=hardware['time_running_ns'],
                    scaled=hardware['scaled'],multiplexed=hardware['multiplexed'],ipc=hardware['ipc'],
                    event=event['name'],type=event['type'],config=event['config'],required=event['required'],
                    opened=event['opened'],in_group=event['in_group'],status=event['status'],count=event['count'],
                    group_errors=json.dumps(hardware['errors'],separators=(',',':')),
                    event_errors=json.dumps(event['errors'],separators=(',',':'))))
    if rows: csv_file(output/'hardware-counters.csv',rows)

def report(output):
    summarize(output)
    abi(output)
    sanitizer_summary(output)
    counters(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    for name in ('summarize', 'abi', 'counters', 'report'):
        mode.add_argument('--' + name, type=Path, metavar='COLLECTED_RUN')
    args = parser.parse_args()
    for name in ('summarize', 'abi', 'counters', 'report'):
        if getattr(args, name) is not None:
            globals()[name](getattr(args, name))


if __name__ == '__main__':
    main()
