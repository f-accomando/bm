#!/usr/bin/env python3
"""bmvideo: a tutorial video of a bm tool, made on the PC with bmhost.

What it does (see claude/skills/bm-video-tutorial/SKILL.md):

  * Script: builds the input file of bmhost (tests/host/bmhost.c) from calls
    like s.key("b"), s.text("hello"), s.wait(30), so the run is the same
    every time (the clock of bmhost is virtual: frame n is at n/60 s);
    for every key it also keeps a "keycap" for the overlay.
  * run(): runs bmhost with the raw video through a pipe.
  * assemble(): ffmpeg puts the 640x360 frames in a 1920x1080 page (the
    console 2x, pixel for pixel; a side panel with the place, the key just
    pressed and the keys of the scene; a title bar; the narrator's line at
    the bottom), as an .ass file drawn by libass, and encodes the mp4.

Only the standard library and ffmpeg (libass) are needed.
"""
import os
import subprocess
import sys

FPS = 60
GAME_W, GAME_H = 640, 360

# keyp() codes the serial/typed queue understands (src/usb/hid.h, runtime.c)
KEYS = {
    "up": "\\xf0", "down": "\\xf1", "left": "\\xf2", "right": "\\xf3",
    "home": "\\xf4", "end": "\\xf5", "pgup": "\\xf6", "pgdn": "\\xf7", "del": "\\xf8",
    "f1": "\\xf9", "f2": "\\xfa", "f3": "\\xfb", "f4": "\\xfc", "f5": "\\xfd",
    # F6-F10 and Ctrl+Enter only as the serial terminal's escape sequences: the raw bytes 0xE5-0xEF are dropped
    "f6": "\\x1b[17~", "f7": "\\x1b[18~", "f8": "\\x1b[19~", "f9": "\\x1b[20~", "f10": "\\x1b[21~",
    "ctrl+enter": "\\x1b[28~", "ctrl+.": "\\x1b[29~",
    "esc": "\\x1b", "enter": "\\n", "tab": "\\x09", "space": "\\x20", "bksp": "\\x7f",
}
# the name on the keycap
CAPS = {
    "up": "↑", "down": "↓", "left": "←", "right": "→", "pgup": "PgUp", "pgdn": "PgDn",
    "del": "Del", "esc": "Esc", "enter": "Enter", "tab": "Tab", "space": "Space", "bksp": "⌫",
    "f1": "F1", "f2": "F2", "f3": "F3", "f4": "F4", "f5": "F5", "f6": "F6", "f7": "F7",
    "f8": "F8", "f9": "F9", "f10": "F10", "home": "Home", "end": "End",
    "ctrl+enter": "Ctrl+Enter", "ctrl+.": "Ctrl+.",
}


def ctrl(letter, shift=False):
    """Ctrl+letter as keyp() gets it: the code 1..26 (Ctrl+Shift: 0xEE first)."""
    code = ord(letter.lower()) - 96
    return ("\\xee" if shift else "") + "\\x%02x" % code


