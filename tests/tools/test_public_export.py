"""Unit tests for scripts/public_export.py (temp dirs only, no git, no network).

Run: python -m unittest tests/tools/test_public_export.py
"""
import os, sys, tempfile, unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'scripts'))
import public_export as pe


def tree(files):
    d = tempfile.mkdtemp()
    for rel, txt in files.items():
        p = os.path.join(d, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w') as f:
            f.write(txt)
    return d


class SecretScan(unittest.TestCase):
    def test_tokens_found(self):
        d = tree({'a.txt': 'x github_pat_' + 'A' * 30 + '\nAKIA' + 'B' * 16 + '\n',
                  'b.c': '// sk-' + 'z' * 24})
        self.assertEqual(sorted(r[0] for r in pe.scan_secrets(d)), ['a.txt', 'a.txt', 'b.c'])

    def test_key_assignments(self):
        d = tree({'cfg.txt': 'wifi_psk=hunter2hunter2\nmeshy_key=abc123\nMARKET_KEY=$(X)\nwifi_psk=password\nnet_password=\n'})
        names = [r[2] for r in pe.scan_secrets(d)]
        self.assertEqual(names, ['wifi_psk', 'meshy_key'])

    def test_private_key_block_and_allowlist(self):
        pem = '-----BEGIN PRIVATE KEY-----\n' + 'A' * 40 + '\n'
        d = tree({'keys/x.pem': pem, 'boot/ca.pem': pem, 'leak.pem': pem,
                  'hdr.h': 'const char *s = "-----BEGIN PRIVATE KEY-----\\n";'})
        self.assertEqual([r[0] for r in pe.scan_secrets(d)], ['leak.pem'])

    def test_binary_skipped(self):
        d = tree({})
        open(os.path.join(d, 'x.bin'), 'wb').write(b'\0ghp_' + b'a' * 30)
        self.assertEqual(pe.scan_secrets(d), [])


class Exclusion(unittest.TestCase):
    def test_excluded_paths(self):
        for p in ('carts/pong/main.lua', '.claude/skills/x/SKILL.md', 'docs/progress/a.md',
                  'SYNC.md', 'tools/sync_hdr.py', 'docs/showreel.mp4', 'carts/editor/cover.png',
                  'scripts/public_export.py', 'manuals/shots/a.png'):
            self.assertTrue(pe.excluded(p), p)

    def test_kept_paths(self):
        for p in ('src/kernel/main.c', 'carts/texroom/cover.png', 'carts/editor/icon.png',
                  'carts/write/main.lua', 'manuals/pdf/api-3d.en.pdf', 'tools/bmplay/bmplay.c',
                  'Makefile', 'docs/API.md', 'keys/market.pem'):
            self.assertFalse(pe.excluded(p), p)

    def test_forbidden_paths_check(self):
        d = tree({'carts/snake/main.lua': '', 'src/a.c': ''})
        self.assertEqual(pe.forbidden_paths(d), ['carts/snake/main.lua'])


class Dangling(unittest.TestCase):
    def test_refs_found_in_makefile_scripts_tests_only(self):
        d = tree({'Makefile': 'GAMES := write\nx: build/carts/village.bm\n',
                  'scripts/s.py': 'run("scripts/docs_audit.py")\n',
                  'tests/t.lua': 'dofile("carts/pong/main.lua")\n',
                  'docs/x.md': 'carts/pong/main.lua\n',
                  'tests/ok.py': 'print("typing speed")\n'})
        got = sorted((r[0], r[1]) for r in pe.dangling(d))
        self.assertEqual(got, [('Makefile', 2), ('scripts/s.py', 1), ('tests/t.lua', 1)])

    def test_clean_tree(self):
        self.assertEqual(pe.dangling(tree({'Makefile': 'all:\n\techo hi\n'})), [])

    def test_check_exit_code(self):
        self.assertEqual(pe.main(['check', tree({'src/a.c': 'int x;'})]), 0)
        self.assertEqual(pe.main(['check', tree({'carts/pong/a': 'x'})]), 1)

    def test_host_tests_parser(self):
        d = tree({'Makefile': 'HOST_TESTS := test-a test-b \\\n  test-c\ntest-a:\ntest-c:\n'})
        self.assertEqual(pe.host_tests(os.path.join(d, 'Makefile')), ['test-a', 'test-c'])


if __name__ == '__main__':
    unittest.main()
