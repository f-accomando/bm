#!/usr/bin/env python3
"""Prepare the public tree (kernel + system + suites + PDF manuals). Never pushes.

  public_export.py export --dest DIR   copy git-tracked files minus EXCLUDE
  public_export.py check DIR           secrets, forbidden paths, dangling refs (exit 1 on any)
  public_export.py build DIR           clean copy, Pi `make` + HOST_TESTS via WSL (no QEMU)

EXCLUDE is derived from the manual cleanup of f-accomando/baremetal
(0f40e87 -> 91c970d -> 18392d3). Run `check` on the destination before publishing.
"""
import argparse, fnmatch, os, re, shutil, subprocess, sys

# fnmatch patterns on posix relative paths ('*' also crosses '/'); a plain
# path also excludes everything below it.
EXCLUDE = [
    '.agents', '.antigravity', '.claude', '.github', 'claude', 'market', 'sync', 'video', 'art',
    'AGENTS.md', 'AI.md', 'CLAUDE.md', 'GEMINI.md', 'SYNC.md',
    *['carts/' + g for g in ('astrowing hunt kitchen nano8 overbit pong shooter snake titan '
                             'titan_ex typing village yharnam').split()],
    'carts/*/cover.png',
    'docs/agents', 'docs/archive', 'docs/design', 'docs/giochi', 'docs/img', 'docs/progress',
    'docs/spec', 'docs/video', 'docs/progress.md', 'docs/ROADMAP.md', 'docs/open-decisions.md',
    'docs/FORMATS-AUDIT.md', 'docs/GUIDA-GIOCHI.md', 'docs/scaffold*.pdf', 'docs/*.png',
    'manuals/shots', 'manuals/themes/preview.png', 'tests/ai/*.png',
    'tests/nano8', 'tests/kitchen', 'tests/overbit', 'tests/yharnam',
    'tests/bm/herobench.c', 'tests/bm/herocost.py', 'tests/bm/mapbench.c',
    'tests/tools/test_agent_audit.py', 'tests/tools/test_public_export.py',
    'boot/rgb30/LEGGIMI.txt',
    *['scripts/' + s for s in ('agent_audit.py cindex.py consistency.py ctx_guard.py docs_audit.py '
                               'generate_guide_pdf.py generate_scaffold_pdf.py generate_scaffold_pdf_it.py '
                               'market-key.sh market.sh mkcovers.py render_game_shots.py wsl-run.ps1 '
                               'public_export.py').split()],
    'tools/bmplay/video.sh', 'tools/bmplay/yharnam_bot.lua', 'tools/showreel', 'tools/mappvs.c', 'tools/sync_*',
    '*.mp4', '*.gif',
]
KEEP = ['carts/texroom/cover.png']   # exceptions to EXCLUDE
GAMES = 'astrowing hunt kitchen nano8 overbit pong shooter snake titan yharnam typing village'.split()
GONE_FILES = ['agent_audit.py', 'cindex.py', 'consistency.py', 'ctx_guard.py', 'docs_audit.py',
              'mkcovers.py', 'render_game_shots.py', 'market.sh', 'market-key.sh', 'wsl-run.ps1',
              'sync_hdr.py', 'sync_check.py', 'sync_watch.py', 'generate_guide_pdf.py']
ALLOW_SECRET_PATHS = ['keys/*.pem', 'boot/ca.pem']   # public keys / certificates
SECRET_RES = [re.compile(p) for p in (
    r'github_pat_[A-Za-z0-9_]{20,}', r'\bghp_[A-Za-z0-9]{20,}', r'\bsk-[A-Za-z0-9_-]{20,}',
    r'\bAKIA[0-9A-Z]{16}\b', r'-----BEGIN [A-Z ]*PRIVATE KEY-----[ \t]*\r?\n[A-Za-z0-9+/=]{20,}',
    # NAME_KEY=value / github_token=... with a real (non-placeholder) value
    r'(?m)^[ \t]*(?:export[ \t]+)?(?:[A-Z][A-Z0-9_]*_KEY|(?i:github_token|wifi_psk|net_password|meshy_key))'
    r'[ \t]*[=:][ \t]*(?![$\s"\'#<]|b[\x22\x27]|(?i:my|la|password|mypassword|your\w*|changeme|x+|[01]|\.\.\.)\b)\S+')]


def excluded(rel):
    rel = rel.replace('\\', '/')
    if rel in KEEP:
        return False
    for p in EXCLUDE:
        if fnmatch.fnmatchcase(rel, p) or rel.startswith(p.rstrip('/') + '/'):
            return True
        if p.startswith('*.') and fnmatch.fnmatchcase(os.path.basename(rel), p):
            return True
    return False


def walk(root):
    for d, ds, fs in os.walk(root):
        ds[:] = [x for x in ds if x != '.git']
        for f in fs:
            p = os.path.join(d, f)
            yield os.path.relpath(p, root).replace('\\', '/'), p


