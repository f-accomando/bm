#!/usr/bin/env python3
"""The Market's catalog (M25): scripts/mkmarket.py checks a folder of games
and signs the catalog with a test key (openssl, ECDSA P-256), then
tests/net/test_catalog.c reads it with src/net/catalog.c and decodes the
covers with the kernel's PNG reader. Also mkmarket.py's own refusals and
--add."""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mkbm  # noqa: E402

MKMARKET = os.path.join(HERE, "..", "..", "scripts", "mkmarket.py")
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


def mkmarket(*args, env=None):
    e = dict(os.environ, BM_MARKET_KEY="")
    e.update(env or {})
    return subprocess.run([sys.executable, MKMARKET, *args], capture_output=True, text=True, env=e)


def cover(seed):
    """a 128x80 picture with gradients and flat areas, opaque"""
    out = bytearray()
    for y in range(80):
        for x in range(128):
            if (x // 16 + y // 16) % 2:
                out += bytes([(x * 2 + seed) & 255, (y * 3) & 255, (x ^ y) & 255, 255])
            else:
                out += bytes([seed, 40, 90, 255])
    return 128, 80, bytes(out)


def game(games, gid, name, data, info):
    d = os.path.join(games, gid)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)
    if info is not None:
        with open(os.path.join(d, "info.txt"), "w", encoding="utf-8") as f:
            f.write(info)
    return d


