"""Descriptive reconstruction of the frozen finite byte-recipe search.
Not an independent correctness oracle. Validate selected capacities against the
native CSV; retain source formulas and full codegen for causal qualification.
"""
def plan(k,kind,a,fmt,secondary=False):
 header=9 if secondary else 5;sf=max(0,k-a-8);suffix=(sf+7)//8;payload=0 if kind==0 else 8
 align=8 if fmt==6 and kind!=0 else 1
 roundup=lambda n:(n+align-1)//align*align
 tag='none' if kind!=2 else 'byte_plane' if fmt==4 else 'bit_plane' if fmt in (5,6) else 'row_byte'
 for slots in range(32,0,-1):
  if fmt==0 and slots&(slots-1):continue
  fp_bytes=(slots+15)//16*16 if fmt==2 else slots
  cursor=header+fp_bytes
  if fmt!=3:
   sf_at=cursor;sf_stride=suffix;cursor+=slots*suffix;cursor=roundup(cursor)
   value_at=cursor;value_stride=roundup(payload+(tag=='row_byte'));tag_at=(cursor+payload)*8;tag_stride=value_stride*8;cursor+=slots*value_stride
  else:
   cursor=roundup(cursor);relative_value=roundup(suffix);stride=roundup(relative_value+payload+(tag=='row_byte'))
   sf_at=cursor;sf_stride=stride;value_at=cursor+relative_value;value_stride=stride;tag_at=(value_at+payload)*8;tag_stride=stride*8;cursor+=slots*stride
  if tag in ('byte_plane','bit_plane'):
   tag_at=cursor*8;tag_stride=8 if tag=='byte_plane' else 1;cursor+=(slots*tag_stride+7)//8
  if cursor>128:continue
  fields=dict(fingerprint=[header,1,1],suffix=[sf_at,sf_stride,suffix] if suffix else None,value=[value_at,value_stride,payload] if payload else None,tag=[tag_at,tag_stride,1 if tag=='bit_plane' else 8] if tag!='none' else None)
  return dict(slots=slots,header_bytes=header,unit_bytes=128,used_bytes=cursor,fields=fields,fingerprint_storage_bytes=fp_bytes,selector_windows=[[header+first,min(16,fp_bytes-first)] for first in range(0,slots,16)])
 raise ValueError((k,kind,a,fmt))

def case(k,kind,a,fmt):return dict(primary=plan(k,kind,a,fmt),secondary=plan(k,kind,a-4,fmt,True))


def access_case(k,kind,a,mode):
    """Permissions only; normalized layout remains case(...,fmt=1)."""
    physical=case(k,kind,a,1)
    output={}
    for name,recipe in physical.items():
        field=recipe['fields']['suffix']
        exact=field[2] if field else 0
        width=1<<(exact-1).bit_length() if exact else 0
        last=field[0]+(recipe['slots']-1)*field[1] if field else 0
        read=width if mode in (2,3) and exact and last+width<=recipe['unit_bytes'] else exact
        windows=[[at,16 if mode in (1,3) and at+16<=recipe['unit_bytes'] else size] for at,size in recipe['selector_windows']]
        output[name]={'suffix_read_bytes':read,'selector_windows':windows}
    return output
