#!/bin/bash
# Compiler launcher: record each compile's peak RSS and times to $SIXDB_RSS_LOG.
out=?; prev=
for a in "$@"; do [[ $prev == -o ]] && out=$a; prev=$a; done
tmp=$(mktemp)
status=0
/usr/bin/time -f "%M,%e,%U,%S" -o "$tmp" "$@" || status=$?
echo "$out,$(tail -1 "$tmp"),$status" >> "$SIXDB_RSS_LOG"
rm -f "$tmp"
exit $status
