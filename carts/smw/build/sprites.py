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
YOSHI = ["126_35_1c9ca6", "126_35_017398", "126_35_d3ea17", "126_35_0e92a7"]


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
    return ("local ZA = {   -- the corner of each zone from the sprite's x, y (the suite has no anchors)\n" + "\n".join(anchors) + "\n}\n"
            "local TAPE_X = %d\n" % tape["x"] +
            "local KOOPAS = {   -- x, y, face (1 left, 0 right), starts as a shell\n" + "\n".join(sp) + "\n}\n")
