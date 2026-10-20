#!/usr/bin/env python3
"""Check frozen controls and unchanged moved leaves against their Git origin."""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[3]
commit = '4fb8fcea0ae309f2cf19d69ee1f6159e3fa57dae'
def before(path):
    return subprocess.check_output(['git', 'show', f'{commit}:{path}'], cwd=root, text=True)
def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()
def tokens(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    text = re.sub(r'^\s*#include[^\n]*', '', text, flags=re.M)
    text = text.replace('namespace ikea::streampack::native', 'namespace ikea::boundlist::sidecar::native')
    text = text.replace('namespace ikea::streampack', 'namespace ikea::boundlist::sidecar')
    text = text.replace('namespace ikea::boundlist::sidecar::detail', 'namespace ikea::boundlist::detail')
    text = text.replace('using namespace ikea::boundlist::native;', '')
    text = text.replace('namespace ikea::boundlist::sidecar::native', 'namespace ikea::boundlist::native')
    text = text.replace('using byte = std::byte;', '')
    text = text.replace('streampack::detail::', 'sidecar::detail::')
    text = text.replace('std::byte', 'byte').replace('ikea::byte_write', 'byte_write')
    return re.findall(r'\w+|[^\w\s]', text)
def access(text):
    start = text.index('template <geometry Family, class Width, class Offset, class Byte> class access')
    return text[start:text.index('\n};', start)+3]
report = []
for leaf in ('detail/prefix.h', 'detail/simd.h', 'author/native.h', 'author/chain.h'):
    old_path = 'ikea/include/ikea/streampack/' + leaf
    new_path = 'ikea/include/ikea/boundlist/' + leaf
    old, new = before(old_path), (root / new_path).read_text()
    assert tokens(old) == tokens(new), leaf
    report.append(dict(before=old_path, after=new_path, before_sha256=digest(old), after_sha256=digest(new), token_equivalent=True))
old = before('ikea/include/ikea/streampack/author/read.h')
new = (root / 'workbench/spikes/boundlist/legacy-read.h').read_text()
assert tokens(access(old)) == tokens(access(new))
report.append(dict(before='ikea/include/ikea/streampack/author/read.h', after='workbench/spikes/boundlist/legacy-read.h', before_sha256=digest(old), after_sha256=digest(new), access_body_token_equivalent=True))
old = before('ikea/include/ikea/streampack/author/write.h')
new = (root / 'workbench/spikes/boundlist/legacy-write.h').read_text()
def writers(text):
    return text[text.index('template <class Effects>\ninline void emit'):text.rfind('} // namespace')]
assert tokens(writers(old)) == tokens(writers(new))
report.append(dict(before='ikea/include/ikea/streampack/author/write.h', after='workbench/spikes/boundlist/legacy-write.h', before_sha256=digest(old), after_sha256=digest(new), mutation_body_token_equivalent=True))
path = pathlib.Path(sys.argv[1])
path.write_text(json.dumps(dict(commit=commit, normalization='comments, includes, namespace and byte aliases only', controls=report), indent=2)+'\n')
print(f'{len(report)} controls/leaves verified against {commit}')