with tempfile.TemporaryDirectory() as tmp:
    key, pub = keypair(tmp, "key")
    _, other = keypair(tmp, "other")
    games = os.path.join(tmp, "games")
    code = b"function _draw() cls(1) end"
    snake_cover, pong_cover = cover(10), cover(200)
    game(games, "snake", "snake.bm", mkbm.pack(code, title="Snake", author="bm", cover=snake_cover),
         "version: 1.2\nlicense: MIT\nabout: Eat the apples, do not bite your tail. è la à!\n")
    game(games, "pong-2", "Pong_2.bm", mkbm.pack(code, title="Pong 2", cover=pong_cover),
         "# a comment\nversion: 0.9-beta\nlicense: CC-BY-4.0\n")
    game(games, "plain", "plain.bm", mkbm.pack(code, title="Plain", author="x"),
         "version: 3\nlicense: BM Community License 1.0\nabout: No cover.\n")

    r = mkmarket(games, "--check")
    check(r.returncode == 0 and "3 games, all right" in r.stdout, "--check: three good games")

    out = os.path.join(tmp, "site")
    keytmp = os.path.join(tmp, "keytmp")          # where the key is written for openssl
    os.makedirs(keytmp)
    r = mkmarket(games, "-o", out, "--serial", "20261001120000", "--require-key", "--pub", pub,
                 env={"BM_MARKET_KEY": open(key).read(), "TMPDIR": keytmp})
    check(r.returncode == 0 and "signed" in r.stdout, "mkmarket: signed with the key from BM_MARKET_KEY")
    if r.returncode:
        print(r.stdout + r.stderr)
    check(os.listdir(keytmp) == [], "mkmarket: no copy of the key left behind")
    files = sorted(os.path.relpath(os.path.join(dp, f), out) for dp, _, fs in os.walk(out) for f in fs)
    check(files == ["games/plain/plain.bm", "games/pong-2/Pong_2.bm", "games/pong-2/cover.png",
                    "games/snake/cover.png", "games/snake/snake.bm", "index.html", "index.sig", "index.txt"],
          "mkmarket: the files GitHub Pages serves")
    page = open(os.path.join(out, "index.html"), encoding="utf-8").read()
    check("Snake" in page and "games/snake/snake.bm" in page and "è la à" in page,
          "index.html: the games, with the accents of info.txt")
    sizes = [os.path.getsize(os.path.join(out, "games", g, "cover.png")) for g in ("snake", "pong-2")]
    check(all(s < 128 * 80 * 3 // 2 for s in sizes), f"covers compressed ({sizes} bytes)")

    # the refusals
    def refused(gid, name, data, info, why, what):
        bad = os.path.join(tmp, "bad-" + gid)
        shutil.copytree(games, bad)
        if name:
            game(bad, gid, name, data, info)
        else:
            os.makedirs(os.path.join(bad, gid), exist_ok=True)
        r = mkmarket(bad, "--check")
        check(r.returncode != 0 and why in r.stderr, what)
        if r.returncode == 0 or why not in r.stderr:
            print(r.stdout + r.stderr)
        shutil.rmtree(bad)

    ok_info = "version: 1\nlicense: MIT\n"
    good = mkbm.pack(code, title="Other")
    refused("x", "x.bm", good, "version: 1\n", "license: required", "no license: refused")
    refused("x", "x.bm", good, "license: MIT\n", "version", "no version: refused")
    refused("x", "x.bm", good, None, "no info.txt", "no info.txt: refused")
    refused("Big", "x.bm", good, ok_info, "the folder name is the id", "id with capitals: refused")
    refused("x", "x.bm", mkbm.pack(code, title="Snake", author="bm"), ok_info, "same title and author",
            "same title and author as another game: refused")
    damaged = bytearray(good)
    damaged[-1] ^= 1
    refused("x", "x.bm", bytes(damaged), ok_info, "damaged", "damaged cartridge: refused")
    refused("x", "x.bm", b"PNG...", ok_info, "not a .bm", "not a cartridge: refused")
    refused("x", "x y.bm", good, ok_info, "file names", "a space in the file name: refused")
    refused("x", "", None, None, "exactly one .bm", "a folder without a cartridge: refused")
    refused("x", "x.bm", mkbm.pack(code), ok_info, "no title", "no title: refused")
    refused("x", "x.bm", good, ok_info + "about: " + "a" * 121 + "\n", "about", "about too long: refused")

    r = mkmarket(games, "-o", os.path.join(tmp, "s2"), "--key", key, "--pub", other)
    check(r.returncode != 0 and "not the pair" in r.stderr, "mkmarket: a key that is not the kernel's: stops")
    r = mkmarket(games, "-o", os.path.join(tmp, "s3"), "--require-key")
    check(r.returncode != 0 and "no key" in r.stderr, "mkmarket: --require-key without a key: stops")

    # --add: a new game, the same one again, then a changed one
    add = os.path.join(tmp, "add")
    cart = os.path.join(tmp, "Hunt.bm")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(code, title="Hunt", author="bm"))
    r = mkmarket(add, "--add", cart, "--version", "a1", "--license", "MIT", "--about", "Find them.")
    info = os.path.join(add, "hunt", "info.txt")
    check(r.returncode == 0 and os.path.exists(os.path.join(add, "hunt", "Hunt.bm")) and
          open(info).read() == "version: a1\nlicense: MIT\nabout: Find them.\n", "--add: a new game")
    r = mkmarket(add, "--add", cart, "--version", "a2")
    check(r.returncode == 0 and "unchanged" in r.stdout and "version: a1" in open(info).read(),
          "--add: the same bytes keep the version")
    with open(cart, "wb") as f:
        f.write(mkbm.pack(code + b" ", title="Hunt", author="bm"))
    r = mkmarket(add, "--add", cart, "--version", "a3")
    check(r.returncode == 0 and "version: a3" in open(info).read() and "about: Find them." in open(info).read(),
          "--add: changed bytes take the new version, the rest stays")
    check(mkmarket(add, "--check").returncode == 0, "--add: the folder passes --check")

    covers = []
    for gid, (_, _, rgba) in (("snake", snake_cover), ("pong-2", pong_cover)):
        p = os.path.join(tmp, gid + ".rgba")
        with open(p, "wb") as f:
            f.write(rgba)
        covers += [gid, p]
    r = subprocess.run([sys.argv[1], out, pub, other, *covers])
    sys.exit(1 if fails or r.returncode else 0)