def read_text(p):
    try:
        if os.path.getsize(p) > 4_000_000:
            return None
        with open(p, 'rb') as f:
            b = f.read()
    except OSError:
        return None
    return None if b'\0' in b else b.decode('utf-8', 'replace')


def scan_secrets(root):
    out = []
    for rel, p in walk(root):
        if any(fnmatch.fnmatchcase(rel, a) for a in ALLOW_SECRET_PATHS):
            continue
        t = read_text(p)
        for rx in SECRET_RES if t else []:
            for m in rx.finditer(t):
                out.append((rel, t.count('\n', 0, m.start()) + 1, re.split(r'[=:]', m.group(0))[0].strip()[:24]))
    return out


def forbidden_paths(root):
    return [rel for rel, _ in walk(root) if excluded(rel)]


def dangling(root):
    g = '|'.join(GAMES)
    rx = re.compile(r'(?:carts/|test-|tests/)(?:%s)\b|\b(?:%s)\.(?:bm|b16|lua|bme)\b|[\'"](?:%s)[\'"]|%s'
                    % (g, g, g, '|'.join(re.escape(f) for f in GONE_FILES)))
    out = []
    for rel, p in walk(root):
        if not (rel == 'Makefile' or rel.startswith(('scripts/', 'tests/'))):
            continue
        t = read_text(p)
        for i, line in enumerate((t or '').split('\n'), 1):
            m = rx.search(line)
            if m:
                out.append((rel, i, m.group(0)))
    return out


def cmd_export(a):
    files = subprocess.check_output(['git', 'ls-files', '-z'], text=True).split('\0')
    n = 0
    for rel in filter(None, files):
        if excluded(rel) or not os.path.isfile(rel):
            continue
        d = os.path.join(a.dest, rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(rel, d)
        n += 1
    print('exported %d files to %s' % (n, a.dest))
    return 0


def cmd_check(a):
    bad = 0
    for title, items in (('secret', scan_secrets(a.dir)), ('forbidden path', forbidden_paths(a.dir)),
                         ('dangling reference', dangling(a.dir))):
        for it in items:
            print('%s: %s' % (title, ':'.join(map(str, it)) if isinstance(it, tuple) else it))
        bad += len(items)
    print('check: %s (%d findings)' % ('FAIL' if bad else 'clean', bad))
    return 1 if bad else 0


def wsl_path(p):
    p = os.path.abspath(p).replace('\\', '/')
    return '/mnt/%s%s' % (p[0].lower(), p[2:]) if re.match(r'^[A-Za-z]:', p) else p


def host_tests(makefile):
    t = open(makefile, encoding='utf-8', errors='replace').read()
    m = re.search(r'^HOST_TESTS\s*:=((?:.*\\\n)*.*)', t, re.M)
    names = re.findall(r'\btest-[\w-]+', re.sub(r'\$\(if\s+(?:\$\([^)]*\)|[^,)])*,[^)]*\)', '', m.group(1))) if m else []  # drop K7-only
    return [n for n in dict.fromkeys(names) if re.search(r'^%s\s*:' % re.escape(n), t, re.M)]


def cmd_build(a):
    tests = host_tests(os.path.join(a.dir, 'Makefile'))
    # copy inside WSL's own filesystem (fast), without build/ and .git
    sh = ('W=$(mktemp -d /tmp/bm-pub-XXXXXX) && tar -C "%s" --exclude=./build --exclude=./.git -cf - . '
          '| tar -C $W -xf - && cd $W && { make 2>&1 | tail -n 15; echo BUILD_RC=${PIPESTATUS[0]}; '
          'for t in %s; do make $t >/dev/null 2>&1 && echo "TEST_OK $t" || echo "TEST_FAIL $t"; done; }; '
          'cd /; rm -rf $W' % (wsl_path(a.dir), ' '.join(tests)))
    cmd = ['bash', '-lc', sh] if os.name != 'nt' else ['wsl', '-e', 'bash', '-lc', sh]
    out = subprocess.run(cmd, capture_output=True, text=True, errors='replace').stdout
    print(out)
    ok = 'BUILD_RC=0' in out
    fails = re.findall(r'TEST_FAIL (\S+)', out)
    print('build: %s; host tests failing: %s (of %d)' % ('pass' if ok else 'FAIL', fails or 'none', len(tests)))
    return 0 if ok and not fails else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    e = sp.add_parser('export')
    e.add_argument('--dest', required=True)
    e.set_defaults(f=cmd_export)
    for n, f in (('check', cmd_check), ('build', cmd_build)):
        c = sp.add_parser(n)
        c.add_argument('dir')
        c.set_defaults(f=f)
    a = ap.parse_args(argv)
    return a.f(a)


if __name__ == '__main__':
    sys.exit(main())
