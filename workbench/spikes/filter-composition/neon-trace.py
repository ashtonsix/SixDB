#!/usr/bin/env python3
"""Replay runtime.cpp's scalar fixture and full-mask monomorphic cursor trace.

Logical address counts, not timings. Keep independent of the packed kernels.
"""
import csv
import operator
import sys

WORD = (1 << 64) - 1
OPS = [operator.eq, operator.lt, operator.le, operator.gt, operator.ge]


def masks(k, op):
    state = 0x1234567812345678 + k

    def random():
        nonlocal state
        state ^= (state << 13) & WORD
        state ^= state >> 7
        state ^= (state << 17) & WORD
        return state

    limit = (1 << k) - 1
    values = [0 if i % 31 == 0 else limit if i % 31 == 1 else random() & limit
              for i in range(1024)]
    queries = [random() & limit if w % 2 else values[w * 16 + w % 16]
               for w in range(64)]
    return [sum(int(OPS[op](values[w * 16 + i], queries[w])) << i for i in range(16))
            for w in range(64)]


def replay(k, op, distinct):
    results = masks(k, op)
    cursors = [0, 7, 13, 21, 32, 41, 54, 63]
    first_same = None

    def step(i):
        return [(((results[w] * 0x9e3779b97f4a7c15) & WORD) >> 58) ^
                ((i + (j * 0x9e37 if distinct else 0)) & 63)
                for j, w in enumerate(cursors)]

    for i in range(512):
        cursors = step(i)
        if len(set(cursors)) == 1 and first_same is None:
            first_same = i + 1
    total = same = 0
    for i in range(100000):
        count = len(set(cursors))
        total += count
        same += count == 1
        cursors = step(i)
    return k, op, "distinct" if distinct else "original", first_same, total / 100000, same


if __name__ == "__main__":
    out = csv.writer(sys.stdout)
    out.writerow(["width", "op", "trace", "first_same_warmup_step",
                  "mean_distinct_windows", "same_window_steps_of_100000"])
    for k, op in [(k, 0) for k in [16, 17, 24, 32, 33, 64]] + [(32, op) for op in range(1, 5)]:
        for distinct in [False, True]:
            out.writerow(replay(k, op, distinct))
