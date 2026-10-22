"""Check concurrent publication, explicit receipts and bounded peer waits."""

from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'peer_inbox.py'
spec = importlib.util.spec_from_file_location('peer_inbox', SCRIPT)
inbox = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inbox)


class PeerInbox(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'to-peer'

    def test_concurrent_writers_keep_all_complete_bodies(self):
        with ThreadPoolExecutor(max_workers=8) as pool:
            messages = list(pool.map(lambda n: inbox.send(self.root, f'writer{n}', 'question', f'body {n}'), range(32)))
        read = inbox.read(self.root, 'reader')
        self.assertEqual(len({m['id'] for m in messages}), 32)
        self.assertEqual({m['body'] for m in read}, {f'body {n}' for n in range(32)})
        self.assertEqual({m['sender'] for m in read}, {f'writer{n}' for n in range(32)})
        self.assertFalse(list(self.root.glob('.pending-*')))

    def test_read_does_not_ack_and_receipts_are_per_reader(self):
        message = inbox.send(self.root, 'Claude', 'Shape', 'Review this boundary.')
        self.assertEqual(len(inbox.read(self.root, 'ikea')), 1)
        self.assertEqual(len(inbox.read(self.root, 'ikea')), 1)
        first = inbox.acknowledge(self.root, 'ikea', message['receipt'])
        self.assertEqual(inbox.acknowledge(self.root, 'ikea', message['receipt']), first)
        self.assertEqual(inbox.read(self.root, 'ikea'), [])
        self.assertEqual(len(inbox.read(self.root, 'workbench')), 1)
        self.assertEqual(len(inbox.read(self.root, 'ikea', include_read=True)), 1)

    def test_late_arrival_sorts_before_acknowledged_message_but_is_not_lost(self):
        message = inbox.send(self.root, 'Codex', 'First', 'already processed')
        inbox.acknowledge(self.root, 'reader', message['receipt'])
        inbox.publish(self.root / '0001-late.md', b'Late arrival from an older writer.\n')
        messages = inbox.read(self.root, 'reader')
        self.assertEqual([m['id'] for m in messages], ['0001-late'])
        self.assertEqual(messages[0]['body'], 'Late arrival from an older writer.\n')

    def test_incomplete_publication_is_invisible_and_publish_never_overwrites(self):
        self.root.mkdir()
        (self.root / '.pending-still-writing.json').write_text('{')
        self.assertEqual(inbox.read(self.root, 'reader'), [])
        path = self.root / 'old.md'
        inbox.publish(path, b'Original')
        with self.assertRaises(FileExistsError):
            inbox.publish(path, b'Replacement')
        self.assertEqual(path.read_bytes(), b'Original')

    def test_ack_refuses_modified_bytes_and_detects_changes_after_ack(self):
        self.root.mkdir()
        path = self.root / 'legacy.md'
        path.write_text('Initial')
        first = inbox.read(self.root, 'reader')[0]
        path.write_text('Changed')
        with self.assertRaisesRegex(ValueError, 'changed since'):
            inbox.acknowledge(self.root, 'reader', first['receipt'])
        current = inbox.read(self.root, 'reader')[0]
        inbox.acknowledge(self.root, 'reader', current['receipt'])
        path.write_text('Changed again')
        with self.assertRaisesRegex(ValueError, 'acknowledged message was modified'):
            inbox.read(self.root, 'reader')

    def test_wait_delivers_an_actual_new_message_and_has_a_finite_timeout(self):
        thread = threading.Thread(target=lambda: (time.sleep(0.1), inbox.send(self.root, 'Claude', 'Reply', 'received')))
        thread.start()
        try:
            messages = inbox.wait(self.root, 'reader', 2, poll=0.02)
            self.assertEqual([m['body'] for m in messages], ['received'])
            inbox.acknowledge(self.root, 'reader', messages[0]['receipt'])
            started = time.monotonic()
            self.assertEqual(inbox.wait(self.root, 'reader', 0.05), [])
            self.assertLess(time.monotonic() - started, 1)
        finally:
            thread.join()

    def test_cli_stdin_file_reply_and_timeout(self):
        def cli(*args, body=None):
            return subprocess.run(['python3', str(SCRIPT), *args], input=body, text=True, capture_output=True)
        sent = cli('send', str(self.root), '--sender', 'Codex', '--subject', 'Boundary', body='Unicode \u2192 test\n')
        self.assertEqual(sent.returncode, 0, sent.stderr)
        message = json.loads(sent.stdout)
        body_file = Path(self.temp.name) / 'reply.txt'
        body_file.write_text('The complete shape is preserved.\n')
        reply = cli('send', str(self.root), '--sender', 'Claude', '--subject', 'Answer',
                    '--body-file', str(body_file), '--in-reply-to', message['id'])
        self.assertEqual(reply.returncode, 0, reply.stderr)
        self.assertEqual(json.loads(reply.stdout)['in_reply_to'], message['id'])
        for item in json.loads(cli('read', str(self.root), '--reader', 'reader').stdout):
            self.assertEqual(cli('ack', str(self.root), '--reader', 'reader', item['receipt']).returncode, 0)
        timed_out = cli('wait', str(self.root), '--reader', 'reader', '--timeout', '0')
        self.assertEqual(timed_out.returncode, 124)
        self.assertEqual(json.loads(timed_out.stdout), [])


if __name__ == '__main__':
    unittest.main()