class Script:
    def __init__(self):
        self.t = 0                  # the frame now
        self.lines = []             # the bmhost input script
        self.caps = []              # (frame, keycap, what)
        self.scenes = []            # (frame, place, [(keycap, what)])
        self.lines_say = []         # (frame_from, frame_to, text): the narrator
        self.cards = []             # (frame, text) markers for the chapters
        self.pointer = (0, 0)
        self.fast = 3               # frames between two keys of a series

    # ---------------------------------------------------------------- time
    def wait(self, n):
        self.t += int(n)
        return self

    def sec(self, s):
        return self.wait(round(s * FPS))

    # --------------------------------------------------------------- input
    def raw(self, code, cap=None, what="", hold=2):
        """Type the codes of keyp() (a string as bmhost reads it) at frame t."""
        self.lines.append("%d type %s" % (self.t, code))
        if cap:
            self.caps.append((self.t, cap, what))
        self.t += hold
        return self

    def key(self, name, what="", cap=None, hold=None):
        """One key by name: 'b', 'space', 'f2', 'esc', 'pgdn' ..."""
        code = KEYS.get(name, name if len(name) == 1 else None)
        if code is None:
            raise ValueError("key? " + name)
        if code == "\\":
            code = "\\x5c"
        label = cap or CAPS.get(name, name.upper() if len(name) == 1 and name.isalpha() else name)
        return self.raw(code, label, what, hold if hold is not None else self.fast)

    def chord(self, letter, what="", shift=False):
        """Ctrl+letter (Ctrl+Shift+letter)."""
        lab = "Ctrl+" + ("Shift+" if shift else "") + letter.upper()
        return self.raw(ctrl(letter, shift), lab, what, 8)

    def type_text(self, text, what="", per_key=4):
        """A text typed into a prompt, one character at a time."""
        first = True
        for ch in text:
            code = "\\x20" if ch == " " else ("\\x5c" if ch == "\\" else ch)
            self.lines.append("%d type %s" % (self.t, code))
            if first and what:
                self.caps.append((self.t, "“" + text + "”", what))
                first = False
            self.t += per_key
        return self

    def pad(self, buttons, what="", cap=None, hold=3):
        """Gamepad buttons held for `hold` frames (names of bmhost: a, b, x, y,
        up, down, left, right, start, select, l1, r1, l2, r2)."""
        self.lines.append("%d pad 1 %s" % (self.t, buttons))
        if cap:
            self.caps.append((self.t, cap, what))
        self.t += hold
        self.lines.append("%d pad 1 none" % self.t)
        self.t += 2
        return self

    def mark(self, cap, what=""):
        """A keycap on the overlay with no input (the key that ends the run:
        F5 leaves the tool, the next part is another recording)."""
        self.caps.append((self.t, cap, what))
        return self

    def shot(self, name):
        self.lines.append("%d shot %s" % (self.t, name))
        self.t += 1                 # the picture is the frame after: keys at the same frame would be in it
        return self

    # ------------------------------------------------------------- overlay
    def scene(self, place, keys=()):
        """From now on: where we are, and the keys of the scene (side panel)."""
        self.scenes.append((self.t, place, list(keys)))
        return self

    def say(self, text, frames=None):
        """The narrator's line from now for `frames` (default: until the next)."""
        if self.lines_say and self.lines_say[-1][1] is None:
            self.lines_say[-1] = (self.lines_say[-1][0], self.t, self.lines_say[-1][2])
        self.lines_say.append((self.t, None if frames is None else self.t + frames, text))
        return self

    def chapter(self, title):
        self.cards.append((self.t, title))
        return self

    def end(self):
        if self.lines_say and self.lines_say[-1][1] is None:
            self.lines_say[-1] = (self.lines_say[-1][0], self.t, self.lines_say[-1][2])
        return self

    def write_input(self, path):
        with open(path, "w") as f:
            f.write("\n".join(self.lines) + "\n")


# ------------------------------------------------------------------- run
def run_bmhost(bmhost, cart, input_path, seconds, sd, raw_out, shots=None, wav=None):
    """bmhost with the frames written to raw_out (a file or a fifo)."""
    cmd = [bmhost, cart, "--sd", sd, "--seconds", str(seconds), "--input", input_path,
           "--video", raw_out, "--quiet"]
    if shots:
        cmd += ["--shots", shots]
    if wav:
        cmd += ["--wav", wav]
    return cmd


