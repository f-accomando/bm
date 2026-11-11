#!/usr/bin/env python3
"""
render_test.py - the bm Tape cartridge in bmhost: pictures of the thirteen pages and the eight engines, keys, the error path.

  python3 tests/tape/render_test.py build/host/bmhost-bin build/carts/tape.bm

bmhost runs the engine (src/bm/bmnative.c emulates ARMv6 on the PC), so native_run executes here.
Checks: the run ends without a Lua error; each of the 20 pictures (thirteen pages, the other seven engines) is not blank, holds
the four encoder colours in the inspector and shows no engine error; the pictures differ; PREVIEW is on the DJ page
only; the pattern letter and PAT / SONG are on the status strip and change with ; ' and P; the TAPE keys T, Backspace and
Shift+Backspace write and clear clips; the sun of the PCM scene has the colour of encoder 2 (CUTOFF); Tab and the arrows go to the right pages; for every page and every encoder, turning the encoder
changes the scene (two runs with the same frames, so the animation is the same); SEQ GRID shows the demo pattern, with
the rim steps of the native euclid; Space runs the transport; Enter shows REC and a recorded hit lands in the grid;
the live keys play all 44 sounds without a Lua error; the PCM scene shows the sample of its sound (name and length from
audio_samples()) and audio_samples() in the cartridge lists the 24 samples of the bank; a cartridge with a broken engine.b still draws, shows the error
text on the status strip, and Space does nothing.
"""
import hashlib, os, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

INK, HOT = (0x0c, 0x08, 0x20), (0xff, 0x6a, 0x3d)
ENC = [(0x3f, 0xe8, 0xf5), (0xff, 0x4f, 0xae), (0xff, 0xc5, 0x3d), (0xf4, 0xf0, 0xff)]
TAB, RIGHT, LEFT, UP, SPACE, ENTER = 0x2B, 0x4F, 0x50, 0x52, 0x2C, 0x28
MOD = [0x3A + i for i in range(8)]       # the modules: F1 to F8
ENCKEYS = [(0x26, 0x12), (0x27, 0x13), (0x2D, 0x2F), (0x2E, 0x30)]     # 9/O 0/P -/[ =/]
KEY_2, KEY_U, RB, KEY_O = 0x1F, 0x07, 0xE6, 0x19      # track 2, the 4th piano key (D), next sound (right option), pad 6 (V)
KEY_T, KEY_BK, KEY_P, KEY_SEMI, KEY_QUOTE = 0x04, 0x2A, 0x31, 0xE3, 0xE7   # A (set), delete, backslash, left and right command


def read_png(path):
    d = open(path, "rb").read()
    pos, idat, w = 8, b"", 0
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
            bpp = {2: 3, 6: 4}[ct]
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev, p = w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, bpp, rows


def near(c, want, tol=8):
    """The screen keeps 5 or 6 bits per channel: a colour comes back within a few units."""
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def has(seen, want):
    return any(near(c, want) for c in seen)


def colours(img, x0, y0, x1, y1):
    """The set of colours in a rectangle, and how many pixels are not ink."""
    w, h, bpp, rows = img
    seen, lit = set(), 0
    for y in range(y0, min(y1, h)):
        r = rows[y]
        for x in range(x0, min(x1, w)):
            c = tuple(r[x * bpp:x * bpp + 3])
            seen.add(c)
            lit += not near(c, INK)
    return seen, lit


def crop_hash(img, x0, y0, x1, y1):
    w, h, bpp, rows = img
    return hashlib.md5(b"".join(rows[y][x0 * bpp:x1 * bpp] for y in range(y0, y1))).hexdigest()


