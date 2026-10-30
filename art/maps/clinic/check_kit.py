#!/usr/bin/env python3
"""check_kit.py: can bm SDK read clinic.bmk, and can the player walk the map?

  python3 check_kit.py [KIT.bmk] [BMHOST]     (default art/maps/clinic.bmk and build/host/bmhost-bin)

1. Python (scripts/bmkit.py): the kit reads back, the models look out and have a map class, every id of the objects
   layer is in the object table, every model of the table exists in both looks.
2. Python, the layout: from the player start a person can walk to the Courtyard Gate, the atrium, the end of the alley
   and every lantern, through the doors that open (the locked doors and the gate are not passed), and the height step
   between two neighbour tiles is at most 0.5 units (the stair treads).
3. bmhost (the PC build of the kernel): a small cart puts the kit in /bmk and calls kit_load, the call bm SDK's menu entry
   "Import a map kit (/bmk)..." makes. It checks the project table (map size, layers, info), builds the 3D map with
   map3d_build, lists map3d_objects (the number of each OBJ id), asks model() for every model in both looks, and asks
   map3d_blocked about a wall tile and a floor tile.
"""
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, HERE)
import bmkit  # noqa: E402
import bmres  # noqa: E402
import build_clinic as B  # noqa: E402

fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL", msg)


def layers_of(f):
    return dict(bmres.layers_get(f))


def python_checks(f):
    lines, problems = bmkit.describe(f)
    for p in problems:
        check(False, p)
    names = {n for n, _ in bmres.mesh_parse(f.get(bmres.SEC_MESH))[1]}
    for n, *_ in B.OBJECTS:
        check(n in names and n + bmkit.FLAT_SUFFIX in names, "model %s in both looks" % n)
    lay = layers_of(f)
    cw = bmres.map_get(f)[0]
    check(cw == B.CW, "map width %d cells" % cw)
    for n in bmkit.LAYER_NAMES:
        check(n in lay and len(lay[n]) == B.CW * B.CH, "layer %s has %d cells" % (n, B.CW * B.CH))
    return lay


def walk_checks(lay):
    W = B.CW

    def at(layer, x, z):
        return lay[layer][(z * 2) * W + x * 2]
    obj = {(x, z): at("objects", x, z) for z in range(B.TH) for x in range(B.TW) if at("objects", x, z)}
    ids = B.IDS
    start = next(p for p, v in obj.items() if v == B.PLAYER_START)
    gate = next(p for p, v in obj.items() if v == ids["gate_ns"])
    solid_things = {ids[n] for n in ("bed", "bed_ns", "table", "cabinet", "lamp", "lantern", "gate_ns", "crate", "barrel",
                                     "pillar", "cart", "barricade")}
    locked = {ids["door_ew_l"], ids["door_ns_l"]}

    def bfs(through_locked):
        """the tiles a person reaches from the start; a locked door is passed only when through_locked (it has a key)"""
        def walkable(x, z):
            if not (0 <= x < B.TW and 0 <= z < B.TH):
                return False
            if at("floor", x, z) == 0 or at("walls", x, z):
                return False
            o = obj.get((x, z))
            return o not in solid_things and (through_locked or o not in locked)
        seen = {start}
        q = collections.deque([start])
        while q:
            x, z = q.popleft()
            for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (x + dx, z + dz)
                if n in seen or not walkable(*n):
                    continue
                if abs(at("height", *n) - at("height", x, z)) * 0.25 > 0.5:        # a tread is at most 0.5 units
                    continue
                seen.add(n)
                q.append(n)
        return seen
    free, keyed = bfs(False), bfs(True)
    near = lambda p, s: any(abs(p[0] - a) + abs(p[1] - b) <= 1 for a, b in s)           # noqa: E731
    check(near(gate, free), "the player reaches the Courtyard Gate without a key")
    check((37, 10) in free, "the atrium is reachable without a key")
    check((47, 43) in free, "the end of the alley is reachable without a key")
    for n, v in obj.items():
        if v == ids["lantern"]:
            check(near(n, free), "the lantern at %s is reachable without a key" % (n,))
        if v == B.ENEMY:
            check(near(n, keyed), "the enemy at %s is reachable (with the keys)" % (n,))
    behind = [n for n, v in obj.items() if v == B.ENEMY and not near(n, free)]
    print("walk: %d tiles without a key, %d with keys; start %s, gate %s; %d enemies behind locked doors"
          % (len(free), len(keyed), start, gate, len(behind)))


