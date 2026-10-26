#!/usr/bin/env python3
"""
The catalog of the bm Market (M25): checks the games of the market's
repository (f-accomando/bm-market), writes what its GitHub Pages serve and
signs the index with the market's key (ECDSA P-256, SHA-256; the public
half, keys/market-pub.pem, is built into the kernel).

  mkmarket.py GAMES [--apps APPS] -o OUT [--key FILE | BM_MARKET_KEY=<pem>] [--pub keys/market-pub.pem]
              [--require-key] [--serial N]
  mkmarket.py GAMES [--apps APPS] --check
                                       only the checks (pull requests)
  mkmarket.py GAMES [--apps APPS] --add game.bm --version 1.2 [--license L] [--about TEXT]
              [--id ID] [--kind app] [--about-file market/about.txt]
                                       puts a cartridge in GAMES (a new folder, or
                                       the same game updated); with --apps and
                                       --kind app it goes in APPS; --about-file has
                                       "id: license | about" lines (the project's
                                       games, make market-seed)

APPS is optional: a second folder with the same layout (apps/<id>/<id>.bm
and info.txt). Each item of APPS is an app (kind app), whatever its
info.txt says. An id must not exist in both folders. Without --apps, GAMES
alone works as before (an item there is an app with "kind: app" in info.txt,
or by its id in APP_IDS).

GAMES has one folder per game, named by its id (a-z, 0-9 and '-', at most
23 characters), with its cartridge (one .bm; the header says which kernels it is
for: both kernels list it, and the one it is not for fades its cover) and info.txt.
A .b16 of an old release is still read (the kernels play it as Pi plus RGB30) but
--add writes .bm only:
  games/snake/snake.bm
  games/snake/info.txt      version: 1.2
                            license: MIT            (required)
                            about: Eat the apples, do not bite your tail.
Title and author come from the cartridge's header: they are what the console
shows and what names the save file, so two games cannot share both.

OUT gets:
  index.txt, index.sig     the catalog and its signature (DER)
  games/<id>/<name>.bm     the cartridge (apps of APPS: apps/<id>/...); an old <name>.b16 is
                           listed only when the id has no .bm
  games/<id>/cover.png     the cover (88x88; 128x80 in older cartridges), when it has one
  index.html               the same catalog for a browser

index.txt (what src/net/market.c reads; CP437 text, one record per game;
the serial only grows, so the console never goes back to an older catalog):
  bm market
  serial 20261001120000
  game snake
  title Snake
  author bm
  version 1.2
  license MIT
  about Eat the apples, do not bite your tail.
  targets 3                (only for a cartridge with the compatibility block: 1 Pi, 2 RGB30, 3 both;
                            without the line the client reads the old rules from the file name)
  file games/snake/snake.bm 48376 <sha256 hex>
  cover games/snake/cover.png 5120 <sha256 hex>
"""
import argparse
import hashlib
import html
import os
import re
import shutil
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmmesh  # noqa: E402
from mkrelease import sign, verify  # noqa: E402

SEC_COVER = 4
COVER_MAX = 512                             # the menu fits any size (menu_load_cover)
MAX_CART = 100 << 20                # GitHub refuses bigger files: its limit, not bm's (a .bm has none)
CART_EXT = (".bm", ".b16")          # the .b16: the same container, the handhelds' profile
MAX_GAMES = 256                     # src/net/market.h
ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,22}$")
FILE_RE = re.compile(r"^[A-Za-z0-9_-][A-Za-z0-9._-]{0,39}\.(bm|b16)$")
VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,14}$")
LIMITS = {"license": 40, "about": 120}
APP_IDS = {"sdk", "editor", "sdk-b16", "code", "pixel", "studio", "animator", "mesh", "sound", "assistant", "write", "typing", "bmweb"}


def die(msg):
    print(f"mkmarket: {msg}", file=sys.stderr)
    sys.exit(1)


# ------------------------------------------------------------------ cartridges

def header_text(data, off, n):
    """a NUL-terminated field of the header, as the console reads it"""
    raw = data[off:off + n].split(b"\0", 1)[0]
    return bytes(c if c >= 32 else 32 for c in raw).strip()


