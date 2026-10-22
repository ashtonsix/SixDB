#!/usr/bin/env python3
"""Exchange colleague messages through a shared directory, without a service."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import uuid


def now():
    return datetime.now(timezone.utc).isoformat(timespec='microseconds')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def component(value):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,159}', value):
        raise ValueError(f'invalid message ID or reader name: {value!r}')
    return value


def publish(path, data):
    """Readers see complete bytes, and publication never replaces another file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.pending-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def send(inbox, sender, subject, body, reply_to=None):
    identifier = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ-') + uuid.uuid4().hex
    message = dict(id=identifier, sender=sender, subject=subject, body=body,
                   created_utc=now(), in_reply_to=reply_to,
                   body_sha256=digest(body.encode()))
    path = inbox / (identifier + '.json')
    publish(path, (json.dumps(message, ensure_ascii=False, indent=2) + '\n').encode())
    return load(path)


def load(path):
    if path.is_symlink():
        raise ValueError(f'message must be an ordinary file: {path}')
    data = path.read_bytes()
    identifier = component(path.stem)
    if path.suffix == '.json':
        message = json.loads(data)
        if message['id'] != identifier or not isinstance(message['body'], str):
            raise ValueError(f'message identity/body mismatch: {path}')
        if digest(message['body'].encode()) != message['body_sha256']:
            raise ValueError(f'message body checksum mismatch: {path}')
    else:
        # Read the hand-written Markdown used to bootstrap an existing session.
        message = dict(id=identifier, sender='unspecified (legacy Markdown)',
                       subject=identifier, body=data.decode())
    return dict(message, path=str(path), receipt=identifier + '@' + digest(data))


def receipt_path(inbox, reader, identifier):
    return inbox / '.receipts' / component(reader) / (component(identifier) + '.json')


def read(inbox, reader, include_read=False):
    component(reader)
    result = []
    for path in sorted(inbox.glob('*')):
        if not path.is_file() or path.name.startswith('.') or path.suffix not in ('.json', '.md'):
            continue
        message = load(path)
        receipt = receipt_path(inbox, reader, message['id'])
        if receipt.exists():
            acknowledged = json.loads(receipt.read_text())
            if acknowledged['receipt'] != message['receipt']:
                raise ValueError(f'acknowledged message was modified: {path}; send a new message')
            if not include_read:
                continue
        result.append(message)
    return result


def acknowledge(inbox, reader, token):
    identifier, _ = token.rsplit('@', 1)
    component(identifier)
    candidates = [inbox / (identifier + ext) for ext in ('.json', '.md')]
    existing = [path for path in candidates if path.is_file()]
    if len(existing) != 1:
        raise ValueError(f'expected one message for {identifier}, found {len(existing)}')
    message = load(existing[0])
    if message['receipt'] != token:
        raise ValueError('message changed since it was read; receipt does not match')
    path = receipt_path(inbox, reader, identifier)
    record = dict(reader=reader, receipt=token, acknowledged_utc=now())
    try:
        publish(path, (json.dumps(record, indent=2) + '\n').encode())
    except FileExistsError:
        record = json.loads(path.read_text())
        if record['receipt'] != token:
            raise ValueError('existing acknowledgment identifies different message bytes')
    return record


def wait(inbox, reader, timeout, poll=0.25):
    if not 0 <= timeout <= 1800:
        raise ValueError('timeout must be between 0 and 1800 seconds')
    deadline = time.monotonic() + timeout
    while True:
        messages = read(inbox, reader)
        if messages:
            return messages
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return []
        time.sleep(min(poll, remaining))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    sending = commands.add_parser('send', help='atomically publish one message')
    sending.add_argument('inbox', type=Path)
    sending.add_argument('--sender', required=True)
    sending.add_argument('--subject', required=True)
    sending.add_argument('--body-file', type=Path, help='UTF-8 body file; stdin when omitted')
    sending.add_argument('--in-reply-to', help='stable message ID being answered')
    for verb in ('read', 'wait', 'ack'):
        command = commands.add_parser(verb)
        command.add_argument('inbox', type=Path)
        command.add_argument('--reader', required=True)
        if verb == 'read':
            command.add_argument('--all', action='store_true', help='include acknowledged messages')
        if verb == 'wait':
            command.add_argument('--timeout', type=float, default=60, help='seconds, at most 1800')
        if verb == 'ack':
            command.add_argument('receipt', help='ID@SHA256 token returned by read/wait')
    args = parser.parse_args()
    if args.command == 'send':
        body = args.body_file.read_text() if args.body_file else sys.stdin.read()
        result = send(args.inbox, args.sender, args.subject, body, args.in_reply_to)
    elif args.command == 'read':
        result = read(args.inbox, args.reader, args.all)
    elif args.command == 'wait':
        result = wait(args.inbox, args.reader, args.timeout)
    else:
        result = acknowledge(args.inbox, args.reader, args.receipt)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 124 if args.command == 'wait' and not result else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError) as error:
        sys.exit(f'Peer inbox: {error}')
