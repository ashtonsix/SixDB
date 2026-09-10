#!/usr/bin/env python3
"""Validate complete-window quality rows and summarize a fixed storage decision."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path

VARIANTS = {(model, scan) for model in ('cheap', 'quadrants') for scan in ('full', 'sample32')}
COLUMNS = ['corpus', 'archive', 'family', 'partition', 'ordinal', 'population',
           'model', 'scan', 'actual_body_bytes', 'predicted_body_bytes']
PLAIN_BYTES = 8192
SUFFIX_BYTES = 64
ALLOCATION_ALIGNMENT = 64
BODY_MAX = 12032
BYTE_SCOPES = ('readable_extent', 'aligned_allocation')


def total_bytes(body, metadata, byte_scope):
    extent = body + SUFFIX_BYTES
    if byte_scope == 'aligned_allocation':
        extent = ((extent + ALLOCATION_ALIGNMENT - 1) // ALLOCATION_ALIGNMENT) * ALLOCATION_ALIGNMENT
    elif byte_scope != 'readable_extent':
        raise ValueError(f'Unknown byte scope: {byte_scope}')
    return metadata + extent


def family_for(corpus, archive):
    if corpus == 'msmarco-keyset':
        return 'msmarco'
    if corpus == 'real-roaring':
        return 'dimension' if archive.startswith('dimension_') else archive.removesuffix('_srt')
    raise ValueError(f'Unknown natural corpus: {corpus}')


def partition_for(family):
    if family == 'census-income':
        return 'test_comparison'
    if family == 'census1881':
        return 'validation_comparison'
    if family in ('dimension', 'uscensus2000', 'weather_sept_85', 'wikileaks-noquotes', 'msmarco'):
        return 'train_family_comparison'
    raise ValueError(f'Unknown source family: {family}')


def percentile(counter, numerator=95, denominator=100):
    count = sum(counter.values())
    if not count:
        return ''
    rank = (count * numerator + denominator - 1) // denominator
    cumulative = 0
    for value, frequency in sorted(counter.items()):
        cumulative += frequency
        if cumulative >= rank:
            return value
    raise AssertionError('Empty percentile')


class Stats:
    def __init__(self):
        self.totals = Counter()
        self.errors = Counter()
        self.near_errors = Counter()

    def add(self, actual, predicted, metadata, byte_scope):
        total = total_bytes(actual, metadata, byte_scope)
        predicted_total = total_bytes(predicted, metadata, byte_scope)
        convert = predicted_total < PLAIN_BYTES
        beneficial = total < PLAIN_BYTES
        false = convert and not beneficial
        missed = not convert and beneficial
        chosen = total if convert else PLAIN_BYTES
        best = min(total, PLAIN_BYTES)
        error = predicted - actual
        near = abs(total - PLAIN_BYTES) <= 512
        self.errors[error] += 1
        if near:
            self.near_errors[error] += 1
        self.totals.update({
            'windows': 1,
            'actual_body_bytes_sum': actual,
            'predicted_body_bytes_sum': predicted,
            'actual_total_bytes_sum': total,
            'predicted_total_bytes_sum': predicted_total,
            'predicted_conversions': int(convert),
            'actual_beneficial_windows': int(beneficial),
            'false_conversions': int(false),
            'missed_savings_windows': int(missed),
            'false_conversion_excess_bytes': total - PLAIN_BYTES if false else 0,
            'missed_savings_bytes': PLAIN_BYTES - total if missed else 0,
            'excess_actual_bytes_over_best': chosen - best,
            'chosen_actual_bytes': chosen,
            'best_actual_bytes': best,
            'near_crossover_windows': int(near),
            'near_false_conversions': int(near and false),
            'near_missed_savings_windows': int(near and missed),
            'near_excess_actual_bytes_over_best': chosen - best if near else 0,
        })

    @staticmethod
    def error_fields(errors, prefix=''):
        n = sum(errors.values())
        absolute = Counter()
        for error, frequency in errors.items():
            absolute[abs(error)] += frequency
        return {prefix + key: value for key, value in {
            'signed_error_mean_bytes': sum(e * n for e, n in errors.items()) / n if n else '',
            'mae_bytes': sum(e * n for e, n in absolute.items()) / n if n else '',
            'p95_absolute_error_bytes': percentile(absolute),
            'max_absolute_error_bytes': max(absolute) if n else '',
            'min_signed_error_bytes': min(errors) if n else '',
            'max_signed_error_bytes': max(errors) if n else '',
        }.items()}

    def row(self):
        result = dict(self.totals)
        result.update(self.error_fields(self.errors))
        result.update(self.error_fields(self.near_errors, 'near_'))
        n = result['windows']
        result['actual_total_bytes_mean'] = result['actual_total_bytes_sum'] / n
        result['predicted_total_bytes_mean'] = result['predicted_total_bytes_sum'] / n
        result['false_conversion_fraction'] = result['false_conversions'] / n
        result['missed_savings_fraction'] = result['missed_savings_windows'] / n
        result['mean_excess_actual_bytes_over_best'] = result['excess_actual_bytes_over_best'] / n
        if result['excess_actual_bytes_over_best'] != (
                result['false_conversion_excess_bytes'] + result['missed_savings_bytes']):
            raise AssertionError('Decision loss accounting')
        return result


def validate_input_counts(directory, counts):
    """When retained inputs are present, compare every archive with its recipe census."""
    checked = []
    for corpus, section in (('real-roaring', 'datasets'), ('msmarco-keyset', 'terms')):
        path = directory / 'inputs' / corpus / 'prepared.json'
        if not path.exists():
            path = directory / (corpus + '-prepared.json')
        if not path.exists():
            continue
        meta = json.loads(path.read_text())
        if meta['request']['name'] != corpus or meta['request']['parameters']['min_cardinality'] != 1:
            raise ValueError(f'{path}: expected the complete min_cardinality=1 preparation')
        expected = {name: item['windows'] for name, item in meta['details'][section].items()}
        actual = {name: n for (source, name), n in counts.items() if source == corpus}
        if actual != expected:
            raise ValueError(f'{corpus}: CSV counts differ from the prepared input census')
        checked.append(corpus)
    return checked


def summarize(directory):
    stats = defaultdict(Stats)
    counts = Counter()
    family_counts = Counter()
    corpus_counts = Counter()
    next_ordinal = Counter()
    identity = {}
    row_count = 0
    with (directory / 'quality.csv').open(newline='') as source:
        reader = csv.DictReader(source)
        if reader.fieldnames != COLUMNS:
            raise ValueError('Unexpected quality CSV columns')
        for first in reader:
            rows = [first]
            for _ in range(3):
                row = next(reader, None)
                if row is None:
                    raise ValueError('Truncated four-variant window')
                rows.append(row)
            row_count += 4
            for row in rows:
                for key in ('ordinal', 'population', 'actual_body_bytes', 'predicted_body_bytes'):
                    row[key] = int(row[key])
            shared = ('corpus', 'archive', 'family', 'partition', 'ordinal', 'population', 'actual_body_bytes')
            if any(tuple(row[key] for key in shared) != tuple(first[key] for key in shared) for row in rows):
                raise ValueError(f'Window identity/actual size differs among variants: {first}')
            if {(row['model'], row['scan']) for row in rows} != VARIANTS:
                raise ValueError(f'Missing/duplicate model or scan: {first}')
            corpus, archive, family, partition = (first[key] for key in shared[:4])
            if corpus == 'structural':
                if partition != 'structural_comparison':
                    raise ValueError('Structural rows must remain separate')
            elif family != family_for(corpus, archive) or partition != partition_for(family):
                raise ValueError(f'Wrong source family/partition: {first}')
            archive_key = (corpus, archive)
            if first['ordinal'] != next_ordinal[archive_key]:
                raise ValueError(f'Noncontiguous or repeated ordinal: {first}')
            next_ordinal[archive_key] += 1
            if not 0 <= first['population'] <= 65536:
                raise ValueError(f'Invalid population: {first}')
            if corpus != 'structural' and first['population'] == 0:
                raise ValueError('Prepared source windows must be nonempty')
            if not 0 <= first['actual_body_bytes'] <= BODY_MAX:
                raise ValueError(f'Invalid actual body size: {first}')
            if first['population'] in (0, 65536) and first['actual_body_bytes'] != 0:
                raise ValueError('Uniform window must have no BEC body bytes')
            if archive_key in identity and identity[archive_key] != (family, partition):
                raise ValueError('Archive changed family/partition')
            identity[archive_key] = (family, partition)
            counts[archive_key] += 1
            family_counts[(corpus, family, partition)] += 1
            corpus_counts[corpus] += 1
            for row in rows:
                if not 0 <= row['predicted_body_bytes'] <= BODY_MAX:
                    raise ValueError(f'Invalid prediction: {row}')
                for metadata in (512, 1024):
                    for scope, name in (('archive', archive), ('family', family), ('corpus', corpus)):
                        # Corpus summaries can span the three previously used
                        # partitions; archive/family rows retain their partition.
                        group_partition = 'mixed_comparison' if scope == 'corpus' and corpus == 'real-roaring' else partition
                        for byte_scope in BYTE_SCOPES:
                            key = (scope, corpus, name, group_partition, row['model'], row['scan'], metadata, byte_scope)
                            stats[key].add(row['actual_body_bytes'], row['predicted_body_bytes'], metadata, byte_scope)
    for corpus, expected in (('real-roaring', 12), ('msmarco-keyset', 16)):
        if sum(source == corpus for source, _ in counts) != expected:
            raise ValueError(f'Expected all {expected} archives from {corpus}')
    expected_structural = {
        'random-dense-prefix': 257, 'random-dense-permuted': 257, 'repeated-byte-prefix': 257,
        'sample-phase-dense': 8, 'sample-phase-zero': 8,
        'repeated-byte-homogeneous': 256, 'half-window-per-tile-rotation': 32,
    }
    if {name: n for (source, name), n in counts.items() if source == 'structural'} != expected_structural:
        raise ValueError('Structural scenario inventory/counts differ from the fixed experiment')
    checked = validate_input_counts(directory, counts)
    results = []
    for (scope, corpus, name, partition, model, scan, metadata, byte_scope), values in sorted(stats.items()):
        fields = values.row()
        results.append(dict(scope=scope, corpus=corpus, name=name, partition=partition,
                            weighting='window_occurrence', corpus_windows=corpus_counts[corpus],
                            occurrence_weight_within_corpus=fields['windows'] / corpus_counts[corpus],
                            model=model, scan=scan, byte_scope=byte_scope,
                            metadata_bytes=metadata, suffix_bytes=SUFFIX_BYTES,
                            plain_bytes=PLAIN_BYTES, minimum_saving_bytes=0, **fields))
    if sum(counts.values()) * 4 != row_count:
        raise AssertionError('Row count accounting')
    return results, counts, family_counts, checked, row_count


def write_report(directory):
    rows, counts, families, checked, row_count = summarize(directory)
    with (directory / 'quality-summary.csv').open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    text = [
        '# Whole-window BEC decision comparison', '',
        f'{row_count:,} rows validated: {sum(counts.values()):,} windows, exactly four frozen-model/scan variants per window. '
        'Every supplied nonempty source window includes all 256 tiles, including empty/full tiles. '
        'Absent all-zero source windows are not synthesized.', '',
        'Each window occurrence has weight one. Archive/family weights in the CSV are fractions of their corpus; '
        'families are not balanced and the two natural corpora are not pooled. Structural cases are a fixed comparison '
        'suite, not an estimated natural frequency distribution. The full per-archive and per-family statistics are in '
        '[quality-summary.csv](quality-summary.csv).', '',
        'Natural partitions retain the original model lineage: census-income is `test_comparison`, census1881 is '
        '`validation_comparison`, and the remaining families/MS MARCO are `train_family_comparison`. All these supplied '
        'families were previously inspected; this is not a fresh holdout and no model or decision margin is fitted here.', '',
        'Each comparison has two explicit byte scopes. `readable_extent` totals metadata + body + 64, the owned readable '
        'extent before allocation rounding. `aligned_allocation` totals metadata + 64 × ceil((body + 64) / 64), '
        'matching the body owner’s 64-byte allocation rounding. Metadata is already a multiple of 64: 512 bytes for '
        'the two packed layouts or 1,024 bytes for direct32. Neither scope includes allocator bookkeeping or '
        'object/control storage; no per-tile 32-byte cap, raw escape or tag is applied.', '',
        'In each scope, convert exactly when its predicted total is < 8,192, with the same fixed zero margin. '
        'The actual best choice is the smaller of the actual total in that scope and 8,192 plain bytes. '
        'False conversions include an actual tie (zero excess bytes); missed savings require an actually smaller total. '
        'Decision loss is chosen actual bytes minus that best actual choice, summed over occurrences. '
        'The CSV includes actual and predicted total-byte sums and means separately for each scope.', '',
        'Signed error is predicted minus actual body bytes. P95 is the nearest-rank 95th percentile of absolute error; '
        'these body-error statistics are unchanged between byte scopes. The near-crossover subset satisfies '
        '|actual total in the stated byte scope − 8,192| ≤ 512, so its membership and errors can change with rounding. '
        'Empty subsets have blank error fields. '
        'The sample estimate is eight times the rounded per-tile predictions from the fixed 32 strata; it is not a bound.', '',
        ('Prepared census cross-check: ' + ', '.join(checked) + '.' if checked else
         'Prepared manifests are absent here: ordinal continuity, archive inventory and four-row agreement were checked, '
         'but the original preparation census was not independently rechecked by this report.'), '',
        '| Corpus / family | Partition | Window occurrences |',
        '|---|---|---:|',
    ]
    for (corpus, family, partition), count in sorted(families.items()):
        text.append(f'| {corpus} / {family} | {partition} | {count:,} |')
    text += ['', '| Corpus | Model / scan | Byte scope | Metadata B | Body MAE B | Body P95 B | Body max B | False conversions | Missed savings | Excess actual B | Near windows / body MAE B |',
             '|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for row in rows:
        if row['scope'] != 'corpus':
            continue
        near = row['near_mae_bytes']
        near_text = f'{near:.2f}' if near != '' else '—'
        text.append(f"| {row['corpus']} | {row['model']} / {row['scan']} | {row['byte_scope']} | {row['metadata_bytes']} | "
                    f"{row['mae_bytes']:.2f} | {row['p95_absolute_error_bytes']} | {row['max_absolute_error_bytes']} | "
                    f"{row['false_conversions']:,} | {row['missed_savings_windows']:,} | "
                    f"{row['excess_actual_bytes_over_best']:,} | {row['near_crossover_windows']:,} / {near_text} |")
    text += ['', 'Structural construction is fixed in quality.cpp: all 257 dense-tile counts with random prefix/permuted '
             'placement and repeated 0x55 bytes; eight sample-mask phases and their inverses; all 256 homogeneous byte '
             'values; and 32 rotations of a contiguous half-full tile. Dense random bytes use SplitMix64 seed '
             '`0x6b77696e646f7731`. The prefix/permutation pair has identical full-scan targets and estimates, while '
             'sample selection can change. This suite includes both sample underestimation and overestimation; '
             'its count sweep was not narrowed using an observed crossover.', '']
    (directory / 'quality-summary.md').write_text('\n'.join(text))
    print(f'Validated {row_count:,} rows / {sum(counts.values()):,} windows; wrote {len(rows)} summary rows.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_directory', type=Path)
    args = parser.parse_args()
    write_report(args.run_directory)


if __name__ == '__main__':
    main()
