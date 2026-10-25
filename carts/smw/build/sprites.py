"""The sprites of the Super Mario World cartridge as bm SPRITES zones with BOXES.

pack_zones() puts the pictures captured from the player's ROM (scripts/smw_sprites.py, smw_enemies.py,
smw_items.py, smw_goal.py) under the tile cells of the sheet, one named zone per animation: the frames
side by side as the suite wants them (zspr, zboxes, bm Pixel). It writes the zones file for
mkbm.py --sprites and returns the Lua text with the anchors and the places of the Koopas.

A zone is a rectangle; the game draws it with zspr(name, x, y, frame, flip) where (x, y) is the
rectangle's corner. The suite has no anchors, so each zone gets the offset ZA[name] = {dx, dy}: draw at
(sprite x + dx, sprite y + dy). The picture sits inside its rectangle so that a mirrored zone is mirrored
about the sprite (Mario's rectangle is 32 wide with his 16-pixel body in the middle).

Boxes (kind frame x y w h, from the rectangle's corner): Mario has hurt (his body), box 3 (his
feet: he stomps with them), box 4 (his body, to kick a shell). A Koopa has hurt (its head: it can be
stomped there) and hit (its body); a sliding shell has both."""
import json, os

MARIO = [("mario_stand", [0x00]), ("mario_walk", [0x00, 0x01]), ("mario_look", [0x03]), ("mario_prun", [0x07, 0x08]),
         ("mario_jump", [0x0B]), ("mario_pjump", [0x0C]), ("mario_skid", [0x0D]), ("mario_fall", [0x24]),
         ("mario_duck", [0x3C]), ("mario_spin", [0x25, 0x00, 0x0F, 0x00]), ("mario_dead", [0x3E])]
KOOPA = [("koopa_l", ["05_8_42ad5a82", "05_8_04c81fb9"]), ("koopa_r", ["05_8_2f5730ba", "05_8_c999e5e3"]),
         ("koopa_turn", ["05_8_5225aa56"]), ("shell", ["05_9_d229f1b8"]), ("shell_slide", ["05_9_d229f1b8"])]
EGG = ["126_2C_79cad8", "126_2C_c7a607", "126_2C_1c9ca6"]
YOSHI = ["126_35_1c9ca6", "126_35_017398", "126_35_d3ea17", "126_35_eaad07"]


