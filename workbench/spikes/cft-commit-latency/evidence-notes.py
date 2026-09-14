#!/usr/bin/env python3
"""Record analysis identities, counts and recovery comparisons for the selected evidence."""
import argparse,csv,gzip,hashlib,io,json,tarfile
from pathlib import Path
BASE=Path(__file__).resolve().parent


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args();root=a.directory
 analyzers=['summarize.py','persistence/analyze.py','persistence/diagnostic_analyze.py','commit/analyze.py','commit/throughput_summary.py','commit/throughput_analyze.py','commit/network_summary.py','recover-durable.py','plot-durable.py']
 # Keep the exact analysis source in the full recovery bundle, without duplicating it in Git evidence.
 with (root/'analysis-source.tar.gz').open('wb') as raw:
  with gzip.GzipFile(fileobj=raw,mode='wb',filename='',mtime=0) as compressed:
   with tarfile.open(fileobj=compressed,mode='w') as archive:
    for file in analyzers:
     payload=(BASE/file).read_bytes();entry=tarfile.TarInfo(file);entry.size=len(payload);entry.mtime=0;entry.mode=0o644
     archive.addfile(entry,io.BytesIO(payload))
 data=dict(quantiles='Hyndman–Fan type 7 linear interpolation of recorded samples; pooled percentiles and independent-pass spread are separate.',
           units='Latency fields ending _us are microseconds; raw timestamps _ns are nanoseconds; rates are unique 4096-byte records per second; Unix timestamps are seconds.',
           analyzers={file:digest(BASE/file) for file in analyzers},counts={})
 data['analysis_source_archive_sha256']=digest(root/'analysis-source.tar.gz')
 for path in sorted(root.rglob('*.csv')):
  if path.name not in ['persistence.csv','d3-diagnostic.csv','commits.csv','throughput.csv']:continue
  rows=list(csv.DictReader(path.open()));data['counts'][str(path.relative_to(root))]=dict(configurations=len(rows),samples=sum(int(r['samples']) for r in rows))
 data['selected_numeric_sha256']={str(p.relative_to(root)):digest(p) for p in sorted(root.rglob('*.csv'))}
 (root/'analysis.json').write_text(json.dumps(data,indent=2)+'\n')


if __name__=='__main__':main()
