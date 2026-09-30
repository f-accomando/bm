#!/usr/bin/env python3
"""Release manifests (M19): scripts/mkrelease.py signs a release with a test
key (openssl, ECDSA P-256), then tests/net/test_release.c checks it with
src/net/release.c. Also mkrelease.py's own refusals."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MKRELEASE = os.path.join(HERE, "..", "..", "scripts", "mkrelease.py")
fails = 0


def check(ok, what):
    global fails
    print(("ok  " if ok else "FAIL") + " " + what)
    fails += not ok


def keypair(tmp, tag):
    key, pub = os.path.join(tmp, tag + ".pem"), os.path.join(tmp, tag + "-pub.pem")
    subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
                    "-out", key], check=True, capture_output=True)
    subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
    return key, pub


def mkrelease(out, files, *extra, env=None):
    args = [sys.executable, MKRELEASE, out, "--version", "v9.9.9", "--commit", "0123abc"]
    for src, path in files:
        args += ["--file", f"{src}:{path}"]
    e = dict(os.environ, BM_RELEASE_KEY="")
    e.update(env or {})
    return subprocess.run(args + list(extra), capture_output=True, text=True, env=e)


with tempfile.TemporaryDirectory() as tmp:
    key, pub = keypair(tmp, "key")
    _, other = keypair(tmp, "other")
    files = []
    for name, path, size in (("kernel.img", "/kernel.img", 150000), ("pong.bm", "/carts/pong.bm", 3000),
                             ("ca.pem", "/bm/ca.pem", 900)):
        src = os.path.join(tmp, name)
        with open(src, "wb") as f:
            f.write(os.urandom(size))
        files.append((src, path))
    out = os.path.join(tmp, "release")

    keytmp = os.path.join(tmp, "keytmp")          # where mkrelease writes the key for openssl
    os.makedirs(keytmp)
    r = mkrelease(out, files, "--require-key", "--pub", pub,
                  env={"BM_RELEASE_KEY": open(key).read(), "TMPDIR": keytmp})
    check(r.returncode == 0 and "signed" in r.stdout, "mkrelease: signed with the key from BM_RELEASE_KEY")
    if r.returncode:
        print(r.stdout + r.stderr)
    check(sorted(os.listdir(out)) == ["ca.pem", "kernel.img", "manifest.sig", "manifest.txt", "pong.bm"],
          "mkrelease: the files under their release names")
    check(os.listdir(keytmp) == [], "mkrelease: no copy of the key left behind")

    r = mkrelease(os.path.join(tmp, "r2"), files, "--key", key, "--pub", other)
    check(r.returncode != 0 and "not the pair" in r.stderr, "mkrelease: a key that is not the kernel's: stops")
    r = mkrelease(os.path.join(tmp, "r3"), files, "--require-key")
    check(r.returncode != 0 and "no key" in r.stderr, "mkrelease: --require-key without a key: stops")
    r = mkrelease(os.path.join(tmp, "r4"), files + [(files[1][0], "/bm/pong.bm")], "--key", key)
    check(r.returncode != 0 and "repeated" in r.stderr, "mkrelease: two files with one name: stops")

    r = subprocess.run([sys.argv[1], out, pub, other])
    sys.exit(1 if fails or r.returncode else 0)
