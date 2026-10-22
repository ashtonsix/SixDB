#!/usr/bin/env python3
"""Install shared Git hooks for the checkout owner's commit attribution."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys


BOT = re.compile(rb'\bclaude\b|anthropic(?:\.com)?', re.I)
ATTRIBUTION = re.compile(
    rb'^\s*(?:[^\w\s]*\s*)?(?:'
    rb'(?:co[- ]?)?author(?:ed)?(?:[- ]by)?|committ(?:ed[- ]by|er)|'
    rb'signed[- ]off[- ]by|assisted[- ]by|generated(?:[- ](?:with|by))?'
    rb')\s*(?::|\b)', re.I)
IDENT = re.compile(rb'^(author|committer) .* ([-\d]+ [+-]\d{4})$')
HOOKS = ('prepare-commit-msg', 'commit-msg', 'post-commit', 'post-rewrite', 'post-applypatch', 'post-merge')
MARKER = '# SixDB commit identity hook\n'


def git(*args, data=None):
    return subprocess.check_output(['git', *args], input=data)


def config(key):
    return git('config', '--get', key).decode().strip()


def identity():
    name, email = config('sixdb.commitName'), config('sixdb.commitEmail')
    if not name or not email or any(c in name + email for c in '\n\r<>'):
        raise ValueError('invalid SixDB commit identity')
    return f'{name} <{email}>'.encode()


def clean_message(message):
    """Remove attribution lines, leaving substantive mentions and human credit."""
    lines = message.splitlines(keepends=True)
    kept = [line for line in lines if not (BOT.search(line) and ATTRIBUTION.match(line))]
    if kept == lines:
        return message
    # Removing a final trailer must not leave its preceding blank paragraph.
    while kept and not kept[-1].strip():
        kept.pop()
    return b''.join(kept)


def normalized_commit(raw, owner, parents=None):
    """Keep trees, raw dates/timezones, ordering and unrelated headers intact."""
    header, message = raw.split(b'\n\n', 1)
    out = []
    for line in header.split(b'\n'):
        match = IDENT.match(line)
        if match:
            line = match[1] + b' ' + owner + b' ' + match[2]
        elif line.startswith(b'parent ') and parents:
            old = line[7:].decode()
            line = b'parent ' + parents.get(old, old).encode()
        out.append(line)
    result = b'\n'.join(out) + b'\n\n' + clean_message(message)
    if result != raw and any(line.startswith((b'gpgsig ', b'gpgsig-sha256 ')) for line in out):
        raise ValueError('cannot change signed metadata; use the configured identity before signing')
    return result


def write_commit(raw):
    return git('hash-object', '-t', 'commit', '-w', '--stdin', data=raw).decode().strip()


def normalize_head(rewritten=None):
    old_head = git('rev-parse', 'HEAD').decode().strip()
    owner = identity()
    mapping = {}
    # Rebase can create commits without running post-commit. Its post-rewrite
    # list identifies the new commits, whose descendants must follow new parents.
    commits = (git('rev-list', '--reverse', '--topo-order', old_head).decode().splitlines()
               if rewritten else [old_head])
    for old in commits:
        raw = git('cat-file', 'commit', old)
        parent_ids = [line[7:].decode() for line in raw.split(b'\n\n', 1)[0].splitlines()
                      if line.startswith(b'parent ')]
        if rewritten and old not in rewritten and not any(p in mapping for p in parent_ids):
            continue
        new_raw = normalized_commit(raw, owner, mapping)
        if new_raw != raw:
            mapping[old] = write_commit(new_raw)
    new_head = mapping.get(old_head, old_head)
    if new_head != old_head:
        git('update-ref', '-m', 'sixdb: normalize commit attribution', 'HEAD', new_head, old_head)
        print(f'SixDB normalized commit attribution: {old_head[:12]} -> {new_head[:12]}', file=sys.stderr)


def install(name, email):
    owner = f'{name} <{email}>'.encode()
    if not name or not email or any(c in name + email for c in '\n\r<>'):
        raise ValueError('invalid name/email')
    # core.hooksPath would bypass the common hooks in some/all worktrees.
    override = subprocess.run(['git', 'config', '--get', 'core.hooksPath'], capture_output=True)
    if override.returncode == 0:
        raise ValueError('core.hooksPath is already set; integrate existing hooks before installing')
    common = Path(git('rev-parse', '--path-format=absolute', '--git-common-dir').decode().strip())
    hooks = common / 'hooks'
    for event in HOOKS:
        path = hooks / event
        if path.exists() and MARKER not in path.read_text():
            raise ValueError(f'refusing to replace an existing hook: {path}')
    installed = common / 'sixdb-hooks' / 'commit_identity.py'
    installed.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(Path(__file__).resolve(), installed)
    hooks.mkdir(exist_ok=True)
    for key, value in (
        ('sixdb.commitName', name), ('sixdb.commitEmail', email),
        ('user.name', name), ('user.email', email),
        ('author.name', name), ('author.email', email),
        ('committer.name', name), ('committer.email', email),
    ):
        git('config', '--local', key, value)
    for event in HOOKS:
        path = hooks / event
        path.write_text('#!/bin/sh\n' + MARKER +
                        'exec python3 "$(git rev-parse --git-common-dir)/sixdb-hooks/commit_identity.py" hook ' +
                        event + ' "$@"\n')
        path.chmod(0o755)
    print(f'Installed shared commit hooks for {owner.decode()} in {hooks}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    setup = commands.add_parser('install', help='install for all worktrees of this repository')
    setup.add_argument('--name', required=True)
    setup.add_argument('--email', required=True)
    hook = commands.add_parser('hook', help=argparse.SUPPRESS)
    hook.add_argument('event', choices=HOOKS)
    hook.add_argument('args', nargs='*')
    args = parser.parse_args()
    if args.command == 'install':
        install(args.name, args.email)
    elif args.event in ('prepare-commit-msg', 'commit-msg'):
        path = Path(args.args[0])
        before = path.read_bytes()
        after = clean_message(before)
        if after != before:
            path.write_bytes(after)
    elif args.event == 'post-rewrite':
        rewritten = {line.split()[1] for line in sys.stdin.read().splitlines() if line.strip()}
        # post-commit may already have replaced the amend result.
        rewritten.add(git('rev-parse', 'HEAD').decode().strip())
        normalize_head(rewritten)
    elif args.event == 'post-merge':
        if args.args[0] == '0':
            introduced = set(git('rev-list', 'ORIG_HEAD..HEAD').decode().splitlines())
            if introduced:
                normalize_head(introduced)
    else:
        normalize_head()


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f'SixDB commit identity: {error}')
