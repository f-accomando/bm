#!/usr/bin/env python3
"""
The files of a bm release (M19): the kernel, the games and bm/ca.pem, with
a manifest that says where each one goes on the SD card, its size and its
SHA-256. The manifest is signed with the project's key (ECDSA P-256,
SHA-256): the Pi checks it with the public key built into the kernel
(keys/release-pub.pem) before it installs anything.

  mkrelease.py OUT --version v0.1.0 --file build/kernel.img:/kernel.img ...
      [--key FILE | BM_RELEASE_KEY=<pem> in the environment] [--pub keys/release-pub.pem]

OUT gets the files under their names in the GitHub release (the base name
of the SD path), manifest.txt and, with a key, manifest.sig (DER). With
--pub the signature is checked against that public key: a secret that is
not the pair of the key in the kernel stops here, not on the Pi.

Manifest (text, one line each, what src/net/release.c reads):
  bm release
  version v0.1.0
  commit <git hash>
  file kernel.img /kernel.img 983872 <sha256 hex>
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile


def die(msg):
    print(f"mkrelease: {msg}", file=sys.stderr)
    sys.exit(1)


def sign(manifest, sig, key_pem):
    """openssl dgst: the key never goes on the command line."""
    fd, key = tempfile.mkstemp(suffix=".pem")
    try:
        with os.fdopen(fd, "w") as f:        # mkstemp: readable by the owner only
            f.write(key_pem)
        r = subprocess.run(["openssl", "dgst", "-sha256", "-sign", key, "-out", sig, manifest],
                           capture_output=True, text=True)
        if r.returncode:
            die("cannot sign: " + r.stderr.strip())
    finally:
        os.remove(key)


def verify(manifest, sig, pub):
    r = subprocess.run(["openssl", "dgst", "-sha256", "-verify", pub, "-signature", sig, manifest],
                       capture_output=True, text=True)
    return r.returncode == 0 and "Verified OK" in r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--version", required=True)
    ap.add_argument("--commit", default="")
    ap.add_argument("--file", action="append", default=[], metavar="SRC:SDPATH")
    ap.add_argument("--key", help="private key file (else BM_RELEASE_KEY)")
    ap.add_argument("--require-key", action="store_true", help="fail without a key")
    ap.add_argument("--pub", help="public key the signature must match")
    a = ap.parse_args()

    if not a.version or any(c.isspace() for c in a.version) or len(a.version) > 31:
        die(f"bad version {a.version!r}")
    os.makedirs(a.out, exist_ok=True)
    lines = ["bm release", f"version {a.version}"]
    if a.commit:
        lines.append(f"commit {a.commit}")
    seen = set()
    for spec in a.file:
        src, sep, path = spec.rpartition(":")
        if not sep or not path.startswith("/") or " " in path or len(path) > 47:
            die(f"bad --file {spec!r} (SRC:/path/on/sd)")
        name = os.path.basename(path)
        if name in seen or name in ("manifest.txt", "manifest.sig") or len(name) > 31:
            die(f"bad or repeated file name {name!r}")
        seen.add(name)
        data = open(src, "rb").read()
        shutil.copyfile(src, os.path.join(a.out, name))
        lines.append(f"file {name} {path} {len(data)} {hashlib.sha256(data).hexdigest()}")
    manifest = os.path.join(a.out, "manifest.txt")
    with open(manifest, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    key_pem = open(a.key).read() if a.key else os.environ.get("BM_RELEASE_KEY", "")
    sig = os.path.join(a.out, "manifest.sig")
    if not key_pem.strip():
        if a.require_key:
            die("no key: BM_RELEASE_KEY (a secret of the repository) or --key")
        print("mkrelease: no key, manifest not signed")
    else:
        if a.pub and "BEGIN PUBLIC KEY" not in open(a.pub).read():
            die(f"{a.pub} has no key yet: scripts/release-key.sh makes the pair, commit its public half")
        sign(manifest, sig, key_pem)
        if a.pub and not verify(manifest, sig, a.pub):
            die(f"the signature does not match {a.pub}: the key is not the pair of the one in the kernel")
    print(f"mkrelease: {a.version}, {len(seen)} files in {a.out}"
          + (", signed" if key_pem.strip() else ""))


if __name__ == "__main__":
    main()
