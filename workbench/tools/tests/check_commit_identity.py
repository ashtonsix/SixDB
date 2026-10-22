"""Exercise the installed hooks with real Git commits and rewritten history."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'commit_identity.py'
spec = importlib.util.spec_from_file_location('commit_identity', SCRIPT)
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)
OWNER = b'Ashton Six <me@ashtonsix.com>'


class CommitIdentity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
        self.env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull,
                        GIT_AUTHOR_DATE='1700000000 +0530', GIT_COMMITTER_DATE='1700001000 -0700')
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.name', 'Before')
        self.git('config', 'user.email', 'before@example.test')
        self.git('commit', '-q', '--allow-empty', '-m', 'base')
        self.base = self.git('rev-parse', 'HEAD').strip()
        subprocess.run(['python3', str(SCRIPT), 'install', '--name', 'Ashton Six', '--email', 'me@ashtonsix.com'],
                       cwd=self.root, env=self.env, check=True, capture_output=True)

    def git(self, *args, env=None, cwd=None, data=None):
        return subprocess.check_output(['git', *args], cwd=cwd or self.root,
                                       env=env or self.env, input=data, stderr=subprocess.PIPE)

    def assert_owner(self, ref='HEAD'):
        raw = self.git('cat-file', 'commit', ref)
        self.assertIn(b'author ' + OWNER + b' 1700000000 +0530\n', raw)
        self.assertIn(b'committer ' + OWNER + b' 1700001000 -0700\n', raw)
        return raw

    def test_message_filter_preserves_substance_and_human_credit(self):
        message = (b'Fix Claude adapter\n\nThe Claude API needs this change.\n\n'
                   b'Co-Authored-By: Alice Example <alice@example.test>\n'
                   b'Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>\n'
                   b'Generated with [Claude Code](https://claude.ai/code)\n'
                   b'Committed by Claude\n')
        self.assertEqual(identity.clean_message(message),
                         b'Fix Claude adapter\n\nThe Claude API needs this change.\n\n'
                         b'Co-Authored-By: Alice Example <alice@example.test>\n')

    def test_commit_overrides_explicit_identity_and_keeps_dates_tree_and_credit(self):
        env = dict(self.env, GIT_AUTHOR_NAME='Claude', GIT_AUTHOR_EMAIL='noreply@anthropic.com',
                   GIT_COMMITTER_NAME='Claude Code', GIT_COMMITTER_EMAIL='bot@anthropic.com')
        self.git('commit', '-q', '--allow-empty', '--author=Other <other@example.test>',
                 '-m', 'Change\n\nCo-Authored-By: Human <human@example.test>\n'
                 'Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>', env=env)
        raw = self.assert_owner()
        self.assertIn(b'Co-Authored-By: Human <human@example.test>', raw)
        self.assertNotIn(b'anthropic', raw)
        self.assertEqual(self.git('rev-parse', 'HEAD^{tree}'), self.git('rev-parse', self.base.decode()+'^{tree}'))

    def test_amend_and_no_verify_cannot_restore_bot_attribution(self):
        for extra in ([], ['--amend']):
            self.git('commit', '-q', '--allow-empty', '--no-verify', *extra,
                     '--author=Claude <noreply@anthropic.com>', '-m', 'Change\n\nCo-authored-by: Claude <noreply@anthropic.com>')
            raw = self.assert_owner()
            self.assertNotIn(b'Claude', raw)

    def test_rebase_normalizes_each_new_commit_and_preserves_dates(self):
        # Imported commits bypass ordinary hooks; rebase's post-rewrite must
        # normalize intermediate commits too, including empty ones.
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-q', '--allow-empty',
                 '--author=Claude <noreply@anthropic.com>', '-m', 'First\n\nCo-Authored-By: Claude <noreply@anthropic.com>')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-q', '--allow-empty',
                 '--author=Other <other@example.test>', '-m', 'Second')
        self.git('rebase', '--force-rebase', '--committer-date-is-author-date', self.base.decode())
        for sha in self.git('rev-list', self.base.decode()+'..HEAD').splitlines():
            raw = self.git('cat-file', 'commit', sha.decode())
            self.assertIn(b'author ' + OWNER + b' 1700000000 +0530\n', raw)
            self.assertIn(b'committer ' + OWNER + b' 1700000000 +0530\n', raw)
            self.assertNotIn(b'Claude', raw)
        self.assertEqual(self.git('rev-list', '--count', self.base.decode()+'..HEAD').strip(), b'2')

    def test_common_hooks_apply_in_an_older_linked_worktree(self):
        linked = self.root / 'old-worktree'
        self.git('worktree', 'add', '-q', '-b', 'linked', str(linked), self.base.decode())
        self.git('commit', '-q', '--allow-empty', '--author=Claude <noreply@anthropic.com>',
                 '-m', 'Linked', cwd=linked)
        raw = self.git('cat-file', 'commit', 'HEAD', cwd=linked)
        self.assertIn(b'author ' + OWNER + b' 1700000000 +0530\n', raw)

    def test_cherry_pick_and_mail_patch_normalize_imported_author(self):
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-q', '--allow-empty',
                 '--author=Claude <noreply@anthropic.com>', '-m', 'Imported')
        imported = self.git('rev-parse', 'HEAD').strip().decode()
        self.git('checkout', '-q', '-b', 'pick', self.base.decode())
        self.git('cherry-pick', '--allow-empty', imported)
        self.assert_owner()
        self.git('checkout', '-q', '-b', 'mail', self.base.decode())
        # A real changed file lets git am apply an ordinary patch.
        self.git('checkout', '-q', 'main')
        (self.root / 'payload').write_text('mail input\n')
        self.git('add', 'payload')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-q',
                 '--author=Claude <noreply@anthropic.com>', '-m', 'Patch')
        patch = self.git('format-patch', '-1', '--stdout')
        self.git('checkout', '-q', 'mail')
        self.git('am', data=patch)
        self.assert_owner()

    def test_fast_forward_merge_normalizes_every_introduced_commit(self):
        self.git('checkout', '-q', '-b', 'incoming')
        for subject in ('One', 'Two'):
            self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-q', '--allow-empty',
                     '--author=Claude <noreply@anthropic.com>', '-m', subject)
        self.git('checkout', '-q', 'main')
        self.git('merge', '--ff-only', 'incoming')
        for sha in self.git('rev-list', self.base.decode()+'..HEAD').splitlines():
            self.assert_owner(sha.decode())

    def test_merge_commit_keeps_both_parents_and_the_merged_tree(self):
        self.git('checkout', '-q', '-b', 'incoming')
        (self.root / 'side').write_text('side content\n')
        self.git('add', 'side')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-q',
                 '--author=Claude <noreply@anthropic.com>', '-m', 'Incoming')
        self.git('checkout', '-q', 'main')
        (self.root / 'main-file').write_text('main content\n')
        self.git('add', 'main-file')
        self.git('commit', '-q', '-m', 'Main change')
        self.git('merge', '--no-ff', 'incoming', '-m', 'Merge\n\nCo-Authored-By: Claude <noreply@anthropic.com>')
        raw = self.assert_owner()
        parents = [line[7:] for line in raw.split(b'\n\n', 1)[0].splitlines() if line.startswith(b'parent ')]
        self.assertEqual(len(parents), 2)
        self.assert_owner(parents[1].decode())
        self.assertEqual(self.git('show', 'HEAD:side'), b'side content\n')
        self.assertEqual(self.git('show', 'HEAD:main-file'), b'main content\n')
        self.assertNotIn(b'Claude', raw)

    def test_raw_rewrite_keeps_merge_parents_headers_and_unusual_dates(self):
        raw = (b'tree ' + b'a'*40 + b'\nparent ' + b'b'*40 + b'\nparent ' + b'c'*40 +
               b'\nauthor Claude <bot@example.test> -100 +1245\n'
               b'committer Bot <bot@example.test> 12345 -0345\nencoding ISO-8859-1\n\n'
               b'Subject\n\nDetails\n')
        out = identity.normalized_commit(raw, OWNER, {'b'*40: 'd'*40})
        self.assertIn(b'parent '+b'd'*40+b'\nparent '+b'c'*40+b'\n', out)
        self.assertIn(b'author '+OWNER+b' -100 +1245\n', out)
        self.assertIn(b'committer '+OWNER+b' 12345 -0345\nencoding ISO-8859-1\n', out)
        self.assertTrue(out.endswith(b'\n\nSubject\n\nDetails\n'))
        self.assertEqual(identity.normalized_commit(out, OWNER), out)

    def test_install_refuses_to_clobber_an_unrelated_hook(self):
        hook = self.root / '.git/hooks/commit-msg'
        hook.write_text('#!/bin/sh\necho existing\n')
        before = hook.read_bytes()
        result = subprocess.run(['python3', str(SCRIPT), 'install', '--name', 'Ashton Six', '--email', 'me@ashtonsix.com'],
                                cwd=self.root, env=self.env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(hook.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