# -------------------------------------------------------------- the .ass
def ass_time(frame):
    cs = round(frame * 100 / FPS)
    return "%d:%02d:%02d.%02d" % (cs // 360000, cs // 6000 % 60, cs // 100 % 60, cs % 100)


def esc(text):
    return text.replace("\\", "\\\\").replace("{", "(").replace("}", ")").replace("\n", "\\N")


def make_ass(s, path, title, subtitle, total_frames, offset=0):
    """The overlay: title bar, side panel (place, key, keys of the scene),
    the narrator's line. `offset` frames shift everything (intro clips)."""
    W, H = 1920, 1080
    hdr = f"""[Script Info]
ScriptType: v4.00+
PlayResX: {W}
PlayResY: {H}
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding
Style: Title,DejaVu Sans,46,&H00F0E4E0,&H00F0E4E0,&H00000000,&H00000000,1,0,0,0,100,100,0,0,1,0,0,7,40,40,0,1
Style: Sub,DejaVu Sans,30,&H00907870,&H00907870,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,9,40,40,0,1
Style: Label,DejaVu Sans,26,&H00907870,&H00907870,&H00000000,&H00000000,1,0,0,0,100,100,3,0,1,0,0,7,0,0,0,1
Style: Place,DejaVu Sans,38,&H0050C0FF,&H0050C0FF,&H00000000,&H00000000,1,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Keycap,DejaVu Sans Mono,72,&H00201810,&H00201810,&H0050C0FF,&H00000000,1,0,0,0,100,100,0,0,3,22,0,5,0,0,0,1
Style: What,DejaVu Sans,36,&H00F0E4E0,&H00F0E4E0,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,5,0,0,0,1
Style: Cheat,DejaVu Sans Mono,30,&H00D8D0C8,&H00D8D0C8,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Say,DejaVu Sans,40,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,3,0,2,60,60,0,1
Style: Panel,DejaVu Sans,20,&H00302418,&H00302418,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1

[Events]
Format: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
"""
    ev = []

    def add(layer, a, b, style, text, ml=0, mr=0, mv=0):
        if b <= a:
            return
        ev.append(f"Dialogue: {layer},{ass_time(a + offset)},{ass_time(b + offset)},{style},,{ml},{mr},{mv},,{text}")

    end = total_frames
    add(0, 0, end, "Title", r"{\pos(40,26)}" + esc(title))
    add(0, 0, end, "Sub", r"{\an9\pos(1880,34)}" + esc(subtitle))

    # side panel: x 1280..1920
    PX = 1320
    add(0, 0, end, "Label", r"{\pos(%d,150)}WHERE" % PX)
    add(0, 0, end, "Label", r"{\pos(%d,330)}KEY" % PX)
    add(0, 0, end, "Label", r"{\pos(%d,520)}KEYS OF THIS PART" % PX)

    sc = s.scenes + [(end, "", [])]
    for i in range(len(sc) - 1):
        a, place, keys = sc[i]
        b = sc[i + 1][0]
        add(1, a, b, "Place", r"{\pos(%d,190)}" % PX + esc(place))
        for j, (kc, what) in enumerate(keys[:8]):
            add(1, a, b, "Cheat", r"{\pos(%d,%d)}" % (PX, 566 + j * 34) + esc("%-9s %s" % (kc, what)))

    # the key just pressed: lights for 0.9 s, or until the next key. A run of
    # the same key (colour "." x7) or of pointer keys (arrows + Space) is one card.
    groups = []                                  # [first frame, last frame, label, what, count]
    for f, cap, what in s.caps:
        pointer = cap in MOVES
        if groups and f - groups[-1][1] <= 15 and (
                (pointer and groups[-1][5]) or (not pointer and cap == groups[-1][2] and what == groups[-1][3])):
            g = groups[-1]
            g[1] = f
            g[4] += 1
            if pointer and cap == "Space" and what:
                g[3] = what
            continue
        groups.append([f, f, "Arrows + Space" if pointer else cap, what if not pointer else "", 1, pointer])
        if pointer and cap == "Space" and what:
            groups[-1][3] = what
    for i, (f, last, cap, what, n, pointer) in enumerate(groups):
        nxt = groups[i + 1][0] if i + 1 < len(groups) else end
        stop = min(last + round(0.9 * FPS), nxt, end)
        label = cap + (" ×%d" % n if n > 1 and not pointer else "")
        if pointer and not what:
            what = "move the pointer, paint"
        add(2, f, stop, "Keycap", r"{\pos(1600,420)}" + esc(" %s " % label))
        if what:
            add(2, f, stop, "What", r"{\pos(1600,490)}" + esc(what))

    for a, b, text in s.lines_say:
        add(3, a, b, "Say", r"{\q0\an2\pos(960,1040)}" + esc(text))
    return hdr + "\n".join(ev) + "\n"


def write_ass(s, path, title, subtitle, total_frames, offset=0):
    with open(path, "w") as f:
        f.write(make_ass(s, path, title, subtitle, total_frames, offset))


# ----------------------------------------------------------- the encoding
def filter_chain(ass_path):
    """640x360 -> 1280x720 (nearest) in a 1920x1080 page, side panel, overlay."""
    ap = ass_path.replace("\\", "/").replace(":", "\\:")
    return (
        "scale=1280:720:flags=neighbor,"
        "pad=1920:1080:0:120:color=0x0b0d13,"
        "drawbox=x=1280:y=120:w=640:h=720:color=0x151926@1:t=fill,"
        "drawbox=x=1280:y=120:w=2:h=720:color=0x2a3048@1:t=fill,"
        
        "drawbox=x=0:y=840:w=1920:h=240:color=0x0b0d13@1:t=fill,"
        f"ass='{ap}'"
    )


def encode(raw_path, ass_path, out_path, frames_every=1, fps=FPS, audio=None):
    vf = filter_chain(ass_path)
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
           "-s", "%dx%d" % (GAME_W, GAME_H), "-r", str(fps), "-i", raw_path]
    if audio:                                    # bmhost --wav gives a .wav; a raw file is 48 kHz mono s16le
        cmd += ["-i", audio] if audio.endswith(".wav") else ["-f", "s16le", "-ar", "48000", "-ac", "1", "-i", audio]
    cmd += ["-vf", vf, "-c:v", "libx264", "-preset", "medium", "-crf", "19", "-pix_fmt", "yuv420p"]
    if audio:
        cmd += ["-c:a", "aac", "-b:a", "128k", "-shortest"]
    cmd += ["-movflags", "+faststart", out_path]
    return cmd


