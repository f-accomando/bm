#!/usr/bin/env python3
"""check_bmm.py: can bm Mesh and bm Animator read these model files (.bmm)?

  python3 scripts/check_bmm.py FILE.bmm [FILE.bmm ...] [--bmhost PATH]      (default build/host/bmhost-bin: `make build/host/bmhost-bin`)

Puts the files on a fake SD card (/bmm and /docs) and runs a small cart in bmhost, the PC build of the kernel. The cart uses the
code the editors use (`require "bm3d"`: T.model_files, T.import_model, T.decode_model) and the kernel's own file_write and res_read.
The expected numbers come from the files themselves (bm_core). For every model of every file it checks: the import, the vertex and
face counts, the tool limits (4096 vertices, 16384 triangles), the skeleton (bones, clips, one bone for each vertex), every face
index, every vertex a finite number, and what the animation editor decodes (the rig and its clips). For every file: the name is a
valid file name of /bmm (1 to 8 characters of a-z 0-9 _ -), it is listed, and the kernel accepts the bytes (file_write) and reads
them back (res_read, the same bytes). Exit 0 when all checks pass.
"""
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SKILL = os.path.join(ROOT, ".claude", "skills", "bm-forge")
sys.path.insert(0, SKILL)
from bm_core import container  # noqa: E402

LUA = r'''
local B = require "bm3d"
local T, G = B, B.G
local passed, total, frame = 0, 0, 0
local function check(ok, what)
  total = total + 1
  if ok then passed = passed + 1 else log("bmm: FAIL " .. what) end
end
local EXPECT = {
%EXPECT%
}
local FILES = { %FILES% }
local function run()
  local have = {}
  for _, e in ipairs(T.model_files()) do have[e.name] = e.kind end
  for _, f in ipairs(FILES) do check(have[f] == "bmm", f .. ".bmm is listed in /bmm as a model") end
  for _, ex in ipairs(EXPECT) do
    local tag = ex.file .. "/" .. ex.model
    local it, why = T.import_model(ex.file, { model = ex.model })
    check(it ~= nil, tag .. " imports: " .. tostring(why))
    if it then
      check(#it.v == ex.nv, tag .. " vertices " .. #it.v .. " expected " .. ex.nv)
      check(#it.f == ex.nf, tag .. " faces " .. #it.f .. " expected " .. ex.nf)
      check(#it.v <= 4096 and #it.f <= 16384, tag .. " inside the tool limits")
      if ex.nb > 0 then
        check(it.bones == ex.nb, tag .. " bones " .. tostring(it.bones) .. " expected " .. ex.nb)
        check(it.clips == ex.nc, tag .. " clips " .. tostring(it.clips) .. " expected " .. ex.nc)
        check(G.skin_state(it) == "skinned", tag .. " skin state " .. tostring(G.skin_state(it)))
        local names = G.rig_bones(it)
        check(names and #names == ex.nb, tag .. " rig_bones reads the names")
        local faces, rig = T.decode_model(it.mc, it.ac)
        check(faces ~= nil and rig ~= nil, tag .. " decode_model gives faces and a rig")
        if rig and ex.nc > 0 then
          local n = rig.clips and #rig.clips or -1
          check(n == ex.nc, tag .. " the animator sees " .. n .. " clips, expected " .. ex.nc)
        end
      end
      local okf = true
      for _, f in ipairs(it.f) do
        for k = 1, 3 do if f[k] < 1 or f[k] > #it.v then okf = false end end
      end
      check(okf, tag .. " every face index is a vertex")
      local okv = true
      for _, v in ipairs(it.v) do
        for k = 1, 3 do if v[k] ~= v[k] or math.abs(v[k]) > 100 then okv = false end end
      end
      check(okv, tag .. " every vertex is a finite number")
    end
  end
  for i, f in ipairs(FILES) do
    local bytes = doc_read(f .. ".bmm")
    check(bytes ~= nil, f .. " is in /docs")
    local ok, path = file_write("chk" .. i .. ".bmm", bytes or "")
    check(ok == true, f .. " passes the kernel check of file_write: " .. tostring(path))
    local r, why = res_read("/bmm/chk" .. i .. ".bmm")
    check(r ~= nil and r.kind == 1 and r.sections[8] ~= nil, f .. " res_read: " .. tostring(why))
  end
end
function _update() frame = frame + 1 end
function _draw()
  cls(0x102030)
  if frame == 2 then
    local ok, err = pcall(run)
    if not ok then log("bmm: ERROR " .. tostring(err)) end
    log(string.format("bmm: %d/%d checks passed", passed, total))
  end
end
'''


