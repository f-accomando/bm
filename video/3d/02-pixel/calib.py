"""Calibration of the face pointer of bm Studio (tool 3/5): which arrows reach which face.

bm Studio moves its face pointer to "the nearest face in that direction on the screen" (T.nav of src/script/bm3d.lua),
so the keys that reach a given face depend on the model. record.py does not guess them: it runs a copy of bm Studio
with a few lines of logging (built here from carts/studio/main.lua, nothing else changed), asks it for the screen
position, centre, normal and colour of every face of every model (key `~`), and plans the arrows with the same
algorithm (nav) in Python. The log copy also traces every key (`KEY ...`), which is how the plan is checked (trace()).

  calib.build_dbg(out_dir) -> path of studio_dbg.bm
  calib.dump(sd, out_dir)  -> {"CRATE": {"hot": 6, "faces": [...]}, ...}   (Studio opens /carts/TROPIC.BME)
  calib.nav(pos, cur, d)   -> the face the pointer goes to
  calib.route(pos, a, b)   -> list of arrow names from face a to face b (None: unreachable)
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "video", "3d"))
import lib3d  # noqa: E402

BS, LF = chr(92), chr(10)          # a backslash and a newline (bmhost reads \xNN and \n in the input text)
MARK ="\n\nend\n\n----------------------------------------------------------------- the program"
INJECT = r'''
do
  local ok = build.key
  function build.key(k)
    ok(k)
    if k == "~" then
      local m = M()
      hot_face()
      log(string.format("DUMP %s %d %s %.4f", m.name, #faces(), tostring(bd.hot), bd.yaw_to))
      for i, f in ipairs(faces()) do
        local sx, sy, sz = face_pos(i)
        local c, n = T.face_center(f), T.face_normal(f)
        log(string.format("FACE %s %d %s %s %s %.4f %.4f %.4f %.3f %.3f %.3f %s", m.name, i, tostring(sx), tostring(sy),
          tostring(sz), c[1], c[2], c[3], n[1], n[2], n[3], f.c and string.format("%06X", f.c) or "-"))
      end
      return
    end
    local nsel = 0
    for _ in pairs(bd.sel) do nsel = nsel + 1 end
    local hf = bd.hot and faces()[bd.hot]
    local pf = nil
    if bd.paint then pf = string.format("%d,%d cx=%d,%d", bd.paint.x, bd.paint.y, bd.paint.cx, bd.paint.cy) end
    local flat = hf and (hf.c and string.format("flat%06X", hf.c) or "tile")
    log(string.format("KEY %s model=%s tool=%d sel=%d hot=%s yaw=%.4f %s pick=%s,%d,%d brush=%s paint=%s", string.format("%q", k):gsub("\n", "n"),
      M() and M().name or "?", bd.tool, nsel, tostring(bd.hot), bd.yaw_to, flat or "-", tostring(picker.open), picker.x, picker.y,
      brush.c and string.format("c%06X", brush.c) or table.concat(brush.rect, ","), pf or "-"))
  end
end
'''


def build_dbg(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    src = open(os.path.join(ROOT, "carts", "studio", "main.lua"), encoding="utf-8").read()
    if MARK not in src:
        raise SystemExit("calib: carts/studio/main.lua changed shape (end of the build block not found)")
    lua = os.path.join(out_dir, "main_dbg.lua")
    open(lua, "w", encoding="utf-8", newline="\n").write(src.replace(MARK, "\n" + INJECT + MARK, 1))
    out = os.path.join(out_dir, "studio_dbg.bm")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", out, "--lua", lua,
                           "--cover", os.path.join(ROOT, "carts", "studio", "icon.png"), "--sheet",
                           os.path.join(ROOT, "carts", "studio", "assets", "sheet.png"), "--sheet8", "--info",
                           os.path.join(ROOT, "carts", "studio", "bm.info")], cwd=ROOT, stdout=subprocess.DEVNULL)
    return out


def run_log(dbg, input_path, seconds, sd):
    """bmhost with the log (no --quiet): the lines of log()."""
    cmd = [lib3d.BMHOST, dbg, "--tool", "--sd", sd, "--input", input_path, "--seconds", str(seconds)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600)
    return r.stdout + r.stderr


def dump(sd, out_dir, model="PICO", n_yaws=8):
    """The faces of `model` as the pointer sees them (tool 3), from each of the 8 camera positions (q / e turn the camera
    by 45 degrees): {"hot": face under the pointer when the model opens, "yaws": [faces at yaw 0, 1, ... 7]}.
    A face has: i, s (screen x, y, z as face_pos), c (centre), n (normal), flat (colour or None)."""
    dbg = build_dbg(out_dir)
    names = ["CRATE", "APPLE", "PICO"]
    lines, t = [], 30
    lines.append("%d type %sx0f" % (t, BS)); t += 20
    lines.append("%d type %sn" % (t, BS)); t += 150
    lines.append("%d type 3" % t); t += 60                # the select tool is on before the model changes (as in the video):
    for _ in range(names.index(model)):                   # the pointer's first face depends on that history
        lines.append("%d type ]" % t); t += 150           # the camera of build.reset settles
    t += 30
    for k in range(n_yaws):
        lines.append("%d type ~" % t); t += 20
        lines.append("%d type e" % t); t += 90            # the camera eases to the next yaw (45 degrees)
    inp = os.path.join(out_dir, "calib_input.txt")
    open(inp, "w", newline=LF).write(LF.join(lines) + LF)
    text = run_log(dbg, inp, t / 60 + 2, sd)
    yaws, hot = [], None
    for ln in text.splitlines():
        p = ln.split()
        if not p:
            continue
        if p[0] == "DUMP" and p[1] == model:
            if hot is None:
                hot = int(p[3])
            yaws.append([])
        elif p[0] == "FACE" and p[1] == model:
            nz = lambda v: None if v == "nil" else float(v)
            yaws[-1].append({"i": int(p[2]), "s": [nz(p[3]), nz(p[4]), nz(p[5])],
                             "c": [float(v) for v in p[6:9]], "n": [float(v) for v in p[9:12]],
                             "flat": None if p[12] == "-" else int(p[12], 16)})
    if len(yaws) != n_yaws or hot is None:
        raise SystemExit("calib: %d dumps of %s%s%s" % (len(yaws), model, LF, text[-1500:]))
    return {"hot": hot, "yaws": yaws}


def trace(sd, out_dir, input_path, seconds):
    """The KEY lines of a run of the log copy (to check a plan)."""
    dbg = build_dbg(out_dir)
    text = run_log(dbg, input_path, seconds, sd)
    return [ln for ln in text.splitlines() if ln.startswith(("KEY", "MKEY", "bm"))]


# ------------------------------------------------------------------------------------- the pointer
def nav(pos, cur, d):
    """T.nav of src/script/bm3d.lua. pos: {i: (x, y, z)} of the faces seen on the screen; cur: face; d: direction."""
    cx, cy, _ = pos[cur]
    dx, dy = {"left": (-1, 0), "right": (1, 0), "up": (0, -1), "down": (0, 1)}[d]
    best, bs = cur, 1e18
    for i, (x, y, z) in pos.items():
        if i == cur:
            continue
        ax, ay = x - cx, y - cy
        along = ax * dx + ay * dy
        across = abs(ax * dy - ay * dx)
        if along > 1 and across <= along * 2:
            s = along + across * 2.5 + (z or 0) * 2
            if s < bs:
                best, bs = i, s
    return best


def positions(faces):
    return {f["i"]: tuple(f["s"]) for f in faces if f["s"][0] is not None}


def route(cal, start, goal):
    """Shortest keys from state start = (yaw, face) to a state whose face is `goal` (any yaw): arrows (nav at that yaw's
    positions) and e / q (the camera 45 degrees). cal: the result of dump(). Returns (keys, (yaw, face)) or None."""
    pos = [positions(f) for f in cal["yaws"]]
    n = len(pos)
    if start[1] == goal:
        return [], start
    seen, todo = {start: []}, [start]
    while todo:
        nxt = []
        for st in todo:
            y, f = st
            moves = [(d, (y, nav(pos[y], f, d))) for d in ("up", "down", "left", "right")]
            moves += [("e", ((y + 1) % n, f)), ("q", ((y - 1) % n, f))]
            for k, g in moves:
                if g not in seen:
                    seen[g] = seen[st] + [k]
                    if g[1] == goal:
                        return seen[g], g
                    nxt.append(g)
        todo = nxt
    return None


# --------------------------------------------------------------------------------- the closed loop
STATE_RE = re.compile(r"sel=(\d+) hot=(\w+) yaw=(-?[0-9.]+)")


class Session:
    """The real pointer, asked key by key. The camera of bm Studio follows the face under the pointer (eased, 20% a frame),
    so which face an arrow reaches depends on the keys before it and on their timing: the static dump is only a guide.
    state(lines) replays the keys of the Studio recording so far (input lines, frames from 0) in the log copy and returns
    what the last key left: (face under the pointer, yaw 0-7, faces chosen). Answers are cached in a json (a rerun of
    record.py needs no bmhost) by the hash of the lines and of what the editor is made of (key)."""

    def __init__(self, sd, out_dir, cache_path, key):
        self.sd, self.out_dir, self.cache_path, self.key = sd, out_dir, cache_path, key
        self.dbg = None
        self.cache = {}
        if os.path.exists(cache_path):
            d = json.load(open(cache_path))
            if d.get("key") == key:
                self.cache = d["states"]
        self.runs = 0

    def state(self, lines):
        import hashlib
        h = hashlib.sha1(LF.join(lines).encode()).hexdigest()
        if h in self.cache:
            return tuple(self.cache[h])
        if self.dbg is None:
            if not os.path.exists(lib3d.BMHOST):
                raise SystemExit("no build/host/bmhost-bin and the plan of the face pointer is not in " + self.cache_path)
            self.dbg = build_dbg(self.out_dir)
        inp = os.path.join(self.out_dir, "session_input.txt")
        open(inp, "w", newline=LF).write(LF.join(lines) + LF)
        last = int(lines[-1].split()[0])
        text = run_log(self.dbg, inp, last / 60 + 1.5, self.sd)
        self.runs += 1
        keys = [ln for ln in text.splitlines() if ln.startswith("KEY ")]
        m = STATE_RE.search(keys[-1]) if keys else None
        if not m or m.group(2) == "nil":
            raise SystemExit("calib: no pointer state in the log of the last key:" + LF + text[-800:])
        yaw = round(float(m.group(3)) / (3.141592653589793 / 4)) % 8
        st = (int(m.group(2)), yaw, int(m.group(1)))
        self.cache[h] = list(st)
        if self.runs % 10 == 0:
            self.save()
        return st

    def save(self):
        json.dump({"key": self.key, "states": self.cache}, open(self.cache_path, "w"))