def call(cmd, **kw):
    print("+", " ".join(cmd), file=sys.stderr)
    return subprocess.run(cmd, check=True, **kw)


# ------------------------------------------------------- storyboard, script
def mmss(frame, offset=0):
    t = (frame + offset) / FPS
    return "%d:%02d" % (int(t) // 60, int(t) % 60)


def _where(s, frame):
    place = ""
    for f, pl, _ in s.scenes:
        if f <= frame:
            place = pl
    return place


def _say_at(s, frame):
    for a, b, text in s.lines_say:
        if a <= frame < (b if b is not None else 10 ** 9):
            return text
    return ""


MOVES = {"↑", "↓", "←", "→", "Space"}


def write_storyboard(s, path, title, offset=0):
    """storyboard.md: one row for a key or a run of pointer keys (arrows, Space)."""
    rows, i = [], 0
    caps = s.caps
    while i < len(caps):
        f, cap, what = caps[i]
        j = i
        if cap in MOVES:
            while j + 1 < len(caps) and caps[j + 1][1] in MOVES and _where(s, caps[j + 1][0]) == _where(s, f):
                j += 1
            n = j - i + 1
            label = "arrows + Space (%d presses)" % n if n > 1 else cap
            what_txt = next((w for _, c, w in caps[i:j + 1] if w and c == "Space"), "move the pointer / paint")
        else:
            label, what_txt = cap, what
        rows.append((f, _where(s, f), label, what_txt))
        i = j + 1
    merged = []                     # the same key again and again: one row, "×N"
    for r in rows:
        if merged and tuple(merged[-1][1:4]) == tuple(r[1:4]) and "(" not in r[2]:
            merged[-1][4] += 1
        else:
            merged.append(list(r) + [1])
    rows = [(f, pl, lab + (" ×%d" % n if n > 1 else ""), what) for f, pl, lab, what, n in merged]
    out = ["# %s — storyboard" % title, "",
           "Time is in the final video (hook and title card included: +%d s)." % (offset // FPS), "",
           "| # | Time | Where | Key / button | What you see | Narrator |",
           "|---|------|-------|--------------|--------------|----------|"]
    for n, (f, place, label, what) in enumerate(rows, 1):
        out.append("| %d | %s | %s | `%s` | %s | %s |" % (n, mmss(f, offset), place, label, what, _say_at(s, f)))
    open(path, "w").write("\n".join(out) + "\n")


def write_script(s, path, title, subtitle, offset=0, description=""):
    out = ["# %s — narrator's script" % title, "", "Each line starts at the time shown (final video).", ""]
    for a, b, text in s.lines_say:
        out.append("- **%s** %s" % (mmss(a, offset), text))
    out += ["", "## Chapters (YouTube)", ""]
    for f, t in s.cards:
        out.append("%s %s" % (mmss(f, offset), t))
    out += ["", description]
    open(path, "w").write("\n".join(out) + "\n")


# ---------------------------------------------------------------- the cards
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_R = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


def card_cmd(out, seconds, lines, fps=FPS, silent=False):
    """A title card: lines = [(text, size, colour, y)], centred, with a fade."""
    vf = []
    for text, size, colour, y in lines:
        t = text.replace(":", "\\:").replace("'", "’")
        vf.append("drawtext=fontfile=%s:text='%s':fontsize=%d:fontcolor=%s:x=(w-text_w)/2:y=%d" %
                  (FONT if size > 40 else FONT_R, t, size, colour, y))
    vf.append("fade=t=in:st=0:d=0.5,fade=t=out:st=%.2f:d=0.5" % (seconds - 0.5))
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
           "color=c=0x0b0d13:s=1920x1080:r=%d:d=%.2f" % (fps, seconds)]
    if silent:
        cmd += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono", "-shortest", "-c:a", "aac"]
    return cmd + ["-vf", ",".join(vf), "-c:v", "libx264", "-crf", "19", "-pix_fmt", "yuv420p", out]


def hook_cmd(main, out, start, seconds, text):
    """A cut of the finished video with a banner: the result, up front."""
    t = text.replace(":", "\\:").replace("'", "’")
    vf = ("drawbox=x=0:y=0:w=1920:h=120:color=0x0b0d13@1:t=fill,"
          "drawtext=fontfile=%s:text='%s':fontsize=52:fontcolor=0xffc050:x=(w-text_w)/2:y=34,"
          "fade=t=in:st=0:d=0.4,fade=t=out:st=%.2f:d=0.4" % (FONT, t, seconds - 0.4))
    return ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-ss", "%.2f" % start, "-t", "%.2f" % seconds,
            "-i", main, "-vf", vf, "-c:v", "libx264", "-crf", "19", "-pix_fmt", "yuv420p", "-r", str(FPS), out]


def concat_cmd(parts, out, audio=False):
    """The parts one after the other. With audio=True the first, second and last
    parts are silent cards/cuts and the main one has sound: all get an audio track."""
    n = len(parts)
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
    for p in parts:
        cmd += ["-i", p]
    if not audio:
        fc = "".join("[%d:v]" % i for i in range(n)) + "concat=n=%d:v=1:a=0[v]" % n
        cmd += ["-filter_complex", fc, "-map", "[v]"]
    else:
        fc = "".join("[%d:v][%d:a]" % (i, i) for i in range(n)) + "concat=n=%d:v=1:a=1[v][a]" % n
        cmd += ["-filter_complex", fc, "-map", "[v]", "-map", "[a]", "-c:a", "aac", "-b:a", "160k"]
    cmd += ["-c:v", "libx264", "-crf", "19", "-preset", "medium", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out]
    return cmd
