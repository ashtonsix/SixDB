#!/usr/bin/env python3
"""Independent local reference samples; no cross-AZ synchronization assumption."""
import argparse
import glob
import json
import os
import socket
import struct
import sys
import threading
import time

RAW = time.CLOCK_MONOTONIC_RAW
NTP_EPOCH = 2208988800


def raw():
    return time.clock_gettime_ns(RAW)


def ntp_ns(data):
    sec, frac = struct.unpack('!II', data)
    return (sec - NTP_EPOCH) * 10**9 + ((frac * 10**9) >> 32)


def sample_ntp(sock):
    packet = bytearray(48)
    packet[0] = 0x23
    nonce = os.urandom(8)
    packet[40:48] = nonce
    a = raw()
    sock.send(packet)
    response = sock.recv(1024)
    b = raw()
    if len(response) < 48 or response[24:32] != nonce:
        raise ValueError('invalid NTP length/origin')
    leap, mode, stratum = response[0] >> 6, response[0] & 7, response[1]
    if leap == 3 or mode != 4 or not 1 <= stratum <= 15:
        raise ValueError(f'unsynchronized NTP: {leap=}, {mode=}, {stratum=}')
    delay = struct.unpack('!i', response[4:8])[0] * 1e9 / 65536
    dispersion = struct.unpack('!I', response[8:12])[0] * 1e9 / 65536
    precision = 2.0 ** struct.unpack('!b', response[3:4])[0] * 1e9
    return dict(kind='ntp', a=a, b=b, t2=ntp_ns(response[32:40]),
                t3=ntp_ns(response[40:48]), root_delay_ns=delay,
                dispersion_ns=dispersion, precision_ns=precision,
                stratum=stratum, leap=leap, refid=response[12:16].hex())


def sample_phc(clock, paths):
    # Refresh first: an invalid cached bound returns EBUSY until a new PHC read
    # succeeds. Reading that cache as a prerequisite can permanently self-block.
    a = raw()
    utc = time.clock_gettime_ns(clock)
    b = raw()
    bounds = [int(open(p).read()) for p in paths]
    return dict(kind='phc',a=a,b=b,utc=utc,error_ns=max(bounds),bounds=paths)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--hz', type=float, default=10)
    args = parser.parse_args()
    phcs = []
    for path in glob.glob('/sys/class/ptp/ptp*'):
        if 'ena' in open(path + '/clock_name').read():
            fd = os.open('/dev/' + os.path.basename(path), os.O_RDONLY)
            # The cached bound belongs to this PHC's PCI device, not another NIC.
            bound = os.path.realpath(path+'/device')+'/phc_error_bound'
            if not os.path.isfile(bound):
                raise ValueError('missing device-specific PHC error bound: '+bound)
            bounds = [bound]
            phcs.append((fd, ((~fd) << 3) | 3, bounds))
    end = time.monotonic() + args.seconds
    lock = threading.Lock()
    def emit(row):
        with lock:
            sys.stdout.write(json.dumps(row)+'\n')
            sys.stdout.flush()
    def ntp_loop():
        while time.monotonic() < end:
            try:
                # A delayed reply after a timeout must not leave a reused socket
                # one nonce behind forever. Closing each request discards stale replies.
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
                    sock.connect(('169.254.169.123',123))
                    sock.settimeout(.2)
                    emit(sample_ntp(sock))
            except (OSError, ValueError) as error:
                emit(dict(kind='error',source='ntp',message=str(error),raw=raw()))
            # NTP waits must neither stall PHC nor cause catch-up request bursts.
            time.sleep(1)
    threading.Thread(target=ntp_loop,daemon=True).start()
    while time.monotonic() < end:
        a = raw()
        realtime = time.time_ns()
        b = raw()
        rows = [dict(kind='wall', a=a, b=b, utc=realtime)]
        for fd, clock, paths in phcs:
            try:
                rows.append(sample_phc(clock,paths))
            except (OSError, ValueError) as error:
                rows.append(dict(kind='error', source='phc', message=str(error), raw=raw()))
        for row in rows:
            emit(row)
        time.sleep(1 / args.hz)


if __name__ == '__main__':
    main()
