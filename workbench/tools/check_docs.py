#!/usr/bin/env python3
"""Offer navigation hints for Git-visible Markdown; no network, edits, or build gate."""
import argparse
from collections import Counter
import html
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[2]
LINK = re.compile(r'\[[^\]\n]*\]\((<[^>\n]+>|[^\s)]+)(?:\s+"[^"]*")?\)')


def prose(text):
    """Hide fenced examples while retaining line numbers."""
    fence = None
    lines = []
    for line in text.splitlines(keepends=True):
        marker = re.match(r'^\s{0,3}(`{3,}|~{3,})', line)
        if marker:
            token = marker[1]
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence) and not line[marker.end():].strip():
                fence = None
            lines.append('\n')
        else:
            lines.append('\n' if fence else line)
    return ''.join(lines)


def anchors(text):
    """ATX headings with GitHub-style duplicate suffixes, plus explicit HTML IDs."""
    found = set(re.findall(r'(?:id|name)=["\']([^"\']+)', text))
    used = Counter()
    for line in text.splitlines():
        heading = re.match(r'^#{1,6}\s+(.+?)\s*#*$', line)
        if heading:
            label = LINK.sub(lambda m: m[0][1:m[0].index(']')], heading[1])
            label = re.sub(r'<[^>]*>', '', html.unescape(label)).lower()
            slug = re.sub(r'[^\w\- ]', '', label).replace(' ', '-')
            unique = slug
            while unique in found:
                used[slug] += 1
                unique = f'{slug}-{used[slug]}'
            found.add(unique)
    return found


def inspect(root):
    names = subprocess.check_output(
        ['git', '-C', str(root), 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
    ).decode().split('\0')
    files = {root / name for name in names if name}
    docs = {p: prose(p.read_text()) for p in sorted(files) if p.suffix == '.md' and p.is_file()}
    destinations = {p: set() for p in docs}
    heading_ids = {p: anchors(text) for p, text in docs.items()}
    hints = []
    local = sibling = fragments = 0
    for path, text in docs.items():
        for match in LINK.finditer(text):
            href = urlsplit(match[1].strip('<>'))
            if href.scheme or href.netloc:
                continue
            target = (path.parent / unquote(href.path)).resolve() if href.path else path
            if not target.is_relative_to(root):
                sibling += 1  # Calico and other sibling checkouts are optional.
                continue
            local += 1
            destinations[path].add(target)
            location = f'{path.relative_to(root)}:{text.count(chr(10), 0, match.start()) + 1}'
            if not target.exists():
                hints.append(f'{location}: missing target {match[1]}')
            elif href.fragment and target.suffix == '.md':
                fragments += 1
                if target not in heading_ids:
                    heading_ids[target] = anchors(prose(target.read_text()))
                if unquote(href.fragment) not in heading_ids[target]:
                    hints.append(f'{location}: heading not found for {match[1]}')

    for directory, marker in [('workbench/spikes', 'README.md'), ('workbench/datasets', 'dataset.json')]:
        parent = root / directory
        catalog = parent / 'README.md'
        for entry in sorted(p.parent for p in files if p.name == marker and p.parent.parent == parent):
            if entry / 'README.md' not in destinations.get(catalog, set()):
                hints.append(f'{directory}/README.md: catalog could link {entry.name}/README.md')
    return hints, len(docs), local, fragments, sibling


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT, help='checkout to inspect; defaults to this checkout')
    args = parser.parse_args()
    hints, docs, links, fragments, sibling = inspect(args.root.resolve())
    print(f'Checked {docs} Markdown files, {links} local links and {fragments} heading references.')
    if sibling:
        print(f'Skipped {sibling} links to sibling checkouts; no external URLs were fetched.')
    for hint in hints:
        print('Hint: ' + hint)
    print(f'{len(hints)} navigation hints. This is advisory; examples and in-flight work may need interpretation.')


if __name__ == '__main__':
    main()
