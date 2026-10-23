"""Private ctypes boundary over actual Frame material; no Polars import here."""
import ctypes as c
import numpy as np

U64=c.c_uint64
class Result(c.Structure):
    _fields_=[('low',U64),('high',U64)]
    def integer(self):return self.low+(self.high<<64)
class Stats(c.Structure):
    _fields_=[(k,U64) for k in ['rows','frame_payload','frame_metadata','frame_capacity',
        'native_capacity','native_lane','minimum','maximum','mask_bytes']]
    def dictionary(self):return {k:getattr(self,k) for k,_ in self._fields_}
class Range(c.Structure):
    _fields_=[('address',c.c_void_p),('bytes',U64),('role',U64)]

class Library:
    def __init__(self,path):
        self.lib=c.CDLL(str(path))
        definitions={
            'build':([c.c_void_p,c.c_size_t],c.c_void_p),
            'destroy':([c.c_void_p],None),
            'mask':([c.c_void_p,c.c_void_p,c.c_size_t],c.c_int),
            'query':([c.c_void_p,c.c_uint,c.c_uint,U64],Result),
            'stats':([c.c_void_p],Stats),
            'ranges':([c.c_void_p,c.c_uint,c.POINTER(Range),c.c_size_t],c.c_size_t),
            'flush_supported':([],c.c_int),
            'flush':([c.c_void_p,c.c_size_t,c.c_size_t],c.c_size_t),
        }
        for name,(args,result) in definitions.items():
            fn=getattr(self.lib,'frame_scan_'+name);fn.argtypes=args;fn.restype=result
            setattr(self,name,fn)
    def image(self,values):return Image(self,values)
    def condition(self,ranges,line):
        return sum(self.flush(address,length,line) for address,length,_ in ranges)

class Image:
    def __init__(self,library,values):
        assert values.dtype==np.dtype('uint64') and values.flags.c_contiguous
        self.library=library;self.handle=library.build(values.ctypes.data,len(values))
        if not self.handle:raise RuntimeError('Frame image construction refused')
    def __enter__(self):return self
    def __exit__(self,*_):self.close()
    def close(self):
        if self.handle:self.library.destroy(self.handle);self.handle=None
    def mask(self,words):
        assert words.dtype==np.dtype('uint64') and words.flags.c_contiguous
        if not self.library.mask(self.handle,words.ctypes.data,len(words)):raise ValueError('mask refused')
    def query(self,implementation,operation,threshold=0):
        return self.library.query(self.handle,implementation,operation,threshold).integer()
    def stats(self):return self.library.stats(self.handle).dictionary()
    def ranges(self,implementation):
        n=self.library.ranges(self.handle,implementation,None,0);rows=(Range*n)()
        assert self.library.ranges(self.handle,implementation,rows,n)==n
        return [(v.address,v.bytes,v.role) for v in rows]