def main(argv):
    args = [a for a in argv[1:]]
    host = os.path.join(ROOT, "build", "host", "bmhost-bin")
    if "--bmhost" in args:
        i = args.index("--bmhost")
        host = args[i + 1]
        del args[i:i + 2]
    files = args
    if not files:
        print(__doc__)
        return 2
    if not os.path.exists(host):
        print("no bmhost: run `make build/host/bmhost-bin`")
        return 2
    fails = []
    stems = []
    exp = []
    for path in files:
        stem = os.path.splitext(os.path.basename(path))[0].lower()
        stems.append(stem)
        if not re.match(r"^[a-z0-9_\-]{1,8}$", stem):
            fails.append("%s: the name must be 1 to 8 characters of a-z 0-9 _ - (the editors list /bmm by that rule)" % path)
            continue
        c = container.read_file(path)
        mesh, anim = c.mesh(), c.anim()
        for m in mesh.models:
            rig = anim.get(m.name) if anim else None
            exp.append('  { file = "%s", model = "%s", nv = %d, nf = %d, nb = %d, nc = %d },'
                       % (stem, m.name, len(m.verts), len(m.faces), len(rig.bones) if rig else 0, len(rig.clips) if rig else 0))
    work = tempfile.mkdtemp(prefix="check-bmm-")
    try:
        sd = os.path.join(work, "sd")
        os.makedirs(os.path.join(sd, "bmm"))
        os.makedirs(os.path.join(sd, "docs"))
        for path, stem in zip(files, stems):
            for d in ("bmm", "docs"):
                shutil.copy(path, os.path.join(sd, d, stem + ".bmm"))
        lua = LUA.replace("%EXPECT%", "\n".join(exp)).replace("%FILES%", ", ".join('"%s"' % s for s in stems))
        lua_path = os.path.join(work, "main.lua")
        open(lua_path, "w").write(lua)
        # a cart with one model, only to carry the Lua (bm_forge makes it)
        probe = os.path.join(work, "probe.bm")
        subprocess.check_call([sys.executable, os.path.join(SKILL, "scripts", "bm_forge.py"), "mesh",
                               os.path.join(SKILL, "examples", "humanoid_boxes.json"), "-o", probe], stdout=subprocess.DEVNULL)
        cart = os.path.join(work, "check.bm")
        subprocess.check_call([sys.executable, os.path.join(SKILL, "scripts", "bm_forge.py"), "export", "cart", probe, "--lua", lua_path,
                               "-o", cart, "--title", "bmm check", "--budget", "20000"], stdout=subprocess.DEVNULL)
        out = subprocess.run([host, cart, "--sd", sd, "--seconds", "3"], capture_output=True, text=True, timeout=240)
        log = out.stdout + out.stderr
        for line in log.splitlines():
            if line.startswith("bmm:"):
                print(line)
        m = re.search(r"^bmm: (\d+)/(\d+) checks passed", log, re.M)
        if m is None or m.group(1) != m.group(2):
            fails.append("the cart: not all checks passed")
            if m is None:
                print(log[-1500:])
        for i, (path, stem) in enumerate(zip(files, stems), 1):
            p = os.path.join(sd, "bmm", "chk%d.bmm" % i)
            same = os.path.exists(p) and open(p, "rb").read() == open(path, "rb").read()
            print("kernel copy of %s is the same bytes: %s" % (stem + ".bmm", same))
            if not same:
                fails.append("%s: the kernel copy differs" % path)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in fails:
        print("FAIL", f)
    print("check_bmm: %s" % ("FAIL (%d)" % len(fails) if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