def check_cart(data):
    """-> (title bytes, author bytes, cover RGBA or None); raises ValueError"""
    if len(data) < 128 or data[:8] not in (b"BMCART\0\0", b"BM33CART"):
        raise ValueError("not a .bm cartridge")
    if struct.unpack_from("<H", data, 18)[0] & 1:
        raise ValueError("a project (.bme), not a game: build its .bm first (Build .bm on the console)")
    if len(data) > MAX_CART:
        raise ValueError(f"{len(data)} bytes, more than GitHub takes in a file ({MAX_CART})")
    crc = struct.unpack_from("<I", data, 20)[0]
    if zlib.crc32(data[128:]) & 0xFFFFFFFF != crc:
        raise ValueError("damaged (the CRC of the header does not match)")
    count = data[17]
    if 128 + 16 * count > len(data):
        raise ValueError("damaged (section table)")
    for i in range(count):
        _, off, size, _ = struct.unpack_from("<IIII", data, 128 + i * 16)
        if off < 128 + 16 * count or off + size > len(data):
            raise ValueError(f"damaged (section {i} out of the file)")
    title, author = header_text(data, 24, 48), header_text(data, 72, 32)
    if not title:
        raise ValueError("no title in the header")
    cover = None
    for typ, body in bmmesh.cart_sections(data):
        if typ == SEC_COVER and len(body) >= 4:
            w, h = struct.unpack_from("<HH", body)
            if 0 < w <= COVER_MAX and 0 < h <= COVER_MAX and len(body) >= 4 + w * h * 4:
                cover = (w, h, body[4:4 + w * h * 4])
            break
    return title, author, cover


def png(w, h, rgba):
    """RGB (or RGBA if anything is see-through), a filter chosen per row"""
    alpha = any(rgba[i] != 255 for i in range(3, len(rgba), 4))
    bpp = 4 if alpha else 3
    rows = []
    for y in range(h):
        line = rgba[y * w * 4:(y + 1) * w * 4]
        rows.append(line if alpha else bytes(b for i, b in enumerate(line) if i % 4 != 3))
    out, prev = bytearray(), bytes(w * bpp)
    for cur in rows:
        best = None
        for f in range(5):
            enc = bytearray(len(cur))
            for i, x in enumerate(cur):
                a = cur[i - bpp] if i >= bpp else 0
                b, c = prev[i], prev[i - bpp] if i >= bpp else 0
                if f == 1:
                    x -= a
                elif f == 2:
                    x -= b
                elif f == 3:
                    x -= (a + b) >> 1
                elif f == 4:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    x -= a if pa <= pb and pa <= pc else b if pb <= pc else c
                enc[i] = x & 255
            cost = sum(v if v < 128 else 256 - v for v in enc)
            if best is None or cost < best[0]:
                best = (cost, f, enc)
        out.append(best[1])
        out += best[2]
        prev = cur

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(out), 9))
            + chunk(b"IEND", b""))


# ------------------------------------------------------------------ games

def read_info(path):
    info = {}
    with open(path, encoding="utf-8") as f:
        for no, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, sep, value = line.partition(":")
            if not sep:
                raise ValueError(f"info.txt line {no}: 'key: value'")
            info[key.strip().lower()] = value.strip()
    return info


def console_text(s, what, limit):
    """UTF-8 from info.txt -> the console's CP437, one line"""
    if any(ord(c) < 32 for c in s):
        raise ValueError(f"{what}: control characters")
    b = s.encode("cp437", errors="replace")
    if len(b) > limit:
        raise ValueError(f"{what}: more than {limit} characters")
    return b


def cart_targets(data):
    """-> the kernels a cartridge declares (header byte 120, flag bit 4: 1 Pi, 2 RGB30, 3 both), or 0 for an
    old file (no block): the client then uses the old rules by the file name (docs/BM-FORMAT.md)"""
    flags = struct.unpack_from("<H", data, 18)[0]
    if flags & 16 and data[120] & 3:
        return data[120] & 3
    return 0


