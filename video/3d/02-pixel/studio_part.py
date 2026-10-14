"""The bm Studio half of episode 2 (shot list rows 39-66): the keys that paint the three models.

build_studio(s, cal, t0, t_end) adds them to the Script `s`; t0 = 2:24 (seconds of the edited video, the start of the
Studio recording), t_end = 3:30. `cal` is the calibration of the face pointer (calib.dump): which arrows reach which face.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import art     # noqa: E402
import calib   # noqa: E402

WARM = lambda c: (c >> 16 & 255) > (c & 255) + 40       # orange legs, cream belly, yellow tail tip: they stay flat colours


class Pointer:
    """The face pointer of bm Studio (tools 3 and 5) and its camera, driven in a closed loop: the real editor (a log copy,
    calib.Session) says where the pointer is after each key, the static dump of calib.py (cal) only suggests the next key
    (arrows, q / e to turn the camera 45 degrees when a face is hidden). Space toggles the face."""

    def __init__(self, s, cal, session, base):
        self.s, self.cal, self.session, self.base = s, cal, session, base
        self.skipped = []
        self.obs = {}
        self.refresh()

    def refresh(self):
        lines = []
        for ln in self.s.lines:
            f, rest = ln.split(" ", 1)
            if int(f) >= self.base:
                lines.append("%d %s" % (int(f) - self.base, rest))
        self.hot, self.yaw, self.sel = self.session.state(lines)

    def key(self, k, what="", hold=2):
        before = (self.yaw, self.hot)
        self.s.key(k, what, hold=hold)
        self.refresh()
        self.obs[(before, k)] = (self.yaw, self.hot)          # what the real editor did: it overrides the static guess

    def route(self, face):
        """BFS over (yaw, face): the keys the real editor was seen to do, else the static dump's guess."""
        pos = [calib.positions(f) for f in self.cal["yaws"]]
        start = (self.yaw, self.hot)
        seen, todo = {start: []}, [start]
        while todo:
            nxt = []
            for st in todo:
                y, f = st
                for k in ("up", "down", "left", "right", "e", "q"):
                    if (st, k) in self.obs:
                        g = self.obs[(st, k)]
                    elif k == "e":
                        g = ((y + 1) % 8, f)
                    elif k == "q":
                        g = ((y - 1) % 8, f)
                    else:
                        g = (y, calib.nav(pos[y], f, k)) if f in pos[y] else st
                    if g not in seen and g != st:
                        seen[g] = seen[st] + [k]
                        if g[1] == face:
                            return seen[g], g
                        nxt.append(g)
            todo = nxt
        return None

    def go(self, face, hold=2):
        seen = {}
        while self.hot != face:
            r = self.route(face)
            if r is None:
                self.skipped.append(face)
                return False
            k = r[0][0]
            n = seen[(self.hot, self.yaw, k)] = seen.get((self.hot, self.yaw, k), 0) + 1
            if n > 3 or len(seen) > 80:
                print("pointer: gave up on face %d at hot=%d yaw=%d (key %s tried %d times, %d states)" % (face, self.hot, self.yaw, k, n, len(seen)))
                self.skipped.append(face)
                return False
            self.key(k, "%s: turn the view" % k if k in ("q", "e") else "", hold=22 if k in ("q", "e") else hold)
        return True

    def pick(self, face, what="", hold=2):
        if self.go(face, hold):
            self.key("space", what, hold=hold)

    def cost(self, face):
        r = self.route(face)
        return (10 ** 6 if r is None else len(r[0]) + 12 * sum(k in "qe" for k in r[0]), face)

    def tour(self, faces, what="", hold=2):
        """Space on each face, always going to the cheapest one next."""
        todo = list(faces)
        first = True
        while todo:
            todo.sort(key=self.cost)
            f = todo.pop(0)
            if self.cost(f)[0] >= 10 ** 6:
                self.skipped.append(f)
                continue
            self.pick(f, what if first else "", hold)
            first = False


def keep_flat(faces):
    """The faces that stay flat colours: legs, belly and tail tip of Pico (warm colours)."""
    return [f["i"] for f in faces if f["flat"] is not None and WARM(f["flat"])]


