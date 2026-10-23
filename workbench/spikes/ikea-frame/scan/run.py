#!/usr/bin/env python3
"""Same-population complete scans through actual Frame/native/Polars calls."""
import os
os.environ['POLARS_MAX_THREADS']='1'
from pathlib import Path
import argparse,hashlib,json,random,statistics,time
from importlib import metadata as packages

p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True)
p.add_argument('--inputs',type=Path,required=True);p.add_argument('--metadata',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--cpu',type=int,required=True)
p.add_argument('--rows',default='65536,524288,2999808');p.add_argument('--trials',type=int,default=8)
p.add_argument('--columns',default='OS,ResolutionWidth,EventTime,UserID,URLHash,WatchID')
a=p.parse_args();os.sched_setaffinity(0,{a.cpu})
# Set affinity before either native library can create a worker thread.
import numpy as np
import polars as pl
from bridge import Library
assert pl.thread_pool_size()==1
assert a.trials>0
a.output.mkdir(parents=True,exist_ok=False);lib=Library(a.library.resolve())
metadata=json.loads(a.metadata.read_text());rng=random.Random(1729)
runtime=[]
for package in packages.distributions():
    if package.metadata['Name'].startswith('polars-runtime'):
        for file in package.files or []:
            if str(file).endswith('.so'):
                path=Path(package.locate_file(file))
                with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
                runtime.append(dict(package=package.metadata['Name'],version=package.version,
                    file=str(file),sha256=digest,bytes=path.stat().st_size))
assert runtime,'actual Polars native runtime was not identified'
line_file=Path(f'/sys/devices/system/cpu/cpu{a.cpu}/cache/index0/coherency_line_size')
line=int(line_file.read_text());assert line and line&(line-1)==0
flush_available=bool(lib.flush_supported());conditions=['warm-repeat']+(['flushed-image'] if flush_available else [])
identity={'polars':pl.__version__,'threads':pl.thread_pool_size(),'cpu':a.cpu,'cache_line':line,
    'flush_available':flush_available,'source_commit':os.environ.get('SIXDB_SOURCE_COMMIT'),
    'library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),
    'polars_runtime':runtime,'numpy':np.__version__,
    'conditions':conditions,'metadata':metadata,'rows_requested':a.rows,'inputs':{},
    'scope':'Prepared complete Python calls including scalar extraction. No measured DRAM-traffic claim.',
    'python':__import__('sys').version,'platform':__import__('platform').platform(),
    'cold_scope':'Frame payload/descriptors/parts/coded arrays or actual Polars numeric buffers. Prepared selections remain warm.'}
(a.output/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')

def schedule(count):
    labels=list(range(count));rng.shuffle(labels)
    first=[0]+[(i+1)//2 if i%2 else count-i//2 for i in range(1,count)]
    rows=[]
    for shift in range(count):
        row=[labels[(v+shift)%count] for v in first];rows.append(row)
        if count%2:rows.append(row[::-1])
    # Mathematical inventory, including the predecessor balance missing from
    # the old cyclic timing rotation. Repeated labels are separate treatments.
    positions=np.zeros((count,count),dtype=np.int64);pairs=positions.copy()
    for row in rows:
        assert set(row)==set(range(count))
        for i,v in enumerate(row):
            positions[v,i]+=1
            if i:pairs[row[i-1],v]+=1
    target=2 if count%2 else 1
    assert np.all(positions==target)
    assert np.all(pairs+np.eye(count,dtype=np.int64)*target==target)
    rng.shuffle(rows);return rows

def numeric_ranges(series):
    view=series.to_numpy(allow_copy=False)
    return [(int(view.ctypes.data),int(view.nbytes),0)],view

records=[];setups=[]
raw_file=(a.output/'samples.jsonl').open('w')
def measure(label,rows,layout,operation,selection,arms,wanted):
    for arm in arms:assert arm['fn']()==wanted,(label,operation,selection,arm['name'])
    order=schedule(len(arms));trials=(a.trials+len(order)-1)//len(order)*len(order)
    for condition in conditions:
        for trial in range(trials):
            if trial%len(order)==0:order=schedule(len(arms))
            for position,index in enumerate(order[trial%len(order)]):
                arm=arms[index]
                flushed_lines=0
                if condition=='warm-repeat':assert arm['fn']()==wanted
                else:
                    flushed_lines=lib.condition(arm['ranges'],line)
                    assert flushed_lines>0
                start=time.perf_counter_ns();answer=arm['fn']();elapsed=time.perf_counter_ns()-start
                assert answer==wanted,(label,arm['name'],condition)
                record=dict(input=label,rows=rows,layout=layout,operation=operation,selection=selection,
                    condition=condition,arm=arm['name'],trial=trial,position=position,ns=elapsed,
                    flushed_lines=flushed_lines,answer=str(answer))
                raw_file.write(json.dumps(record)+'\n');records.append(record)
        raw_file.flush()

def sum_dtype(series,selected_rows):
    """Prove the reduction bound before choosing Polars' accumulator width.

    Int8/16 and UInt8/16 already widen to Int64. Int32/UInt32 do not;
    neither does a 64-bit SUM. A wide input alone does not require Int128.
    """
    lo,hi=int(series.min()),int(series.max())
    bound=selected_rows*max(abs(lo),abs(hi))
    if series.dtype in (pl.Int8,pl.Int16,pl.UInt8,pl.UInt16):return None,bound
    limit=(1<<32)-1 if series.dtype==pl.UInt32 else (1<<31)-1 if series.dtype==pl.Int32 else (1<<64)-1 if series.dtype==pl.UInt64 else (1<<63)-1
    if bound<=limit:return None,bound
    return (pl.UInt64 if lo>=0 and bound<=(1<<64)-1 else pl.Int64 if bound<=(1<<63)-1 else pl.Int128),bound

def aggregate(series,dtype):
    return int((series.cast(dtype) if dtype is not None else series).sum())

for name in a.columns.split(','):
    path=a.inputs/(name+'.u64');raw=np.memmap(path,dtype='<u8',mode='r')
    spec=metadata[name];signed=spec['mapping']=='offset-binary';bias=(1<<63) if signed else 0
    assert path.stat().st_size%8==0 and len(raw)>=256
    with path.open('rb') as f:input_sha=hashlib.file_digest(f,'sha256').hexdigest()
    assert len(raw)==spec['rows']
    identity['inputs'][name]={'sha256':input_sha,'bytes':path.stat().st_size}
    for rows in sorted({min(int(n),len(raw))//256*256 for n in a.rows.split(',')}):
        assert rows>0
        original=np.asarray(raw[:rows]);blocks=rows//256;selected_blocks=max(1,blocks//16)
        # Move the same selected original blocks to every16th position. The
        # paired layouts select exactly the same logical values, not merely
        # equal densities with accidentally different predicate populations.
        layouts=[('original',original,list(range(selected_blocks)))]
        permutation=np.empty(blocks,dtype=np.int64);dest=np.arange(selected_blocks)*16
        permutation[dest]=np.arange(selected_blocks);rest=np.ones(blocks,dtype=bool);rest[dest]=False
        permutation[rest]=np.arange(selected_blocks,blocks)
        layouts.append(('block-scattered',np.ascontiguousarray(original.reshape(blocks,256)[permutation]).reshape(-1),dest.tolist()))
        selected_sha=None
        for layout,values,selected in layouts:
            prepared=time.perf_counter_ns();image=lib.image(values);encode_ns=time.perf_counter_ns()-prepared
            try:
                logical=((values^np.uint64(bias)).view(np.int64) if signed else values).astype(np.dtype(spec['dtype'].lower()))
                series=pl.Series('x',logical);assert series.n_chunks()==1
                ranges,keep=numeric_ranges(series)
                stats=image.stats();residual=(values-np.uint64(stats['minimum'])).astype(f'uint{stats["native_lane"]*8}')
                for_series=pl.Series('r',residual);for_ranges,for_keep=numeric_ranges(for_series)
                full=np.full((blocks+63)//64,np.uint64((1<<64)-1),dtype=np.uint64)
                if blocks%64:full[-1]=np.uint64((1<<(blocks%64))-1)
                if layout=='original':
                    image.mask(full);threshold=int(np.partition(values.copy(),rows//2)[rows//2])
                    expected=int(np.count_nonzero(values>=threshold));rhs=threshold-stats['minimum']
                    logical_threshold=threshold-bias
                    lazy=series.to_frame().lazy().select((pl.col('x')>=pl.lit(logical_threshold,dtype=series.dtype)).sum())
                    for_lazy=for_series.to_frame().lazy().select((pl.col('r')>=pl.lit(rhs,dtype=for_series.dtype)).sum())
                    def polars_for():
                        return rows if rhs<=0 else 0 if threshold>stats['maximum'] else int((for_series>=rhs).sum())
                    frame=lambda:image.query(0,1,threshold);native=lambda:image.query(1,1,threshold)
                    arms=[dict(name='frame',fn=frame,ranges=image.ranges(0)),
                          dict(name='frame-catalogue',fn=lambda:image.query(2,1,threshold),ranges=image.ranges(2)),
                          dict(name='frame-catalogue-duplicate',fn=lambda:image.query(2,1,threshold),ranges=image.ranges(2)),
                          dict(name='native-for',fn=native,ranges=image.ranges(1)),
                          dict(name='polars-series',fn=lambda:int((series>=logical_threshold).sum()),ranges=ranges),
                          dict(name='polars-lazy',fn=lambda:int(lazy.collect(engine='in-memory').item()),ranges=ranges),
                          dict(name='polars-for',fn=polars_for,ranges=for_ranges),
                          dict(name='polars-for-lazy',fn=lambda:rows if rhs<=0 else int(for_lazy.collect(engine='in-memory').item()),ranges=for_ranges),
                          dict(name='frame-duplicate',fn=frame,ranges=image.ranges(0)),
                          dict(name='polars-for-duplicate',fn=polars_for,ranges=for_ranges)]
                    measure(name,rows,layout,'count-ge','all',arms,expected)
                    dtype,full_bound=sum_dtype(series,rows)
                    for_dtype,for_bound=sum_dtype(for_series,rows)
                    # Python integers are the independent exact oracle. Mapping
                    # the working domain back subtracts one bias per selected row.
                    full_wanted=sum(int(v) for v in logical)
                    lazy_sum=series.to_frame().lazy().select(
                        (pl.col('x').cast(dtype) if dtype is not None else pl.col('x')).sum())
                    frame=lambda:image.query(0,0)-rows*bias
                    native=lambda:image.query(1,0)-rows*bias
                    polars_sum=lambda:aggregate(series,dtype)
                    polars_for_sum=lambda:aggregate(for_series,for_dtype)+rows*(stats['minimum']-bias)
                    for_lazy_sum=for_series.to_frame().lazy().select(
                        (pl.col('r').cast(for_dtype) if for_dtype is not None else pl.col('r')).sum())
                    arms=[dict(name='frame',fn=frame,ranges=image.ranges(0)),
                          dict(name='frame-catalogue',fn=lambda:image.query(2,0)-rows*bias,ranges=image.ranges(2)),
                          dict(name='frame-catalogue-duplicate',fn=lambda:image.query(2,0)-rows*bias,ranges=image.ranges(2)),
                          dict(name='native-for',fn=native,ranges=image.ranges(1)),
                          dict(name='polars-series',fn=polars_sum,ranges=ranges),
                          dict(name='polars-lazy',fn=lambda:int(lazy_sum.collect(engine='in-memory').item()),ranges=ranges),
                          dict(name='polars-for',fn=polars_for_sum,ranges=for_ranges),
                          dict(name='polars-for-lazy',fn=lambda:int(for_lazy_sum.collect(engine='in-memory').item())+rows*(stats['minimum']-bias),ranges=for_ranges),
                          dict(name='frame-duplicate',fn=frame,ranges=image.ranges(0)),
                          dict(name='polars-for-duplicate',fn=polars_for_sum,ranges=for_ranges)]
                    measure(name,rows,layout,'sum','all',arms,full_wanted)
                    full_law=dict(polars_sum_cast=str(dtype),polars_sum_bound=str(full_bound),
                        polars_for_sum_cast=str(for_dtype),polars_for_sum_bound=str(for_bound))
                else:full_law={}
                start=time.perf_counter_ns()
                words=np.zeros((blocks+63)//64,dtype=np.uint64)
                for b in selected:words[b//64]|=np.uint64(1)<<np.uint64(b%64)
                mask_build_ns=time.perf_counter_ns()-start
                start=time.perf_counter_ns();image.mask(words);mask_bind_ns=time.perf_counter_ns()-start
                start=time.perf_counter_ns()
                ix=np.concatenate([np.arange(b*256,(b+1)*256,dtype=np.uint32) for b in selected])
                indices=pl.Series('indices',ix)
                index_bind_ns=time.perf_counter_ns()-start
                # Independent Python integer oracle; no numpy fixed-width SUM.
                wanted=sum(int(v) for v in logical[ix]);dtype,bound=sum_dtype(series,len(ix))
                digest=hashlib.sha256(logical[ix].tobytes()).hexdigest()
                if selected_sha is None:selected_sha=digest
                else:assert digest==selected_sha,'layouts selected different logical values'
                def bind_spans():
                    # A contiguous selected prefix is one slice, rather than
                    # hundreds of artificially separate one-block chunks.
                    spans=[];first=previous=selected[0]
                    for b in selected[1:]:
                        if b!=previous+1:
                            spans.append((first*256,(previous-first+1)*256));first=b
                        previous=b
                    spans.append((first*256,(previous-first+1)*256))
                    chunks=[series.slice(first,count) for first,count in spans]
                    return chunks[0] if len(chunks)==1 else pl.concat(chunks,rechunk=False)
                start=time.perf_counter_ns();chunks=bind_spans();span_bind_ns=time.perf_counter_ns()-start
                span_lazy=chunks.to_frame().lazy().select(
                    (pl.col('x').cast(dtype) if dtype is not None else pl.col('x')).sum())
                frame=lambda:image.query(0,0)-len(ix)*bias
                native=lambda:image.query(1,0)-len(ix)*bias
                gather=lambda:aggregate(series.gather(indices),dtype)
                span=lambda:aggregate(chunks,dtype)
                span_request=lambda:aggregate(bind_spans(),dtype)
                arms=[dict(name='frame',fn=frame,ranges=image.ranges(0)),
                      dict(name='frame-catalogue',fn=lambda:image.query(2,0)-len(ix)*bias,ranges=image.ranges(2)),
                      dict(name='frame-catalogue-duplicate',fn=lambda:image.query(2,0)-len(ix)*bias,ranges=image.ranges(2)),
                      dict(name='native-for',fn=native,ranges=image.ranges(1)),
                      dict(name='polars-gather',fn=gather,ranges=ranges),
                      dict(name='polars-prepared-spans',fn=span,ranges=ranges),
                      dict(name='polars-prepared-spans-lazy',fn=lambda:int(span_lazy.collect(engine='in-memory').item()),ranges=ranges),
                      dict(name='polars-span-request',fn=span_request,ranges=ranges),
                      dict(name='frame-duplicate',fn=frame,ranges=image.ranges(0)),
                      dict(name='polars-span-duplicate',fn=span,ranges=ranges)]
                measure(name,rows,layout,'sum','one-sixteenth-blocks',arms,wanted)
                setups.append(dict(input=name,rows=rows,layout=layout,selected_rows=len(ix),stats=stats,
                    frame_image_bytes=sum(size for _,size,_ in image.ranges(0)),
                    native_image_bytes=sum(size for _,size,_ in image.ranges(1)),
                    frame_encode_and_native_for_ns=encode_ns,frame_mask_bind_ns=mask_bind_ns,
                    common_mask_build_ns=mask_build_ns,polars_index_bind_ns=index_bind_ns,
                    polars_span_bind_ns=span_bind_ns,polars_buffer_bytes=keep.nbytes,
                    polars_for_buffer_bytes=for_keep.nbytes,index_bytes=ix.nbytes,
                    polars_selected_sum_cast=str(dtype),polars_selected_sum_bound=str(bound),**full_law,
                    selected_logical_sha256=digest,polars_prepared_span_chunks=chunks.n_chunks(),
                    permutation_sha256=hashlib.sha256(permutation.tobytes()).hexdigest() if layout!='original' else None))
            finally:image.close()
        print(name,rows,'complete',flush=True)
raw_file.close()
groups={};keys=['input','rows','layout','operation','selection','condition','arm']
for record in records:groups.setdefault(tuple(record[k] for k in keys),[]).append(record['ns'])
summary=[dict(zip(keys,key),samples=len(ns),median_ns=statistics.median(ns),samples_ns=ns) for key,ns in sorted(groups.items())]
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(a.output/'setup.json').write_text(json.dumps(setups,indent=2)+'\n')
(a.output/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
assert hashlib.sha256(a.library.read_bytes()).hexdigest()==identity['library_sha256']
print('complete',len(records),'samples')