def read_game(games, gid, force_kind=None):
    """-> a list of dicts (one per cartridge: a .bm, a .b16, or both), or
    raises ValueError with what is wrong"""
    if not ID_RE.match(gid):
        raise ValueError("the folder name is the id: a-z, 0-9 and '-', at most 23")
    d = os.path.join(games, gid)
    carts = [n for n in sorted(os.listdir(d)) if n.lower().endswith(CART_EXT)]
    if any(n.lower().endswith(".bme") for n in os.listdir(d)):
        raise ValueError("a project (.bme) is not a game of the Market: build its .bm first")
    if not carts:
        raise ValueError("no cartridge (.bm or .b16)")
    if len({n.lower().endswith(".b16") for n in carts}) != len(carts):
        raise ValueError("two cartridges of the same kind: at most one .bm and one .b16")
    if not os.path.exists(os.path.join(d, "info.txt")):
        raise ValueError("no info.txt")
    info = read_info(os.path.join(d, "info.txt"))
    version, lic = info.get("version", ""), info.get("license", "")
    kind = force_kind or info.get("kind", "app" if gid in APP_IDS else "game").lower()
    if kind not in ("game", "app"):
        raise ValueError(f"kind {kind!r}: 'game' or 'app'")
    if not VERSION_RE.match(version):
        raise ValueError(f"version {version!r}: 1-15 of A-Z, a-z, 0-9, '.', '_', '-'")
    if not lic:
        raise ValueError("license: required (MIT, CC-BY-4.0, BM Community License 1.0...)")
    out = []
    for name in carts:
        if not FILE_RE.match(name):
            raise ValueError(f"{name}: file names with A-Z, a-z, 0-9, '.', '_' and '-', at most 43")
        data = open(os.path.join(d, name), "rb").read()
        try:
            title, author, cover = check_cart(data)
        except ValueError as e:
            raise ValueError(f"{name}: {e}")
        out.append({
            "id": gid, "name": name, "data": data, "title": title, "author": author, "cover": cover,
            "version": version.encode(), "license": console_text(lic, "license", LIMITS["license"]),
            "about": console_text(info.get("about", ""), "about", LIMITS["about"]),
            "kind": kind, "info": info, "targets": cart_targets(data),
        })
    return out


def read_games(games, apps=None):
    """GAMES, then APPS (optional); the items of APPS are always apps"""
    if not os.path.isdir(games):
        die(f"{games}: no such folder")
    folders = [(games, "games", None)]
    if apps:
        if os.path.isdir(apps):         # a market without apps/ yet is fine
            folders.append((apps, "apps", "app"))
    out, errors, seen, ids = [], [], {}, {}
    for folder, sub, force in folders:
        for gid in sorted(os.listdir(folder)):
            if gid.startswith(".") or not os.path.isdir(os.path.join(folder, gid)):
                continue
            if gid in ids:
                errors.append(f"{gid}: the id is in {ids[gid]}/ and in {sub}/ (an id is in one folder only)")
                continue
            ids[gid] = sub
            try:
                gs = read_game(folder, gid, force)
            except (ValueError, OSError, UnicodeDecodeError) as e:
                errors.append(f"{gid}: {e}")
                continue
            for g in gs:
                g["sub"] = sub
                who = (g["title"].lower(), g["author"].lower())
                if who in seen and seen[who] != gid:
                    errors.append(f"{gid}: same title and author as {seen[who]} (the save files would mix)")
                    continue
                seen[who] = gid
                out.append(g)
    if len(out) > MAX_GAMES:
        errors.append(f"{len(out)} games, the console reads at most {MAX_GAMES}")
    if errors:
        die("\n  ".join(["the catalog is not right:"] + errors))
    return out


# ------------------------------------------------------------------ output

def _render_cards(items):
    rows = []
    for g in items:
        t = html.escape(g["title"].decode("cp437"))
        a = html.escape(g["author"].decode("cp437")) or "-"
        about = html.escape(g["info"].get("about", ""))
        lic = html.escape(g["info"].get("license", ""))
        b16 = g["name"].lower().endswith(".b16")
        t_bits = g.get("targets") or (3 if b16 else 1)
        who = {1: "Pi", 2: "RGB30", 3: "Pi + RGB30"}[t_bits]
        fmt = (f'<span class="badge b16">.b16 (old) &middot; {who}</span>' if b16
               else f'<span class="badge bm">{who}</span>')
        img = (f'<img src="{g["sub"]}/{g["id"]}/cover.png" width="176" height="176" alt="">' if g["cover"]
               else f'<div class="nocover">{t}</div>')
        rows.append(f"""<li>{img}<div><h2>{t} {fmt}</h2><p class="by">{a} &middot; v{html.escape(g["version"].decode())}
 &middot; {lic} &middot; {(len(g["data"]) + 1023) // 1024} KB</p><p>{about}</p>
<p><a href="{g["sub"]}/{g["id"]}/{g["name"]}" download>{g["name"]}</a></p></div></li>""")
    return "\n".join(rows)


