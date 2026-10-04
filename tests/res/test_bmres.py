#!/usr/bin/env python3
"""Resource files (scripts/bmres.py, docs/RISORSE.md): the container, INFO
and SPRITES, extraction and integration (the same sections back in an
empty project; the same pixels under every face, zone and tile in a full
one), conversions, broken files.

  tests/res/test_bmres.py VILLAGE.bm DEMO.bm SOUND.bm OUTDIR
"""
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmaudio  # noqa: E402
import bmres  # noqa: E402
import mkbm  # noqa: E402

fails, checks = [], 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        fails.append(msg)
        print("FAIL", msg)


def raises(fn, msg):
    try:
        fn()
    except bmres.ResError:
        check(True, msg)
        return
    check(False, msg + " (no error)")


def empty_cart():
    return bmres.load(mkbm.pack(b"-- empty", title="empty"))


def roundtrip(f):
    return bmres.load(bmres.dump(f))


def px(sheet, x, y):
    w, h, rgba = sheet
    p = bytes(rgba[4 * (y * w + x):4 * (y * w + x) + 4])
    return p if p[3] >= 128 else bmres.CLEAR


def block(sheet, x, y, w, h):
    return [px(sheet, x + i, y + j) for j in range(h) for i in range(w)]


def face_texels(f, model):
    """for each textured face of a model: the pixels under its corners' box"""
    sheet = bmres.sheet_get(f)
    recs = dict(bmres.mesh_parse(f.get(bmres.SEC_MESH))[1])
    out = []
    for _, colour, uv in bmres.rec_faces(recs[model]):
        if colour & bmres.TEXTURED:
            us, vs = [t // 8 for t in uv[0::2]], [t // 8 for t in uv[1::2]]
            x0, y0 = min(us), min(vs)
            w, h = max(1, max(us) - x0), max(1, max(vs) - y0)
            out.append(block(sheet, x0, y0, min(w, sheet[0] - x0), min(h, sheet[1] - y0)))
    return out


def kept(old, new):
    """every pixel drawn in the old sheet is the same in the new one (the clear ones may take new pixels)"""
    w, h, rgba = old
    return all(px(new, x, y) == px(old, x, y) for y in range(h) for x in range(w) if rgba[4 * (y * w + x) + 3] >= 128)


def map_pixels(f):
    """the map drawn: the pixels of every cell's tile (None for the empty ones)"""
    w, h, rgba = bmres.sheet_get(f)
    mw, mh, cells = bmres.map_get(f)
    per, n = w // 8, (w // 8) * (h // 8)
    return [block((w, h, rgba), c % per * 8, c // per * 8, 8, 8) if 0 < c < n else None for c in cells]


def sections(f, skip=(bmres.SEC_INFO,)):
    return {t: b for t, b in f.sections if t not in skip}


# ------------------------------------------------------------------ tests

def test_info_sprites():
    text = b"name: Knights\nlicense: CC-BY-4.0\ntags: a, b\n\n[model knight]\ndesc: with a sword\nweird key: kept\n"
    info = bmres.info_parse(text)
    check(info["file"][0] == ("name", "Knights") and info["items"][0][:2] == ["model", "knight"], "INFO parsed")
    check(bmres.info_parse(bmres.info_dump(info)) == info, "INFO back the same")
    check(bmres.kv_get(info["items"][0][2], "weird key") == "kept", "INFO keeps unknown keys")
    raises(lambda: bmres.info_parse(b"x: " + b"a" * bmres.INFO_MAX), "INFO over 16 KiB")
    zones = [{"name": "hero", "x": 0, "y": 8, "w": 16, "h": 16, "frames": 4, "fps": 8},
             {"name": "coin", "x": 64, "y": 0, "w": 8, "h": 8, "frames": 1, "fps": 0}]
    body = bmres.sprites_encode(zones)
    check(len(body) == 4 + 28 * 2 and bmres.sprites_decode(body) == zones, "SPRITES back the same")
    raises(lambda: bmres.sprites_decode(bmres.sprites_encode(zones + [zones[0]])), "SPRITES with a name twice")
    raises(lambda: bmres.sprites_decode(body[:-1]), "SPRITES cut short")


def test_container(village):
    f = bmres.extract_models(village, ["house"])
    data = bmres.dump(f)
    check(data[:8] == b"BMRES\0\0\0" and struct.unpack_from("<H", data, 12)[0] == bmres.MODEL, "BMRES header")
    g = bmres.load(data)
    check(g.kind == bmres.MODEL and g.name == "house" and g.sections == f.sections, "BMRES read back")
    check(bmres.dump(g) == data, "BMRES written the same")
    bad = bytearray(data)
    bad[-1] ^= 1
    raises(lambda: bmres.load(bytes(bad)), "a broken CRC")
    raises(lambda: bmres.load(data[:100]), "a file cut short")
    lua = bmres.File(bmres.MODEL, sections=[[bmres.SEC_LUA, b"x"]] + f.sections)
    raises(lambda: bmres.dump(lua), "code in a resource file")
    snd = bmres.File(bmres.SOUND, sections=[s for s in f.sections])
    raises(lambda: bmres.dump(snd), "MESH in a sounds file")
    raises(lambda: bmres.dump(bmres.File(bmres.KIT)), "an empty kit")
    odd = bmres.File(bmres.MODEL, sections=f.sections + [[77, b"future"]])
    check(roundtrip(odd).get(77) == b"future", "unknown sections kept")
    mesh = f.get(bmres.SEC_MESH)
    broken = bmres.File(bmres.MODEL, sections=[[bmres.SEC_MESH, mesh[:-3]]])
    raises(lambda: bmres.dump(broken), "a broken MESH")


def test_models(village, demo, out):
    recs = [n for n, _ in bmres.mesh_parse(village.get(bmres.SEC_MESH))[1]]
    vsheet = bmres.sheet_get(village)
    for name in recs:
        res = roundtrip(bmres.extract_models(village, [name]))
        check(face_texels(res, name) == face_texels(village, name), f"{name}: the same texels in the .bmm")
        if res.get(bmres.SEC_SHEET8) is not None or res.get(bmres.SEC_SHEET) is not None:
            s = bmres.sheet_get(res)
            check(s[0] * s[1] < vsheet[0] * vsheet[1], f"{name}: only the cells it uses")
        cart = empty_cart()
        bmres.integrate(cart, res, "x")
        cart = roundtrip(cart)
        for t, b in sections(res).items():
            check(cart.get(t) == b, f"{name}: {bmres.SEC_NAMES[t]} the same in an empty project")
        again = bmres.extract_models(cart, [name])
        check(sections(again) == sections(res), f"{name}: extracted again, the same")
    house = roundtrip(bmres.extract_models(village, ["house", "villager"]))
    check([n for n, _ in bmres.mesh_parse(house.get(bmres.SEC_MESH))[1]] == ["house", "villager"], "two models")
    check(house.get(bmres.SEC_ANIM) is not None, "the skeleton goes with its model")
    bmres.write(os.path.join(out, "HOUSE.bmm"), house)

    # into a cartridge with its own sheet and map: nothing of it changes
    cart = roundtrip(demo)
    before_sheet, before_map = bmres.sheet_get(cart), map_pixels(cart)
    bmres.integrate(cart, house, "x")
    bmres.integrate(cart, house, "y")
    cart = roundtrip(cart)
    after = bmres.sheet_get(cart)
    check(after[0] == before_sheet[0], "the sheet keeps its width")
    check(kept(before_sheet, after), "the old pixels stay")
    check(map_pixels(cart) == before_map, "the map draws the same")
    names = [n for n, _ in bmres.mesh_parse(cart.get(bmres.SEC_MESH))[1]]
    check(names == ["house", "villager", "house2", "villager2"], f"names made unique: {names}")
    check([n for n, _ in bmres.anim_parse(cart.get(bmres.SEC_ANIM))] == ["villager", "villager2"],
          "skeletons renamed with their models")
    for n, m in (("house", "house"), ("house", "house2")):
        check(face_texels(cart, m) == face_texels(house, n), f"{m}: the same texels in the cartridge")
    info = bmres.info_of(cart)
    it = bmres.info_item(info, "model", "house2")
    check(it is not None and bmres.kv_get(it[2], "origin") == "y", "INFO of the part with its origin")
    bmres.write(os.path.join(out, "demo_models.bm"), cart)

    # a sheet that is full: it grows down, never wider; a piece wider than it does not go in
    def tight():
        c = empty_cart()
        bmres.sheet_set(c, 32, 8, bytearray(b"\x10\x20\x30\xff" * (32 * 8)))
        return c
    src = empty_cart()
    bmres.sheet_set(src, 64, 8, bytearray(b"\x90\x20\x30\xff" * (64 * 8)))
    narrow = roundtrip(bmres.extract_image(roundtrip(src), rect=(0, 0, 16, 8), name="n"))
    wide = roundtrip(bmres.extract_image(roundtrip(src), rect=(0, 0, 64, 8), name="w"))
    c = tight()
    bmres.integrate(c, narrow)
    check(bmres.sheet_get(c)[:2] == (32, 16), "a full sheet grows down")
    raises(lambda: bmres.integrate(tight(), wide), "a piece wider than the sheet")


def test_image(demo, out):
    full = roundtrip(bmres.extract_image(demo))
    check(bmres.sheet_get(full)[2] == bmres.sheet_get(demo)[2], "the whole sheet as an image")
    zone = roundtrip(bmres.extract_image(demo, rect=(8, 0, 16, 8), name="flag", frames=2, fps=6))
    zones = bmres.sprites_decode(zone.get(bmres.SEC_SPRITES))
    check(zones[0]["name"] == "flag" and zones[0]["w"] == 8 and zones[0]["frames"] == 2, "a zone with frames")
    z = zones[0]
    check(block(bmres.sheet_get(zone), z["x"], z["y"], 16, 8) == block(bmres.sheet_get(demo), 8, 0, 16, 8),
          "the zone's pixels")
    cart = empty_cart()
    bmres.integrate(cart, zone)
    check(sections(roundtrip(cart), (bmres.SEC_INFO, bmres.SEC_LUA)) == sections(zone), "image: the same in an empty project")
    cart = roundtrip(demo)
    bmres.integrate(cart, zone)
    bmres.integrate(cart, zone)
    zs = bmres.sprites_decode(cart.get(bmres.SEC_SPRITES))
    check([q["name"] for q in zs] == ["flag", "flag2"], "zones made unique")
    for q in zs:
        check(block(bmres.sheet_get(cart), q["x"], q["y"], 16, 8) == block(bmres.sheet_get(demo), 8, 0, 16, 8),
              f"{q['name']}: the same pixels in the cartridge")
    bmres.write(os.path.join(out, "FLAG.bmi"), zone)
    again = bmres.extract_image(roundtrip(cart), sprites=["flag2"])
    check(block(bmres.sheet_get(again), 0, 0, 16, 8) == block(bmres.sheet_get(demo), 8, 0, 16, 8),
          "a named zone extracted")


def test_sounds(sound, out):
    whole = roundtrip(bmres.extract_sounds(sound))
    check(whole.get(bmres.SEC_AUDIO) == sound.get(bmres.SEC_AUDIO), "the whole bank, byte for byte")
    bank = bmaudio.unpack(sound.get(bmres.SEC_AUDIO))
    one = roundtrip(bmres.extract_sounds(sound, sfx=["JUMP"]))
    b1 = bmaudio.unpack(one.get(bmres.SEC_AUDIO))
    check(len(b1["sfx"]) == 1 and b1["sfx"][0]["name"] == "JUMP" and not b1["songs"], "one sound effect")
    used = {bmres._step_sound(s) for s in b1["sfx"][0]["steps"]} - {None}
    check(len(b1["sounds"]) == len(used) and max(used) < len(b1["sounds"]), "only the sounds it plays")
    orig = bank["sfx"][1]
    for a, b in zip(orig["steps"], b1["sfx"][0]["steps"]):
        sa, sb = bmres._step_sound(a), bmres._step_sound(b)
        if sa is not None:
            check(bank["sounds"][sa] == b1["sounds"][sb], "the steps play the same sounds")
    song = roundtrip(bmres.extract_sounds(sound, songs=["CHILL"]))
    b2 = bmaudio.unpack(song.get(bmres.SEC_AUDIO))
    check(len(b2["songs"]) == 1 and len(b2["patterns"]) == len(set(bank["songs"][1]["order"])), "a song and its patterns")
    cart = empty_cart()
    bmres.integrate(cart, one)
    check(cart.get(bmres.SEC_AUDIO) == one.get(bmres.SEC_AUDIO), "sounds: the same in an empty project")
    bmres.integrate(cart, song)
    b3 = bmaudio.unpack(roundtrip(cart).get(bmres.SEC_AUDIO))
    check([x["name"] for x in b3["sfx"]] == ["JUMP"] and [g["name"] for g in b3["songs"]] == ["CHILL"],
          "two files of sounds together")
    check(len(b3["sounds"]) <= len(b1["sounds"]) + len(b2["sounds"]), "identical sounds reused")
    bmres.write(os.path.join(out, "JUMP.bms"), one)


def test_map(demo, village, out):
    res = roundtrip(bmres.extract_map(demo))
    check(map_pixels(res) == map_pixels(demo), "the map draws the same in the .bmt")
    s = bmres.sheet_get(res)
    check(s[0] * s[1] <= bmres.sheet_get(demo)[0] * bmres.sheet_get(demo)[1], "only the tiles it uses")
    cart = empty_cart()
    bmres.integrate(cart, res)
    check(sections(roundtrip(cart), (bmres.SEC_INFO, bmres.SEC_LUA)) == sections(res), "map: the same in an empty project")
    cart = roundtrip(village)
    old = bmres.sheet_get(cart)
    bmres.integrate(cart, res)
    cart = roundtrip(cart)
    check(map_pixels(cart) == map_pixels(demo), "the map draws the same in another cartridge")
    check(kept(old, bmres.sheet_get(cart)), "its sheet keeps its pixels")
    for n in ("house", "well"):
        check(face_texels(cart, n) == face_texels(village, n), f"{n}: its texture stays")
    raises(lambda: bmres.integrate(cart, res), "a second map")
    bmres.write(os.path.join(out, "DEMO.bmt"), res)


def layer_pixels(f, i):
    """layer i drawn: the pixels of every cell's tile, and the flags of the tile"""
    w, h, rgba = bmres.sheet_get(f)
    flags = bmres.flags_get(f)
    per, n = w // 8, (w // 8) * (h // 8)
    return [(block((w, h, rgba), c % per * 8, c // per * 8, 8, 8), flags.get((c % per, c // per), 0))
            if 0 < c < n else None for c in bmres.layers_get(f)[i][1]]


def layered_cart():
    """a cartridge with two layers and the flags of its tiles (R11)"""
    cols = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255), (255, 255, 255)]
    rgba = bytearray(32 * 16 * 4)
    for c in range(1, 8):
        for y in range(8):
            for x in range(8):
                o = 4 * ((c // 4 * 8 + y) * 32 + c % 4 * 8 + x)
                rgba[o:o + 4] = bytes(cols[c - 1]) + bytes([255 if (x + y) % 3 else 0])
    layers = [("main", 3, 2, struct.pack("<6H", 1, 2, 0, 0, 3, 1)), ("front", 3, 2, struct.pack("<6H", 0, 0, 4, 5, 0, 0))]
    return bmres.load(mkbm.pack(b"-- layered", (32, 16, bytes(rgba)), title="layered", layers=layers,
                                flags={1: 1, 4: 6, 7: 9}))


def test_layers_flags(village, out):
    src = layered_cart()
    check([n for n, _ in bmres.layers_get(src)] == ["main", "front"], "mkbm: two layers")
    check(bmres.flags_get(src) == {(1, 0): 1, (0, 1): 6, (3, 1): 9}, "mkbm: the flags by the cell's place")
    res = roundtrip(bmres.extract_map(src))
    check([n for n, _ in bmres.layers_get(res)] == ["main", "front"], "the .bmt keeps the layers")
    check(all(layer_pixels(res, i) == layer_pixels(src, i) for i in (0, 1)),
          "each layer draws the same in the .bmt, its tiles with their flags")
    check(len(bmres.flags_get(res)) == 2, "only the flags of the tiles the map uses")
    cart = empty_cart()
    bmres.integrate(cart, res)
    check(sections(roundtrip(cart), (bmres.SEC_INFO, bmres.SEC_LUA)) == sections(res),
          "layers and flags: the same in an empty project")
    cart = roundtrip(village)
    bmres.integrate(cart, res)
    cart = roundtrip(cart)
    check(all(layer_pixels(cart, i) == layer_pixels(src, i) for i in (0, 1)),
          "the layers draw the same in another cartridge, the flags moved with their tiles")
    bmres.write(os.path.join(out, "LAYERS.bmt"), res)
    bmres.convert(os.path.join(out, "LAYERS.bmt"), os.path.join(out, "layers.csv"))
    check(os.path.exists(os.path.join(out, "layers_front.csv")), ".bmt -> .csv: the other layer beside")
    w = bmres.sheet_get(res)[0] // 8
    fl = mkbm.read_flags(os.path.join(out, "layers_flags.csv"))
    check(fl == {cy * w + cx: v for (cx, cy), v in bmres.flags_get(res).items()},
          ".bmt -> .csv: the flags as mkbm.py --flags reads them")
    bad = roundtrip(src)
    bad.drop(bmres.SEC_MAP)
    raises(lambda: bmres.check(bad), "LAYERS without a map")
    bad = roundtrip(src)
    bad.put(bmres.SEC_LAYERS, bad.get(bmres.SEC_LAYERS)[:-2])
    raises(lambda: bmres.check(bad), "LAYERS of the wrong size")


def test_palette(village, demo, out):
    pal = roundtrip(bmres.extract_palette(village))
    cols = [c for c in bmres.sheet8_palette(village.get(bmres.SEC_SHEET8)) if c[3] >= 128]
    w, h, rgba = bmres.sheet_get(pal)
    check(h == 1 and [bytes(rgba[4 * i:4 * i + 4]) for i in range(w)] == cols, "pixel i is colour i")
    small = bmres.palette_file(cols[:8], "eight")
    cart = roundtrip(demo)
    before = bmres.sheet_get(cart)
    bmres.integrate(cart, small)
    cart = roundtrip(cart)
    check(bmres.sheet8_palette(cart.get(bmres.SEC_SHEET8))[:8] == cols[:8], "the palette first in the sheet's")
    check(bmres.sheet_get(cart)[2] == before[2], "the pixels stay")
    raises(lambda: bmres.integrate(roundtrip(demo), pal), "more than 256 colours together")
    bmres.write(os.path.join(out, "VILLAGE.bmc"), pal)


def test_kit(village):
    kit = roundtrip(bmres.extract_kit(village))
    check(set(sections(kit)) == {bmres.SEC_SHEET8, bmres.SEC_MESH, bmres.SEC_ANIM}, "a kit of a game")
    cart = empty_cart()
    bmres.integrate(cart, kit)
    check(sections(roundtrip(cart), (bmres.SEC_INFO, bmres.SEC_LUA)) == sections(kit), "kit: the same in an empty project")


def test_convert(demo, sound, out):
    png = os.path.join(out, "sheet.png")
    w, h, rgba = bmres.sheet_get(demo)
    bmres.png_write(png, w, h, rgba)
    img = bmres.convert(png, os.path.join(out, "SHEET.bmi"))
    check(bmres.sheet_get(roundtrip(img))[2] == bytearray(b"".join(
        bmres._norm(rgba[i:i + 4]) for i in range(0, len(rgba), 4))), ".png -> .bmi")
    bmres.write(os.path.join(out, "SHEET.bmi"), img)
    bmres.convert(os.path.join(out, "SHEET.bmi"), os.path.join(out, "back.png"))
    check(mkbm.read_png(os.path.join(out, "back.png"))[:2] == (w, h), ".bmi -> .png")
    js = os.path.join(out, "bank.json")
    bmres.write(os.path.join(out, "BANK.bms"), bmres.extract_sounds(sound))
    bmres.convert(os.path.join(out, "BANK.bms"), js)
    check(bmaudio.pack(json.load(open(js))) == sound.get(bmres.SEC_AUDIO), ".bms -> .json -> the same bank")
    hexf = os.path.join(out, "pal.hex")
    open(hexf, "w").write("ff0000\n00ff00\n#0000ff\n")
    pal = bmres.convert(hexf, os.path.join(out, "P.bmc"))
    check([c for c in bmres.sheet8_palette(pal.get(bmres.SEC_SHEET8))] == [b"\xff\0\0\xff", b"\0\xff\0\xff", b"\0\0\xff\xff"],
          ".hex -> .bmc")
    bmres.write(os.path.join(out, "P.bmc"), pal)
    bmres.convert(os.path.join(out, "P.bmc"), os.path.join(out, "p.gpl"))
    check(bmres.read_gpl(os.path.join(out, "p.gpl")) == [b"\xff\0\0\xff", b"\0\xff\0\xff", b"\0\0\xff\xff"], ".bmc -> .gpl")
    bmres.write(os.path.join(out, "MAP.bmt"), bmres.extract_map(demo))
    csv = os.path.join(out, "map.csv")
    bmres.convert(os.path.join(out, "MAP.bmt"), csv)
    back = bmres.convert(csv, os.path.join(out, "MAP2.bmt"), tiles=os.path.join(out, "map.png"))
    check(map_pixels(roundtrip(back)) == map_pixels(demo), ".bmt -> .csv + .png -> .bmt")


def test_cli(village_path, out):
    bmm = os.path.join(out, "WELL.bmm")
    check(bmres.main(["extract", village_path, "-o", bmm, "--model", "well", "--license", "CC0-1.0",
                      "--tags", "village"]) == 0, "bmres extract")
    f = bmres.read(bmm)
    check(bmres.kv_get(bmres.info_of(f)["file"], "license") == "CC0-1.0", "the command line writes INFO")
    cart = os.path.join(out, "cli.bm")
    open(cart, "wb").write(mkbm.pack(b"-- cli", title="cli"))
    check(bmres.main(["add", cart, bmm]) == 0, "bmres add")
    check("well" in dict(bmres.mesh_parse(bmres.read(cart).get(bmres.SEC_MESH))[1]), "the model in the cartridge")
    origin = hashlib.sha256(open(bmm, "rb").read()).hexdigest()
    it = bmres.info_item(bmres.info_of(bmres.read(cart)), "model", "well")
    check(it and bmres.kv_get(it[2], "origin") == origin and bmres.kv_get(it[2], "license") == "CC0-1.0",
          "origin and license follow the model")
    check(bmres.main(["info", bmm, "--item", "model", "well", "--desc", "a well"]) == 0, "bmres info")
    check(bmres.main(["extract", village_path, "-o", os.path.join(out, "X.bmm"), "--model", "nope"]) == 1,
          "a model that is not there")
    check(bmres.main(["list", bmm, cart]) == 0, "bmres list")


def main():
    village_path, demo_path, sound_path, out = sys.argv[1:5]
    os.makedirs(out, exist_ok=True)
    village, demo, sound = (bmres.read(p) for p in (village_path, demo_path, sound_path))
    test_info_sprites()
    test_container(village)
    test_models(village, demo, out)
    test_image(demo, out)
    test_sounds(sound, out)
    test_map(demo, village, out)
    test_layers_flags(village, out)
    test_palette(village, demo, out)
    test_kit(village)
    test_convert(demo, sound, out)
    test_cli(village_path, out)
    print(f"bmres: {checks - len(fails)}/{checks} checks passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