def pack_zones(ex, tiles_png, out_png, out_txt, restyle=None):
    from PIL import Image
    sheet = Image.open(tiles_png).convert("RGBA")
    zones = []          # name, w, h, [images with their place in the rectangle], anchor, boxes

    meta = json.load(open(ex + "/mario/poses.json"))
    for name, poses in MARIO:
        frames = []
        for pose in poses:
            m = meta.get("%02X_1" % pose)
            mirror = m is None
            if mirror:
                m = meta["%02X_0" % pose]
            im = Image.open(ex + "/mario/" + m["file"]).convert("RGBA")
            ox = m["ox"]
            if mirror:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
                ox = 16 - m["ox"] - m["w"]
            frames.append((im, ox + 8, m["oy"] + 8))
        zones.append((name, 32, 48, frames, (-8, -8),
                      ["hurt * 10 24 12 16", "box 3 * 10 36 12 4", "box 4 * 10 24 12 16"]))

    en = json.load(open(ex + "/enemies/pics.json"))
    for name, pics in KOOPA:
        frames = [(Image.open(ex + "/enemies/" + en[p]["file"]).convert("RGBA"), en[p]["ox"], en[p]["oy"] + 16) for p in pics]
        if name == "shell":
            boxes = ["hurt * 0 16 16 16"]
        elif name == "shell_slide":
            boxes = ["hurt * 0 16 16 16", "hit * 0 16 16 16"]
        else:
            boxes = ["hurt * 0 16 16 10", "hit * 0 16 16 16"]
        zones.append((name, 16, 32, frames, (0, -16), boxes))

    # sprites captured by taking them out of the screen (scripts/smw_diff.py): the pictures are exact
    def dpic(sid, key):
        m = json.load(open(ex + "/diff/%s/pics.json" % sid))[key]
        return Image.open(ex + "/diff/%s/%s" % (sid, m["file"])).convert("RGBA"), m["ox"], m["oy"]
    zones.append(("plant", 24, 40, [(im, ox, oy + 8) for im, ox, oy in (dpic("4F", "4182a38b"), dpic("4F", "3f21bea6"))],
                  (0, -8), ["hit * 5 14 12 16"]))
    zones.append(("flower", 24, 16, [dpic("4E", k) for k in ("75c96d8a", "f4009734", "109bbf7b", "6a4ba8c4")], (0, 0), []))
    zones.append(("tuft", 24, 16, [dpic("4D", k) for k in ("5ecb4264", "ec7f6dbc", "c0f14197")], (0, 0), []))
    # the Chargin' Chuck: a 32x48 cell with the sprite's box at (8, 16); pictures facing left
    def chuck(keys):
        return [(im, ox + 8, oy + 16) for im, ox, oy in (dpic("91", k) for k in keys)]
    cboxes = ["hurt * 8 16 16 8", "hit * 8 16 16 16"]
    zones.append(("chuck_idle", 32, 48, chuck(["aa29e8dc", "5cfd864c"]), (-8, -16), cboxes))
    zones.append(("chuck_run", 32, 48, chuck(["037b87f5", "0200dd16", "31089d4c", "461a6571"]), (-8, -16), cboxes))
    zones.append(("chuck_hurt", 32, 48, chuck(["cf1110af", "24bca37b", "49d7cc10"]), (-8, -16), ["hurt * 8 16 16 8"]))
    zones.append(("chuck_wait", 32, 48, chuck(["ee58e211", "ae3248ae"]), (-8, -16), cboxes))
    zones.append(("msg", 24, 16, [dpic("B9", k) for k in ("35de3170", "00414886")], (0, 0), []))
    zones.append(("pswitch", 24, 16, [dpic("3E", "93ac4b17")], (0, 0), []))

    items = json.load(open(ex + "/items.json"))
    for name, pics in (("egg", EGG), ("yoshi", YOSHI)):
        frames = [(Image.open(ex + "/item_%s.png" % p).convert("RGBA"), items[p]["ox"], items[p]["oy"]) for p in pics]
        zones.append((name, 32, 32, frames, (0, 0), []))

    tape = json.load(open(ex + "/goal/goal.json"))
    zones.append(("tape", tape["w"], tape["h"], [(Image.open(ex + "/goal/tape.png").convert("RGBA"), 0, 0)],
                  (tape["ox"], tape["oy"]), []))

    height = sheet.height + sum(z[2] for z in zones)
    big = Image.new("RGBA", (sheet.width, height), (0, 0, 0, 0))
    big.paste(sheet, (0, 0))
    txt, anchors = [], []
    y = sheet.height
    for name, w, h, frames, anchor, boxes in zones:
        assert w * len(frames) <= sheet.width, name
        for i, (im, px, py) in enumerate(frames):
            assert 0 <= px and px + im.width <= w and 0 <= py and py + im.height <= h, (name, px, py, im.size)
            if restyle:
                im = restyle(im)
            big.paste(im, (i * w + px, y + py))
        txt.append("%s 0 %d %d %d %d %d" % (name, y, w, h, len(frames), 8 if len(frames) > 1 else 1))
        txt += ["  " + b for b in boxes]
        anchors.append("  %s = { %d, %d }," % (name, anchor[0], anchor[1]))
        y += h
    big.save(out_png)
    open(out_txt, "w").write("\n".join(txt) + "\n")

    spawns = json.load(open(ex + "/spawns.json")) if os.path.exists(ex + "/spawns.json") else []
    seen, sp = set(), []
    for s in spawns:
        if s["id"] == 5 and (s["x"], s["y"]) not in seen:
            seen.add((s["x"], s["y"]))
            sp.append("  { %d, %d, %d, %d }," % (s["x"], s["y"], s["face"], 1 if s["status"] == 9 else 0))
    decor = []
    for s in spawns:
        if s["id"] in (0x4E, 0x4D) and (s["id"], s["x"], s["y"]) not in seen:
            seen.add((s["id"], s["x"], s["y"]))
            decor.append("  { %s, %d, %d }," % ('"flower"' if s["id"] == 0x4E else '"tuft"', s["x"], s["y"]))
    plants = [(s["x"], s["y"]) for s in spawns if s["id"] == 0x4F][:1]
    chucks = sorted({(s["x"], s["y"]) for s in spawns if s["id"] == 0x91})
    rows = [r.split(",") for r in open(ex + "/enemies/states.csv").read().splitlines()[1:]]
    path = [int(r[5]) for r in rows if int(r[2]) == 0x4F]               # its y, a tick at a time, going up
    plant_lua = "local CHUCKS = { %s }\n" % ", ".join("{ %d, %d }" % c for c in chucks)
    plant_lua += "local PLANT_AT = { %s }\nlocal PLANT_PATH = { %s }\n" % (
        ", ".join("{ %d, %d }" % p for p in plants), ", ".join(str(y) for y in path))
    fixed = [(s["id"], s["x"], s["y"]) for s in spawns if s["id"] in (0xB9, 0x3E)]
    fixed = sorted(set(fixed))
    fixed_lua = "local MSGS = { %s }\nlocal PSWITCHES = { %s }\n" % (
        ", ".join("{ %d, %d }" % (x, y) for i, x, y in fixed if i == 0xB9), ", ".join("{ %d, %d }" % (x, y) for i, x, y in fixed if i == 0x3E))
    return ("local ZA = {   -- the corner of each zone from the sprite's x, y (the suite has no anchors)\n" + "\n".join(anchors) + "\n}\n"
            "local TAPE_X = %d\n" % tape["x"] + plant_lua + fixed_lua +
            "local DECOR = {   -- kind, x, y\n" + "\n".join(decor) + "\n}\n" +
            "local KOOPAS = {   -- x, y, face (1 left, 0 right), starts as a shell\n" + "\n".join(sp) + "\n}\n")