def page(games, serial):
    play = [g for g in games if g.get("kind", "game") != "app"]
    apps = [g for g in games if g.get("kind", "game") == "app"]
    sections = []
    if play:
        sections.append(f"<h2>Games ({len(play)})</h2>\n<ul>\n{_render_cards(play)}\n</ul>")
    if apps:
        sections.append(f"<h2>Apps &amp; Dev Tools ({len(apps)})</h2>\n<ul>\n{_render_cards(apps)}\n</ul>")
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>bm Market</title>
<style>
:root {{ --bg:#16161c; --card:#22222a; --text:#f0f0f4; --dim:#9a9aa8; --accent:#00c8f0; }}
body {{ margin:0; background:var(--bg); color:var(--text); font:16px/1.5 system-ui,sans-serif; }}
main {{ max-width:880px; margin:0 auto; padding:24px 16px; }}
h1 {{ margin:0 0 4px; }} .lead {{ color:var(--dim); margin:0 0 24px; }}
h2 {{ margin:24px 0 12px; font-size:20px; }}
li h2 {{ margin:0; display:flex; align-items:center; gap:8px; flex-wrap:wrap; }}
.badge {{ font-size:12px; font-weight:600; padding:2px 8px; border-radius:999px; }}
.badge.bm {{ background:#0d3b4c; color:#5ce1ff; }}
.badge.b16 {{ background:#4a2c0a; color:#ffb84d; }}
ul {{ list-style:none; padding:0; margin:0; display:grid; gap:16px; }}
li {{ display:flex; gap:16px; background:var(--card); border-radius:12px; padding:16px; flex-wrap:wrap; }}
li img, .nocover {{ border-radius:8px; image-rendering:pixelated; width:176px; height:176px; }}
.nocover {{ display:flex; align-items:center; justify-content:center; background:#303040; color:#ffe678; }}
.by {{ color:var(--dim); margin:0 0 8px; }}
a {{ color:var(--accent); }} code {{ background:#000; padding:2px 6px; border-radius:4px; }}
</style></head><body><main>
<h1>bm Market</h1>
<p class="lead">Free games and apps for bm: the Raspberry Pi Zero W and the Powkiddy RGB30 (<code>.bm</code>).
On the console: the <b>Market</b> tab of the menu (<b>Games</b> / <b>Apps</b>). From a PC: download a cartridge, then
<code>tools/bm_net.py IP --send game.bm</code>. Catalog {serial}, {len(play)} games, {len(apps)} apps.</p>
{chr(10).join(sections)}
</main></body></html>
"""


def build(games, out, serial):
    lines = [b"bm market", b"serial " + serial.encode()]
    for g in games:
        gdir = os.path.join(out, g["sub"], g["id"])
        os.makedirs(gdir, exist_ok=True)
        rel = f"{g['sub']}/{g['id']}/{g['name']}"
        with open(os.path.join(out, rel), "wb") as f:
            f.write(g["data"])
        lines.append(b"game " + g["id"].encode())
        if g.get("kind") == "app":
            lines.append(b"kind app")
        if g.get("targets"):
            lines.append(f"targets {g['targets']}".encode())
        lines += [b"title " + g["title"], b"author " + g["author"],
                  b"version " + g["version"], b"license " + g["license"], b"about " + g["about"],
                  f"file {rel} {len(g['data'])} {hashlib.sha256(g['data']).hexdigest()}".encode()]
        if g["cover"]:
            p = png(*g["cover"])
            with open(os.path.join(gdir, "cover.png"), "wb") as f:
                f.write(p)
            lines.append(f"cover {g['sub']}/{g['id']}/cover.png {len(p)} {hashlib.sha256(p).hexdigest()}".encode())
    index = os.path.join(out, "index.txt")
    with open(index, "wb") as f:
        f.write(b"\n".join(lines) + b"\n")
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page(games, serial))
    return index


def add(games, path, gid, version, lic, about, kind=None, apps=None):
    """a cartridge into GAMES (APPS for an app, when APPS is given): a new
    folder, or the same game updated (the version changes only when the
    bytes do)"""
    data = open(path, "rb").read()
    try:
        title, author, _ = check_cart(data)
    except ValueError as e:
        die(f"{path}: {e}")
    gid = gid or re.sub(r"[^a-z0-9-]+", "-", os.path.splitext(os.path.basename(path))[0].lower()).strip("-")[:23]
    if not ID_RE.match(gid):
        die(f"bad id {gid!r}: --id with a-z, 0-9 and '-'")
    to_apps = bool(apps) and (kind == "app")
    other = games if to_apps else apps
    if other and os.path.isdir(os.path.join(other, gid)):
        die(f"{gid}: the id is already in {other} (an id is in one folder only)")
    d = os.path.join(apps if to_apps else games, gid)
    os.makedirs(d, exist_ok=True)
    name = os.path.basename(path)
    if name.lower().endswith(".b16"):
        die(f"{path}: a .b16 is not published any more; build the .bm (its header says the kernels)")
    # one cartridge per id: the new .bm replaces the old .bm and any old .b16 of the same game
    old = [n for n in os.listdir(d) if n.lower().endswith(CART_EXT)]
    info_path = os.path.join(d, "info.txt")
    info = read_info(info_path) if os.path.exists(info_path) else {}
    same = old == [name] and open(os.path.join(d, name), "rb").read() == data
    for n in old:
        if n != name or not same:
            os.remove(os.path.join(d, n))
    if not same:
        shutil.copyfile(path, os.path.join(d, name))
        info["version"] = version
    info.setdefault("version", version)
    if lic:
        info["license"] = lic
    if about:
        info["about"] = about
    if to_apps:
        info.pop("kind", None)          # the folder says it
    elif kind:
        info["kind"] = kind
    elif gid in APP_IDS:
        info.setdefault("kind", "app")
    with open(info_path, "w", encoding="utf-8", newline="\n") as f:
        for k in ("version", "license", "about", "kind"):
            if k in info:
                f.write(f"{k}: {info[k]}\n")
        for k, v in info.items():
            if k not in ("version", "license", "about", "kind"):
                f.write(f"{k}: {v}\n")
    print(f"mkmarket: {gid}: {title.decode('cp437')} {'unchanged' if same else 'v' + info['version']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("games")
    ap.add_argument("--apps", help="second folder: the apps (same layout as GAMES)")
    ap.add_argument("-o", "--out")
    ap.add_argument("--check", action="store_true", help="only check the games")
    ap.add_argument("--serial", help="default: the time, YYYYMMDDhhmmss UTC")
    ap.add_argument("--key", help="private key file (else BM_MARKET_KEY)")
    ap.add_argument("--require-key", action="store_true", help="fail without a key")
    ap.add_argument("--pub", help="public key the signature must match")
    ap.add_argument("--add", metavar="CART", help="put this cartridge in GAMES")
    ap.add_argument("--id")
    ap.add_argument("--version")
    ap.add_argument("--license")
    ap.add_argument("--about")
    ap.add_argument("--about-file")
    ap.add_argument("--kind", choices=("game", "app"), help="catalog page: game (default) or app; app goes in --apps, if given")
    a = ap.parse_args()

    if a.add:
        if not a.version or not VERSION_RE.match(a.version):
            die("--add needs --version (1-15 of A-Z, a-z, 0-9, '.', '_', '-')")
        lic, about = a.license, a.about
        if a.about_file:
            gid = a.id or os.path.splitext(os.path.basename(a.add))[0].lower()
            for line in open(a.about_file, encoding="utf-8"):
                key, sep, rest = line.partition(":")
                if sep and not line.startswith("#") and key.strip() == gid:
                    l_, _, ab = rest.partition("|")
                    lic, about = lic or l_.strip(), about or ab.strip()
        add(a.games, a.add, a.id, a.version, lic, about, a.kind, a.apps)
        return
    games = read_games(a.games, a.apps)
    if a.check:
        print(f"mkmarket: {len(games)} games, all right")
        return
    if not a.out:
        die("-o OUT (or --check)")
    serial = a.serial or time.strftime("%Y%m%d%H%M%S", time.gmtime())
    if not serial.isdigit() or len(serial) > 15:
        die(f"bad serial {serial!r}: digits, at most 15")
    os.makedirs(a.out, exist_ok=True)
    index = build(games, a.out, serial)

    key_pem = open(a.key).read() if a.key else os.environ.get("BM_MARKET_KEY", "")
    sig = os.path.join(a.out, "index.sig")
    if not key_pem.strip():
        if a.require_key:
            die("no key: BM_MARKET_KEY (a secret of the market's repository) or --key")
        print("mkmarket: no key, catalog not signed")
    else:
        if a.pub and "BEGIN PUBLIC KEY" not in open(a.pub).read():
            die(f"{a.pub} has no key yet: scripts/market-key.sh makes the pair, commit its public half")
        sign(index, sig, key_pem)
        if a.pub and not verify(index, sig, a.pub):
            die(f"the signature does not match {a.pub}: the key is not the pair of the one in the kernel")
    print(f"mkmarket: catalog {serial}, {len(games)} games in {a.out}" + (", signed" if key_pem.strip() else ""))


if __name__ == "__main__":
    main()