LUA = r'''
local passed, total, frame = 0, 0, 0
local function check(ok, what)
  total = total + 1
  if ok then passed = passed + 1 else log("kit: FAIL " .. what) end
end
local OBJ = {
%OBJ%
}
local MODELS = { %MODELS% }
local function run()
  local p, err = kit_load("/bmk/clinic.bmk")
  check(p ~= nil, "kit_load: " .. tostring(err))
  if not p then return end
  check(p.map_w == %CW% and p.map_h == %CH%, "map size " .. tostring(p.map_w) .. "x" .. tostring(p.map_h))
  check(type(p.layers) == "table" and #p.layers == 4, "four layers: " .. tostring(p.layers and #p.layers))
  check(type(p.info) == "string" and p.info:find("[object bed]", 1, true) ~= nil, "info has the object table")
  check(p.info:find("[map clinic]", 1, true) ~= nil, "info has the map block")
  for _, n in ipairs(MODELS) do
    check(model(n) ~= nil, "model " .. n)
    check(model(n .. "_f") ~= nil, "model " .. n .. "_f")
  end
  local w = map3d_build({ floor = "floor", height = "height", walls = "walls", cell = 1.0, step = 0.25 })
  check(w ~= nil, "map3d_build")
  local nx, nz = map3d_size(w)
  check(nx == %TW% and nz == %TH%, "map3d_size " .. tostring(nx) .. "x" .. tostring(nz))
  local count = {}
  for _, o in ipairs(map3d_objects(w)) do count[o.kind] = (count[o.kind] or 0) + 1 end
  for id, n in pairs(OBJ) do
    local k = id == 1024 and "PLAYER_START" or ("OBJ" .. id)
    check((count[k] or 0) == n, k .. " count " .. tostring(count[k]) .. " expected " .. n)
  end
  local h = map3d_height(w, %SX%, %SZ%)
  check(h ~= nil and math.abs(h - %SH%) < 0.01, "height at the start " .. tostring(h) .. " expected %SH%")
  check(map3d_blocked(w, 3.5, %SH% + 1.0, 8.5, 0.3) == true, "a wall tile blocks")
  check(map3d_blocked(w, %SX%, %SH% + 1.0, %SZ%, 0.3) == false, "the start tile is free")
  local g = map3d_height(w, 72.5, 10.5)
  check(g ~= nil, "the gate tile has a floor")
  local s0, s1 = map3d_height(w, 22.5, 9.5), map3d_height(w, 31.5, 9.5)
  check(s0 and s1 and s0 > s1 + 1.5, "the hall stair goes down: " .. tostring(s0) .. " to " .. tostring(s1))
end
function _update() frame = frame + 1 end
function _draw()
  cls(0x102030)
  if frame == 2 then
    local ok, err = pcall(run)
    if not ok then log("kit: ERROR " .. tostring(err)) end
    log(string.format("kit: %d/%d checks passed", passed, total))
  end
end
'''


def host_checks(kit, host, lay):
    work = tempfile.mkdtemp(prefix="clinic-kit-")
    try:
        sd = os.path.join(work, "sd")
        os.makedirs(os.path.join(sd, "bmk"))
        shutil.copy(kit, os.path.join(sd, "bmk", "clinic.bmk"))
        W = B.CW
        count = collections.Counter(v for v in (lay["objects"][(z * 2) * W + x * 2] for z in range(B.TH) for x in range(B.TW)) if v)
        sx, sz = next((x, z) for z in range(B.TH) for x in range(B.TW) if lay["objects"][(z * 2) * W + x * 2] == B.PLAYER_START)
        sh = lay["height"][(sz * 2) * W + sx * 2] * 0.25
        lua = (LUA.replace("%OBJ%", "\n".join("  [%d] = %d," % (k, v) for k, v in sorted(count.items())))
               .replace("%MODELS%", ", ".join('"%s"' % n for n, *_ in B.OBJECTS))
               .replace("%CW%", str(B.CW)).replace("%CH%", str(B.CH)).replace("%TW%", str(B.TW)).replace("%TH%", str(B.TH))
               .replace("%SX%", "%g" % (sx + 0.5)).replace("%SZ%", "%g" % (sz + 0.5)).replace("%SH%", "%g" % sh))
        lua_path = os.path.join(work, "main.lua")
        open(lua_path, "w").write(lua)
        cart = os.path.join(work, "check.bm")
        subprocess.check_call([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", cart, "--lua", lua_path,
                               "--res", "640x360", "--title", "clinic kit check"], stdout=subprocess.DEVNULL)
        out = subprocess.run([host, cart, "--sd", sd, "--seconds", "3", "--tool"], capture_output=True, text=True, timeout=180)
        log = out.stdout + out.stderr
        for line in log.splitlines():
            if line.startswith("kit:"):
                print(line)
        m = re.search(r"^kit: (\d+)/(\d+) checks passed", log, re.M)
        check(m is not None and m.group(1) == m.group(2), "the cart: all checks passed")
        if m is None:
            print(log[-1500:])
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main(argv):
    kit = argv[1] if len(argv) > 1 else os.path.join(ROOT, "art", "maps", "clinic.bmk")
    host = argv[2] if len(argv) > 2 else os.path.join(ROOT, "build", "host", "bmhost-bin")
    f = bmres.read(kit)
    lay = python_checks(f)
    walk_checks(lay)
    if os.path.exists(host):
        host_checks(kit, host, lay)
    else:
        print("no bmhost: `make build/host/bmhost-bin` (the kernel checks were skipped)")
    print("check_kit: %s" % ("FAIL (%d)" % len(fails) if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
