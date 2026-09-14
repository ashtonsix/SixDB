"""Read AWS instance-store vendor counters without depending on a new nvme-cli plugin."""
import struct,subprocess,time

def read(device):
    result={'steady_ns':time.monotonic_ns(),'utc_ns':time.time_ns()}
    p=subprocess.run(['nvme','get-log',device,'--log-id=0xd0','--log-len=8192','--namespace-id=1','--raw-binary'],capture_output=True,timeout=5)
    result.update(end_ns=time.monotonic_ns(),returncode=p.returncode)
    if p.returncode or len(p.stdout)!=8192:
        return result|{'error':p.stderr.decode(errors='replace')[:1000],'length':len(p.stdout)}
    magic,version=struct.unpack_from('<II',p.stdout)
    result.update(magic=magic,version=version)
    if magic!=0xec2c0d7e:return result|{'error':'unexpected instance-store vendor magic'}
    names=['read_ops','write_ops','read_bytes','write_bytes','read_time_us','write_time_us','volume_iops_exceeded_us','volume_tp_exceeded_us','instance_iops_exceeded_us','instance_tp_exceeded_us','queue_length']
    return result|{'nvme':dict(zip(names,struct.unpack_from('<11Q',p.stdout,8)))}
