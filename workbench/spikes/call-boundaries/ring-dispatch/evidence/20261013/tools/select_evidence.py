#!/usr/bin/env python3
"""Select bounded retained evidence from qualified indices and worker archives.

Run after aggregate.py and pmu_audit.py have regenerated the source indices.
This selector reads source files only; it never compiles or runs a benchmark.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil

CURSOR_JOBS = ['20261013T014818Z-c881630c', '20261013T014818Z-0e3eca70', '20261013T015258Z-653c5d1d']
COHORT_JOB = '20261013T015803Z-77b8aeee'
FAILED_JOB = '20261013T020049Z-977506e3'
MARKER_JOBS = ['20261013T013200Z-0865760d', '20261013T013206Z-726128bc', '20261013T013332Z-458c5129']
PMU_JOB = '20261013T021257Z-82cd7665'
GEOMETRIES = {(128, 1), (128, 32), (1048576, 32), (4194304, 8), (4194304, 32)}
PMU_ARMS = {'ring-hints', 'manual-hints'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def write_csv(path, rows):
    keys = list(dict.fromkeys(key for row in rows for key in row))
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=keys)
        writer.writeheader(); writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--review', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--workers', type=Path, default=Path('build/workers'))
    parser.add_argument('--local', type=Path, default=Path('build/loom-ring-review-20261013/streampack-local'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    output = args.output or args.review / 'selected'
    output.mkdir(parents=True, exist_ok=True)
    index = {'format': 1, 'status': 'selected, not yet promoted', 'intended_owner': 'workbench/spikes/call-boundaries/ring-dispatch/evidence/20261013',
             'scope': {'native_geometries': sorted(GEOMETRIES), 'native_arms': 'all eight original controls, seven reps per cell',
                       'cohort': 'same five endpoint geometries on V2, grains 1/2/4; failed Granite has no admitted timing',
                       'marker': 'rejected local C1 hinted StreamPack regression, not a native speed qualification',
                       'pmu': 'ring-hints and manual-hints, modes basic/stalls, sizes 4096/4194304, old/explicit/grain4, five reps'},
             'source_indices': {}, 'jobs': {}, 'files': [], 'limits': []}
    native_path, pmu_path = args.review / 'native-measurements.json', args.review / 'pmu-evidence.json'
    native, pmu = load(native_path), load(pmu_path)
    assert native['status'] == 'validated' and len(pmu['samples']) == 480
    for p in [native_path, pmu_path]:
        index['source_indices'][p.name] = {'sha256': sha(p), 'rebuild_scripts': ['tools/aggregate.py', 'tools/pmu_audit.py']}
    native_jobs = {j['job_id']: j for j in native['jobs']}
    recovery = {}

    def job_receipt(job_id):
        if job_id in recovery:
            return recovery[job_id]
        base = args.workers / job_id
        collection, job = load(base / 'collection.json'), load(base / 'job.json')
        receipt = {'job_id': job_id, 'worker_uri': job['uri'], 'machine': job['config']['machine'],
                   'instance_type': job['config']['instance_type'], 'script': job['script'],
                   'source': job['source'], 'source_commit': job.get('source_commit'),
                   'result_archive': collection['artifact'], 'source_archive_member': 'source.tar.gz',
                   'worker_result': load(base / 'results/worker-result.json'),
                   'local_job_sha256': sha(base / 'job.json'), 'local_collection_sha256': sha(base / 'collection.json'),
                   'selected_members': {}}
        members = collection['members']
        assert members['source.tar.gz']['sha256'] == job['source']['sha256']
        recovery[job_id] = (receipt, members)
        index['jobs'][job_id] = receipt
        return receipt, members

    def worker_file(job_id, name, expected=None, retained=None):
        receipt, members = job_receipt(job_id)
        member = members[name]
        path = args.workers / job_id / 'results' / name
        if path.exists():
            assert sha(path) == member['sha256'], (job_id, name, 'archive manifest differs from local bytes')
        if expected:
            assert member['sha256'] == expected, (job_id, name, 'qualified index differs from recovery member')
        entry = {'sha256': member['sha256'], 'bytes': member['bytes']}
        if retained:
            dest = output / retained; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest); entry['retained_as'] = str(retained)
        receipt['selected_members'][name] = entry
        return path

    native_rows, selected_jobs = [], []
    for job_id in CURSOR_JOBS + [COHORT_JOB]:
        job = native_jobs[job_id]
        assert job['worker_status']['state'] == 'complete'
        item = {k: job[k] for k in ['job_id', 'campaign', 'optimized_ring_sha256', 'source', 'dispatch_sources', 'hardware_profiles', 'qualification', 'isas']}
        item['executable_order'] = job['executable_order']
        item['order_timestamp_decimal_comma_rows_decoded'] = job['order_timestamp_decimal_comma_rows_decoded']
        item['measurement_files'] = []
        for measurement in job['measurement_files']:
            if (measurement['n'], measurement['chains']) not in GEOMETRIES:
                continue
            name = measurement['measurement']
            path = worker_file(job_id, name, measurement['csv_sha256'])
            worker_file(job_id, name.replace('.csv', '.context'), measurement['context_sha256'])
            rows = list(csv.DictReader(path.open()))
            assert len(rows) == measurement['rows'] == 56
            native_rows.extend({'job_id': job_id, 'isa': measurement['isa'], 'variant': measurement['variant'], **row} for row in rows)
            item['measurement_files'].append(measurement)
        item['cells'] = [{k:v for k,v in c.items() if k != 'samples'} for c in job['cells'] if (c['n'], c['chains']) in GEOMETRIES]
        item['selected_rows'] = sum(m['rows'] for m in item['measurement_files'])
        for isa, profile in item['hardware_profiles'].items():
            worker_file(job_id, profile['path'], profile['sha256'])
            worker_file(job_id, 'binary-sha256-' + isa + '.txt')
            worker_file(job_id, 'compile-' + isa + '.json')
        for name, digest in job['files'].items():
            worker_file(job_id, name, digest)
        selected_jobs.append(item)
    assert len(native_rows) == 5040
    write_csv(output / 'native.csv', native_rows)
    write_json(output / 'native.json', {'format': 1, 'jobs': selected_jobs, 'independent_key_set_checksums': native['independent_key_set_checksums'],
                                      'limits': native['limits'], 'selected_raw_rows': len(native_rows), 'raw_rows': 'native.csv'})

    failed = native_jobs[FAILED_JOB]
    assert failed['worker_status']['state'] == 'failed' and not failed['cells']
    worker_file(FAILED_JOB, 'worker-result.json', retained=Path('failed-granite/worker-result.json'))
    worker_file(FAILED_JOB, 'build-asan-avx2.log', retained=Path('failed-granite/build-asan-avx2.txt'))
    obsolete=output/'failed-granite/build-asan-avx2.log'
    if obsolete.exists():
        assert sha(obsolete)==sha(output/'failed-granite/build-asan-avx2.txt')
        obsolete.unlink()
    failure_log = (output / 'failed-granite/build-asan-avx2.txt').read_text()
    assert 'Stack realignment in presence of dynamic allocas is not supported with this calling convention.' in failure_log
    write_json(output / 'failed-granite/qualification.json', {'job_id': FAILED_JOB, 'qualification': failed['qualification'],
        'worker_status': failed['worker_status'], 'admitted_measurements': 0,
        'failure': 'Clang ASan compilation of Ring rotation check failed on stack realignment with preserve calling convention; no native cohort timings qualified.'})

    marker_discrepancies = []
    for job_id in MARKER_JOBS:
        job = native_jobs[job_id]
        discrepancy = job['provenance_discrepancy']
        assert discrepancy['actual_captured_and_runtime_ring_sha256'].startswith('bc98eb245dda')
        marker_discrepancies.append({'job_id':job_id,'source_note_discrepancy':discrepancy,'qualified_timing_rows_retained':0})
        for name in ['dispatch-sources.json','source-provenance.json','source-manifest.json']:
            worker_file(job_id,name,job['files'][name])
    local = args.local
    focus = load(local / 'focus-c1-comparison.json')
    receipt = load(local / 'build-receipt.json')
    assert focus['status'] == 'complete' and receipt['new_ring_sha256'].startswith('bc98eb245dda')
    assert receipt['source_difference'] == ['loom/include/loom/ring.h']
    local_receipts, local_rows = [], []
    for process in focus['process_order']:
        directory = local / process
        for path in sorted(directory.glob('*.txt')):
            dest = Path('marker-rejected/local') / process / path.name
            destination = output / dest; destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,destination)
            lines = path.read_text().splitlines()
            config = dict(token.split('=',1) for token in lines[0].split())
            assert config['width'] == '1' and config['hints'] == '1' and config['requests'] == '65536'
            samples = [dict(token.split('=',1) for token in line.split()) for line in lines if line.startswith('sample=')]
            assert len(samples) == 20 and {int(s['sample']) for s in samples} == set(range(20))
            for sample in samples:
                for key,value in focus['matrix'][0]['counts'].items():
                    assert int(sample[key]) == value
                local_rows.append({'process':process,'cut':'old' if '-old-' in process else 'marker', 'family':config['family'],'route':config['route'], **sample})
            local_receipts.append({'source':str(path),'retained_as':str(dest),'sha256':sha(path)})
        for name in ['measure.json','source-receipt','cases.json']:
            path=directory/name; dest=Path('marker-rejected/local')/process/name
            shutil.copyfile(path,output/dest)
            local_receipts.append({'source':str(path),'retained_as':str(dest),'sha256':sha(path)})
    assert len(local_rows) == 320
    write_csv(output/'marker-rejected/local.csv',local_rows)
    write_json(output/'marker-rejected/summary.json', {'format':1,'status':'rejected marker control','source':receipt,
        'focused_comparison':focus,'source_note_discrepancies':marker_discrepancies,'raw_local_files':local_receipts,
        'raw_rows':'local.csv','local_rows':len(local_rows),
        'limits':['Local OrbStack VM screen, not native performance qualification.','No remote archive was made for this local screen; selected raw logs are retained here.',
                  'Source archives of native marker jobs contain the exact bc98 header; copied source note described a different later layout hash.']})

    selected_samples = [s for s in pmu['samples'] if s['execution'] in PMU_ARMS]
    assert len(selected_samples) == 120
    for sample in selected_samples:
        worker_file(PMU_JOB,sample['file'],sample['sha256'],Path('pmu/raw')/sample['file'])
    for name,digest in pmu['files'].items():
        worker_file(PMU_JOB,name,digest)
    for mode in ['basic','stalls']:
        for name in ['probe-'+mode+'.json','probe-'+mode+'-status.json']:
            worker_file(PMU_JOB,name,pmu['files'][name],Path('pmu/capability')/name)
    write_csv(output/'pmu.csv',selected_samples)
    write_json(output/'pmu.json',{'format':1,'job_id':PMU_JOB,'qualification':pmu['qualification'], 'sources':pmu['sources'],
        'hardware':pmu['hardware'],'driver_compiles':pmu['driver_compiles'],'executable_order':pmu['executable_order'],
        'probes':pmu['probes'],'cells':[c for c in pmu['cells'] if c['execution'] in PMU_ARMS],
        'selected_samples':len(selected_samples),'raw_receipts':'pmu/raw','raw_sample_metrics':'pmu.csv','limits':pmu['limits'],
        'nonselected_arms':'All other arms remain recoverable from the same result archive and indexed whole-worker CSV members.'})
    # Keep a small static instruction audit, not multi-megabyte disassemblies.
    codegen=args.review/'native-codegen.json'
    shutil.copyfile(codegen,output/'native-codegen.json')
    for script in ['aggregate.py','pmu_audit.py','select_evidence.py']:
        dest=output/'tools'/script;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(args.review/script,dest)
    index['limits']=['Selection retains endpoint controls rather than every successful run.',
                     'Marker native measurements are referenced only as rejected provenance controls; no marker speed claims retained.',
                     'PMU hardware intervals and uninstrumented throughput intervals remain separate.',
                     'Future adapter jobs must be appended explicitly after their own qualification; none inferred from cohort data.']
    for path in sorted(output.rglob('*')):
        if path.is_file() and path.name!='recovery.json':
            index['files'].append({'path':str(path.relative_to(output)),'sha256':sha(path),'bytes':path.stat().st_size})
    index['counts']={'native_rows':len(native_rows),'native_summary_cells':sum(len(j['cells']) for j in selected_jobs),
                     'local_marker_rows':len(local_rows),'pmu_raw_receipts':len(selected_samples)}
    write_json(output/'recovery.json',index)
    print(output,'selected bytes',sum(f['bytes'] for f in index['files']),'files',len(index['files']),index['counts'])


if __name__=='__main__':
    main()