def run(host, cart, script, shots, seconds):
    os.makedirs(shots, exist_ok=True)
    inp = os.path.join(os.path.dirname(shots), os.path.basename(shots) + ".txt")
    open(inp, "w").write(script)
    p = subprocess.run([host, cart, "--shots", shots, "--input", inp, "--seconds", str(seconds), "--quiet"],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def key(frame, usage, n=1, gap=6):
    """Script lines: the key is down for 2 frames, n times, gap frames apart."""
    out = []
    for i in range(n):
        out += ["%d keys %d" % (frame + gap * i, usage), "%d keys none" % (frame + gap * i + 2)]
    return out


PAGES = ["SYNTH ENGINE", "SYNTH ENVELOPE", "SYNTH FILTER", "DRUM PAD", "SEQ GRID", "TAPE", "MIX LEVEL", "MIX PAN", "FX ECHO",
         "FX ROOM", "FX CHORUS", "DJ", "TEMPO"]
PREVIEW = {11}                                          # indexes of the pages that make no sound (DJ)
ENGINES = ["FM", "STRING", "PULSE", "CLUSTER", "ORGAN", "VOX", "DIGITAL", "PCM"]
# the keys that bring a page up from the start: MOD[0] to F8, Tab inside SYNTH
GOTO = {0: key(10, MOD[0]), 1: key(10, MOD[0]) + key(16, TAB), 2: key(10, MOD[0]) + key(16, TAB) + key(22, TAB),
        3: key(10, MOD[1]), 4: key(10, MOD[2]), 5: key(10, MOD[3]), 6: key(10, MOD[4]), 7: key(10, MOD[4]) + key(16, TAB),
        8: key(10, MOD[5]), 9: key(10, MOD[5]) + key(16, TAB), 10: key(10, MOD[5]) + key(16, TAB) + key(22, TAB),
        11: key(10, MOD[6]), 12: key(10, MOD[7])}


def script(lines):
    return "\n".join(sorted(lines, key=lambda l: int(l.split()[0]))) + "\n"


def main():
    host, cart = sys.argv[1:3]
    tmp = tempfile.mkdtemp(prefix="tape-test-")
    fails = []

    def check(name, ok):
        if not ok: fails.append(name)
        print(("ok   " if ok else "FAIL ") + name)

    def one(name, lines, seconds=2, shot_at=None):
        """One run; the picture at the given frame (default: the last line of the script)."""
        shot_at = shot_at or max(int(l.split()[0]) for l in lines) + 20
        code, out = run(host, cart, script(lines + ["%d shot %s" % (shot_at, name)]), "%s/shots/%s" % (tmp, name), seconds)
        if code != 0 or " ok" not in out:
            check("run %s ends without a Lua error" % name, False)
            print(out.strip()[-300:])
        return read_png("%s/shots/%s/%s.png" % (tmp, name, name))

    # thirteen pages, then the other seven engines on the SYNTH ENGINE page (Up changes the engine)
    img, hashes = {}, {}
    for i in range(13):
        img["pg%d" % i] = one("pg%d" % i, GOTO[i], 2)
    for k in range(1, 8):
        img["eng%d" % k] = one("eng%d" % k, GOTO[0] + key(20, UP, k), 2)
    img["eng0"] = img["pg0"]
    names = ["pg%d" % i for i in range(13)] + ["eng%d" % k for k in range(1, 8)]
    for n in names:
        im = img[n]
        _, lit = colours(im, 0, 44, 960, 404)
        check("%s: the picture is not blank" % n, lit > 1500)
        strip, _ = colours(im, 960, 100, 1280, 360)
        for k, c in enumerate(ENC):
            check("%s: encoder colour %d in the value strip" % (n, k + 1), has(strip, c))
        err, _ = colours(im, 974, 376, 1160, 404)
        check("%s: no engine error on the status strip" % n, not has(err, HOT))
        hashes[n] = crop_hash(im, 0, 44, 960, 404)
    check("the 20 pictures differ", len(set(hashes.values())) == 20)
    for i in range(13):
        pv = colours(img["pg%d" % i], 1190, 84, 1280, 106)[1]       # PREVIEW is in the inspector, under the page name
        check("%s: PREVIEW %s on the status strip" % (PAGES[i], "shown" if i in PREVIEW else "not shown"), (pv > 20) == (i in PREVIEW))

    # Tab and the arrows
    base = key(10, MOD[0])
    same = lambda a, b: crop_hash(a, 0, 0, 1280, 404) == crop_hash(b, 0, 0, 1280, 404)
    check("Right goes to the next page (as Tab, inside SYNTH)", same(one("right", base + key(16, RIGHT), 2, 50), one("tab", base + key(16, TAB), 2, 50)))
    check("Left from the first page goes to the last (TEMPO)", same(one("left", base + key(16, LEFT), 2, 50), one("f8", key(10, MOD[7]), 2, 50)))
    check("Tab in a one-page module stays", same(one("tab2", key(10, MOD[1]) + key(16, TAB), 2, 50), one("f2", key(10, MOD[1]), 2, 50)))

    # every encoder moves its part of the scene: the same frames with the encoder held up or down against none
    def scene_crop(im):
        w, h, bpp, rows = im
        rows = [bytearray(r[:960 * bpp]) for r in rows[44:404]]
        return hashlib.md5(b"".join(bytes(r) for r in rows)).hexdigest()
    for pg in range(13):
        steps = [(0, [])] if pg != 0 else [(k, key(20, UP, k)) for k in range(8)]
        for eng, pre in steps:
            ref = scene_crop(one("ref", GOTO[pg] + pre, 3, 120))
            for e in range(4):
                moved = False
                for d in (0, 1):
                    hold = ["60 keys %d" % ENCKEYS[e][d], "90 keys none"]
                    moved = moved or scene_crop(one("enc", GOTO[pg] + pre + hold, 3, 120)) != ref
                check("%s: encoder %d moves the scene" % (PAGES[pg] + (" " + ENGINES[eng] if pg == 0 else ""), e + 1), moved)

    # SEQ GRID with the demo pattern: track 2 is the bass line; on track 1 (key 1) the rim steps 2 6 9 12 15 come from the native euclid
    def cells(im):
        w, h, bpp, rows = im
        x = lambda i: int(3 * (12 + 18.5 * i)) + 6                    # inside the box of step i, near its top left corner
        return [tuple(rows[68][x(i) * bpp:x(i) * bpp + 3]) for i in range(16)]
    def cursor(im):                                                 # the cyan mark in the middle of the box of step 1
        w, h, bpp, rows = im
        return near(tuple(rows[95][60 * bpp:60 * bpp + 3]), ENC[0])
    c2 = cells(img["pg4"])
    pink = [i for i in range(16) if near(c2[i], ENC[1])]
    check("SEQ GRID shows the bass line of track 2, the cursor on step 1 (pink cells %s)" % pink, pink == [3, 6, 8, 11, 14] and cursor(img["pg4"]))
    c1img = one("grid1", GOTO[4] + key(16, 0x1E), 2)
    c1 = cells(c1img)
    pink = [i for i in range(16) if near(c1[i], ENC[1])]
    check("SEQ GRID shows the beat of track 1 (pink cells %s), with the rim steps of the native euclid" % pink,
          pink == [2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 14, 15] and cursor(c1img))

    # the transport, REC, and a recorded hit: select track 2, arm REC, start; U (note 4 of the key block) near step 2
    live = key(5, KEY_2) + key(10, MOD[2])
    seq0 = one("seq0", live, 4, 130)
    seq1 = one("seq1", live + key(40, SPACE), 4, 130)
    check("Space runs the transport (bar.beat.step changes)", crop_hash(seq0, 1090, 0, 1220, 40) != crop_hash(seq1, 1090, 0, 1220, 40))
    check("REC is not on the status strip before Enter", colours(seq0, 1226, 28, 1280, 48)[1] == 0)
    rec = one("rec", live + key(30, ENTER) + key(40, SPACE) + key(71, KEY_U) + key(100, SPACE), 4, 160)
    check("REC (hot colour) is on the status strip after Enter", has(colours(rec, 1226, 28, 1280, 48)[0], HOT))
    ctl = one("ctl", live + key(40, SPACE) + key(71, KEY_U) + key(100, SPACE), 4, 160)

    def lit(im):
        w, h, bpp, rows = im
        return [i for i in range(16) if not near(cells(im)[i], INK)]
    check("a hit is recorded on a step of track 2 only with REC on (lit steps %s, without REC %s)" % (lit(rec), lit(ctl)),
          len(lit(rec)) == len(lit(ctl)) + 1 and set(lit(ctl)) < set(lit(rec)))

    # step edit on SEQ GRID (track 2, brush C2): Q moves the cursor, T toggles a slot, Backspace clears a step, Shift+Backspace the track
    q, bk, tk = 0x26, 0x2A, 0x04
    def dark(im):
        return [i for i in range(16) if near(cells(im)[i], INK)]
    t_on = one("edit_t", GOTO[4] + key(30, q) + key(40, tk) + key(50, q), 2, 90)
    check("T puts the brush note into the step at the cursor (step 2 lit: %s)" % (1 not in dark(t_on)), 1 not in dark(t_on) and 1 in dark(img["pg4"]))
    t_off = one("edit_t2", GOTO[4] + key(30, q) + key(40, tk) + key(50, tk) + key(60, q), 2, 100)
    check("T on the same note takes it out again", 1 in dark(t_off))
    clr = one("edit_bk", GOTO[4] + key(30, q, 3) + key(60, bk) + key(70, q), 2, 100)
    check("Backspace clears the step at the cursor (step 4)", 3 in dark(clr) and 6 not in dark(clr))
    clr_all = one("edit_sbk", GOTO[4] + key(30, q) + ["50 keys 0xE1 %d" % bk, "52 keys none"] + key(60, q), 2, 100)
    check("Shift+Backspace clears the track (lit steps %s)" % [i for i in range(16) if i not in dark(clr_all)], [i for i in range(16) if i not in dark(clr_all)] == [])   # the cursor box is empty now: only its middle mark is lit
    mute = one("mute", GOTO[4] + ["30 keys 0xE1 %d" % KEY_2, "32 keys none"], 2, 60)
    check("Shift+2 mutes track 2 (M2 on the status strip)", colours(mute, 974, 376, 1100, 404)[1] > 10 and colours(img["pg4"], 974, 376, 1100, 404)[1] == 0)

    # phase 3: the pattern letter and PAT / SONG on the status strip
    def strip(im, x0, x1): return crop_hash(im, x0, 358, x1, 378)
    p0 = img["pg4"]
    check("the status strip shows PAT (grey) and no SONG", has(colours(p0, 1086, 358, 1130, 378)[0], (0xa6, 0x9f, 0xce)) and not has(colours(p0, 1086, 358, 1130, 378)[0], ENC[3]))
    song = one("song", GOTO[4] + key(20, KEY_P), 2, 60)
    check("P switches to SONG: the mode text changes and turns white", strip(song, 1086, 1130) != strip(p0, 1086, 1130) and has(colours(song, 1086, 358, 1130, 378)[0], ENC[3]))
    check("P again goes back to PAT", strip(one("pat2", GOTO[4] + key(20, KEY_P) + key(30, KEY_P), 2, 70), 1086, 1130) == strip(p0, 1086, 1130))
    patb = one("patb", GOTO[4] + key(20, KEY_QUOTE), 2, 60)
    check("' selects the next pattern: the letter after the track changes (A to B)", strip(patb, 1060, 1072) != strip(p0, 1060, 1072))
    check("; goes back to the first pattern (A)", strip(one("pata", GOTO[4] + key(20, KEY_QUOTE) + key(30, KEY_SEMI), 2, 70), 1060, 1072) == strip(p0, 1060, 1072))
    check("pattern B of track 2 holds its own bass line (the lift of the demo): same steps lit, other notes",
          [i for i in range(16) if near(cells(patb)[i], ENC[1])] == [3, 6, 8, 11, 14] and crop_hash(patb, 0, 60, 960, 110) != crop_hash(p0, 0, 60, 960, 110))

    # the TAPE page keys: track 2 (selected) has a clip in the slots 2 to 4 of the demo; the cursor is on slot 1
    def lane(im, slot, row=2):
        w, h, bpp, rows = im
        y, x = 3 * (78 + (row - 1) * 13) + 3, 3 * (26 + slot * 18 + 9)      # inside the top edge of the clip, middle of the slot
        return tuple(rows[y][x * bpp:x * bpp + 3])
    tape0 = img["pg5"]
    check("TAPE shows the demo clips: track 2 has a white clip in slot 2 and none in slot 1", near(lane(tape0, 1), ENC[3]) and near(lane(tape0, 0), INK))
    check("TAPE shows the clip of another track in the grid colour (track 1, slot 1)", near(lane(tape0, 0, 1), (0x4b, 0x3d, 0x96)))
    tw = one("tape_t", GOTO[5] + key(30, KEY_T), 2, 60)
    check("T writes the selected pattern of the current track into the slot at the cursor (a clip in slot 1)", near(lane(tw, 0), ENC[3]))
    tc = one("tape_bk", GOTO[5] + key(30, KEY_T) + key(40, KEY_BK), 2, 70)
    check("Backspace clears the slot at the cursor", near(lane(tc, 0), INK) and near(lane(tc, 1), ENC[3]))
    tr = one("tape_sbk", GOTO[5] + ["30 keys 0xE1 %d" % KEY_BK, "32 keys none"], 2, 60)
    check("Shift+Backspace clears the song row of the track", near(lane(tr, 1), INK) and near(lane(tr, 2), INK) and near(lane(tape0, 2), ENC[3]))

    # the colour fix: in the PCM scene the sun is the cutoff, so it has the colour of encoder 2
    pcm_sun = tuple(img["eng7"][3][152][480 * img["eng7"][2]:480 * img["eng7"][2] + 3])
    check("the sun of the PCM scene has the colour of encoder 2 (CUTOFF), not encoder 3", near(pcm_sun, ENC[1]))

    # the live keys play every sound: ] loads the next sound into the track, a key is held for a moment
    play = key(5, KEY_2) + key(10, MOD[0])
    for n in range(45):
        play += key(20 + 20 * n, RB) + ["%d keys %d" % (23 + 20 * n, KEY_U), "%d keys none" % (33 + 20 * n)]
    play += key(940, MOD[1]) + key(950, KEY_O)                    # and a pad
    code, out = run(host, cart, script(play), tmp + "/shots/play", 17)
    check("the live keys play 44 sounds (the list wraps once) and a pad without a Lua error", code == 0 and " ok" in out)

    # the PCM scene (engine 8): the sample of the sound is named on the scene; audio_samples() of the cartridge lists the 24 samples
    names = [l.split()[0] for l in open(ROOT + "/tests/tape/felucca_golden.txt") if l.strip() and not l.startswith("#")]
    lua = open(ROOT + "/build/tape/main.lua").read() + (
        '\nlocal smp = audio_samples()\nlog("TAPE_SAMPLES " .. #smp .. " " .. (smp[1] and smp[1].name or "") .. " "'
        ' .. (smp[24] and smp[24].name or ""))\n')
    open(tmp + "/samples.lua", "w").write(lua)
    subprocess.check_call([sys.executable, ROOT + "/scripts/mkbm.py", "-o", tmp + "/samples.bm", "--lua", tmp + "/samples.lua",
                           "--sheet", ROOT + "/build/tape/sheet.png", "--sheet8", "--audio", ROOT + "/build/tape/bank.json",
                           "--info", ROOT + "/carts/tape/bm.info"], stdout=subprocess.DEVNULL)
    open(tmp + "/samples.txt", "w").write(script(key(10, MOD[0])))
    p = subprocess.run([host, tmp + "/samples.bm", "--input", tmp + "/samples.txt", "--seconds", "1"], capture_output=True, text=True)
    code, out = p.returncode, p.stdout + p.stderr
    check("audio_samples() in the cartridge lists %d samples (%s)" % (len(names), out.split("TAPE_SAMPLES")[-1].strip()[:40]),
          code == 0 and "TAPE_SAMPLES %d %s %s" % (len(names), names[0], names[-1]) in out)

    # the error path: a broken engine.b must leave the cartridge drawing, with the error text on the status strip
    bad = open(ROOT + "/build/tape/main.lua").read()
    check("error path: the test finds engine.b in main.lua", "function euclid(k: i32" in bad)
    bad = bad.replace("function euclid(k: i32", "function euclid(k: str")
    open(tmp + "/main.lua", "w").write(bad)
    subprocess.check_call([sys.executable, ROOT + "/scripts/mkbm.py", "-o", tmp + "/bad.bm", "--lua", tmp + "/main.lua",
                           "--sheet", ROOT + "/build/tape/sheet.png", "--sheet8", "--info", ROOT + "/carts/tape/bm.info"],
                          stdout=subprocess.DEVNULL)
    badscript = script(key(30, MOD[2]) + key(40, SPACE) + key(50, KEY_U) + ["80 shot bad"])
    code, out = run(host, tmp + "/bad.bm", badscript, tmp + "/badshots", 2)
    check("error path: the run ends without a Lua error (Space and a live key included)", code == 0 and " ok" in out)
    im = read_png(tmp + "/badshots/bad.png")
    check("error path: the error text is on the status strip", has(colours(im, 974, 376, 1160, 404)[0], HOT))
    check("error path: the scene still draws", colours(im, 0, 44, 960, 404)[1] > 1500)

    print("tape render: %s (pictures in %s/shots)" % ("FAIL" if fails else "ok", tmp))
    sys.exit(1 if fails else 0)


main()
