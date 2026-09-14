#!/bin/bash
# Two or more named members, a private TCP port 43841, and setup=minimal suffice.
set -euo pipefail
export PYTHONPATH="$SIXDB_SOURCE/workbench/tools${PYTHONPATH:+:$PYTHONPATH}"
python3 - <<'PY'
import http.server
import json
import os
from pathlib import Path
import subprocess
import threading
import urllib.request
import uuid
from worker_context import GroupContext

ctx = GroupContext.from_env()
# This sentinel demonstrates retained setup, without claiming a clean machine.
cache = Path(os.environ['SIXDB_SOURCE']) / 'build/group-smoke-setup'
cache.parent.mkdir(exist_ok=True)
if not cache.exists():
    cache.write_text(str(uuid.uuid4()))
identity = {'member': ctx.member, 'job': ctx.job['id'], 'setup': cache.read_text(),
            'reused': os.environ['SIXDB_WORKER_REUSED']}
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(json.dumps(identity).encode())
    def log_message(self, *args):
        pass

server = http.server.ThreadingHTTPServer(('0.0.0.0', 43841), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    address = subprocess.check_output(['hostname', '-I'], text=True).split()[0]
    ctx.ready({'url': f'http://{address}:43841'})
    peers = ctx.wait_ready(timeout=240)
    ctx.progress('checking', waiting_for=list(peers))
    observed = {}
    for name, peer in peers.items():
        with urllib.request.urlopen(peer['url'], timeout=10) as response:
            observed[name] = json.load(response)
        assert observed[name]['job'] == ctx.manifest['members'][name]['job']
    (ctx.results / 'group-smoke.json').write_text(json.dumps(identity | {'peers': observed}, indent=2) + '\n')
    ctx.publish('checked', True)
    # Keep endpoints alive until everyone has used them. This is outside timing.
    ctx.wait_values('checked', timeout=60)
    ctx.progress('complete', completed=len(peers))
finally:
    server.shutdown()
    server.server_close()
PY