def eye_faces(faces):
    """The two outer front faces of the head: normal +z, in the top quarter of the model, the most forward ones."""
    ymax = max(f["c"][1] for f in faces)
    ymin = min(f["c"][1] for f in faces)
    top = [f for f in faces if f["n"][2] > 0.9 and f["c"][1] >= ymin + 0.85 * (ymax - ymin)]
    zmax = max(f["c"][2] for f in top)
    front = sorted((f for f in top if f["c"][2] > zmax - 0.005), key=lambda f: f["c"][0])
    return [front[0]["i"], front[-1]["i"]]


def build_studio(s, cal, session, t0, t_end):
    at = lambda dt: t0 + dt
    s.until(t0, "start of Studio")
    s.chapter("Textured models in bm Studio")
    s.scene("bm Studio ▸ menu", [("Ctrl+O", "open"), ("Esc", "menu")])
    s.say("Back in bm Studio, we open the same project. The models are still flat colours.")
    s.chord("o", "Ctrl+O: open")
    s.until(at(3))
    s.key("enter", "TROPIC.BME")
    s.until(at(6), "F2")                              # 2:30
    s.key("f2", "F2: models")
    s.scene("bm Studio ▸ F2 models", [("Up Down", "model"), ("Enter", "build it")])
    s.sec(1)
    s.shot("s_models")
    s.until(at(12), "Enter")                          # 2:36
    s.key("enter", "Enter: build it")
    s.scene("bm Studio ▸ F1 build", [("3", "select"), ("a", "all faces"), ("Tab", "the sheet"), ("Enter", "take / put")])
    s.say("Select the crate, tool three, A for all faces. Tab opens the sheet: pick the wood and press Enter, "
          "then Enter again to put it on the faces.")
    s.until(at(14), "tool 3")                         # 2:38
    s.key("3", "3: select tool")
    s.until(at(15))
    s.key("a", "a: all faces")
    s.sec(0.5)
    s.shot("s_crate_all")
    s.until(at(17), "Tab")                            # 2:41
    s.key("tab", "Tab: the sheet")
    s.scene("bm Studio ▸ the sheet", [("Arrows", "choose a tile"), ("z", "tile size"), ("Enter", "take it")])
    s.until(at(18), "z")                              # 2:42
    for i in range(3):                                # 16 -> 32 -> 8 -> 16
        s.key("z", "z: tile size" if i == 0 else "", hold=24)
    s.until(at(20), "arrows")                         # 2:44
    TILE = {n: i for i, n in enumerate(art.NAMES)}
    pk = {"x": 0, "y": 0}                             # where the sheet picker is (tiles of 16)

    def pick_tile(name, what):
        tx, ty = TILE[name], art.ROW_Y // 16
        for _ in range(abs(tx - pk["x"])):
            s.key("right" if tx > pk["x"] else "left", hold=8)
        for _ in range(abs(ty - pk["y"])):
            s.key("down" if ty > pk["y"] else "up", hold=8)
        pk["x"], pk["y"] = tx, ty
        s.sec(0.5)
        s.shot("s_pick_" + name)
        s.key("enter", what)

    pick_tile("wood", "Enter: take the wood")
    s.until(at(25), "Enter put")                      # 2:49
    s.key("enter", "Enter: put it on the faces")
    s.sec(1)
    s.shot("s_crate_wood")
    s.until(at(27), "]")                              # 2:52
    s.scene("bm Studio ▸ F1 build ▸ select", [("]  [", "next / previous model"), ("a", "all faces / none"), ("Space", "choose a face"),
                                              ("Enter", "put the tile")])
    s.say("Same for Pico: the skin on every face. Then the two outer front faces of the head get the face tile.")
    s.key("]", "]: next model", hold=30)
    s.key("]", "]: next model", hold=50)
    s.shot("s_pico")
    pf = cal["yaws"][0]
    keep, eyes = keep_flat(pf), eye_faces(pf)
    s.until(at(29), "3 a")                            # 2:54
    s.key("3", "3: select tool")
    s.key("a", "a: all faces")
    ptr = Pointer(s, cal, session, round(t0 * 60))
    s.until(at(30), "Tab")                            # 2:55
    s.key("tab", "Tab: the sheet")
    pick_tile("skin", "Enter: take the skin")
    s.until(at(33), "exclude")                        # 2:58
    ptr.tour(keep, "Space: not the legs, the belly, the tail tip", hold=1)
    s.sec(0.4)
    print("studio: %d faces to keep flat, %d unreachable %s; skin tour ends at %.1f s" % (len(keep), len(ptr.skipped), sorted(ptr.skipped), s.t / 60))
    s.shot("s_pico_keep")
    s.key("enter", "Enter: skin on the body")
    s.sec(1)
    s.shot("s_pico_skin")
    s.until(at(36), "eyes")                           # 3:00
    s.key("a", "a: none")
    ptr.tour(eyes, "Space: the two front faces of the head", hold=3)
    print("studio: eye faces %s, unreachable total %s; eyes done at %.1f s" % (eyes, sorted(ptr.skipped), s.t / 60))
    s.shot("s_pico_eyes")
    s.until(at(38), "Tab face")                       # 3:02
    s.key("tab", "Tab: the sheet")
    pick_tile("face", "Enter: take the face tile")
    s.key("enter", "Enter: put it on the two faces")
    s.sec(0.5)
    s.shot("s_pico_face")
    s.until(at(40), "5")                              # 3:04
    s.chapter("Painting a model")
    s.scene("bm Studio ▸ F1 build ▸ paint", [("5", "paint"), ("Enter", "open the tile"), ("c", "colour"), ("Space", "paint a pixel"),
                                             ("e", "clear"), ("i", "pick"), ("Esc", "close")])
    s.say("Tool five paints the pixels of a face's tile. Pick white and a dark colour and paint an eye. "
          "The model changes while you paint, and every face with that tile follows.")
    s.key("5", "5: paint tool")
    s.until(at(42), "Enter paint")                    # 3:06
    s.key("enter", "Enter: paint the face's tile")
    s.sec(1)
    s.shot("s_paint_open")
    s.until(at(44), "c white")                        # 3:08

    def colour(n, what):                              # the list of c: the palette in order, the cursor starts on 1
        s.key("c", "c: pick " + what)
        s.sec(0.4)
        for _ in range(n - 1):
            s.key("down", hold=6)
        s.key("enter", "Enter: " + what)

    colour(6, "white")
    s.until(at(46), "paint")                          # 3:10
    # the panel starts at the middle (8,8): the eye is 3 white pixels and the pupil (art.EYE_WHITE, EYE_DARK)
    s.key("left", hold=10)
    s.key("space", "Space: paint a pixel", hold=10)
    s.key("up", hold=10)
    s.key("space", hold=10)
    s.key("right", hold=10)
    s.key("space", hold=10)
    s.key("down", hold=10)
    s.key("space", hold=10)
    s.sec(0.5)
    s.shot("s_paint_white")
    s.until(at(48), "c dark")                         # 3:12
    colour(2, "dark")
    s.key("space", "Space: the pupil", hold=10)
    s.sec(0.4)
    s.shot("s_paint_pupil")
    s.until(at(50), "i e")                            # 3:14
    s.key("i", "i: pick the colour", hold=20)
    s.key("e", "e: clear the pixel", hold=22)
    s.sec(0.4)
    s.shot("s_paint_clear")
    s.key("space", "Space: paint it again", hold=20)
    s.until(at(51), "Esc")                            # 3:15
    s.key("esc", "Esc: close")
    s.sec(0.5)
    s.shot("s_pico_done")
    s.until(at(52), "[")                              # 3:16
    s.key("[", "[: previous model", hold=40)
    s.until(at(53), "apple")                          # 3:17
    s.scene("bm Studio ▸ F1 build ▸ select", [("3", "select"), ("a", "all faces"), ("Tab", "the sheet"), ("Enter", "take / put")])
    s.say("The apple gets its tile. Control S saves.")
    s.key("3", "3: select tool")
    s.key("a", "a: all faces")
    s.key("tab", "Tab: the sheet")
    pick_tile("apple", "Enter: take the apple")
    s.key("enter", "Enter: put it on the faces")
    s.sec(0.8)
    s.shot("s_apple")
    s.until(at(56), "Ctrl+S")                         # 3:20
    s.chord("s", "Ctrl+S: save")
    s.sec(1.5)
    s.shot("s_saved")
    s.until(at(58), "F2")                             # 3:22
    s.chapter("Next: animation")
    s.scene("bm Studio ▸ F2 models", [("Up Down", "model")])
    s.say("Next time: bm Animator. We give Pico a skeleton and make him run.")
    s.key("f2", "F2: models")
    for k in ("up", "up", "down", "down"):
        s.sec(1.2)
        s.key(k, "next model")
    s.sec(1)
    s.shot("s_end")
    s.until(t_end, "end")
    s.end()
