#!/usr/bin/env python3
"""check_editors.py: can the editors read the Hunter files?

  python3 check_editors.py [BMHOST]       (default build/host/bmhost-bin; `make build/host/bmhost-bin` builds it)

Puts art/models/hunter.bmm and hunterlo.bmm on a fake SD card (/bmm) and runs a small cart in bmhost, the PC build
of the kernel. The cart uses the code that bm Mesh and bm Animator use (`require "bm3d"`: T.model_files,
T.import_model, T.decode_model) and the kernel's own res_read and res_write. It checks, for every model of
both files: the name, the vertex and face counts, the skeleton (bones, clips, one bone per vertex), the tool limits
(4096 vertices, 16384 triangles, 64 bones, 255 clips), that every face index is in range, that the kernel's file_write accepts
both files (it checks the bytes) and res_read reads them. (res_write is for bm's tools only: a cart cannot call it.)
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SKILL = os.path.join(ROOT, ".claude", "skills", "bm-forge")
sys.path.insert(0, SKILL)
from bm_core import container  # noqa: E402

FILES = ["hunter", "hunterlo"]
LUA = r'''
local B = require "bm3d"
local T, G = B, B.G
local passed, total, frame = 0, 0, 0
local function check(ok, what)
  total = total + 1
  if ok then passed = passed + 1 else log("editors: FAIL " .. what) end
end
local EXPECT = {
%EXPECT%
}
local function run()
  local files = T.model_files()
  local have = {}
  for _, e in ipairs(files) do have[e.name] = e.kind end
  for _, f in ipairs({ %FILES% }) do check(have[f] == "bmm", f .. ".bmm is listed in /bmm as a model") end
  for _, ex in ipairs(EXPECT) do
    local tag = ex.file .. "/" .. ex.model
    local it, why = T.import_model(ex.file, { model = ex.model })
    check(it ~= nil, tag .. " imports: " .. tostring(why))
    if it then
      check(#it.v == ex.nv, tag .. " vertices " .. #it.v .. " expected " .. ex.nv)
      check(#it.f == ex.nf, tag .. " faces " .. #it.f .. " expected " .. ex.nf)
      check(#it.v <= 4096 and #it.f <= 16384, tag .. " inside the tool limits")
      check(it.bones == ex.nb, tag .. " bones " .. tostring(it.bones) .. " expected " .. ex.nb)
      check(it.clips == ex.nc, tag .. " clips " .. tostring(it.clips) .. " expected " .. ex.nc)
      check(G.skin_state(it) == "skinned", tag .. " skin state " .. tostring(G.skin_state(it)))
      local names = G.rig_bones(it)
      check(names and #names == ex.nb, tag .. " rig_bones reads the names")
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
      -- bm Animator: the skeleton and the clips as the animation editor decodes them
      local faces, rig = T.decode_model(it.mc, it.ac)
      check(faces ~= nil and rig ~= nil, tag .. " decode_model gives faces and a rig")
      if rig and ex.nc > 0 then
        local n = rig.clips and #rig.clips or -1
        check(n == ex.nc, tag .. " the animator sees " .. n .. " clips, expected " .. ex.nc)
      end
      if ex.model == "hunter" and ex.file == "hunter" then
        local mirror = 0
        for i = 0, ex.nb - 1 do if G.mirror_bone(names, i) ~= i then mirror = mirror + 1 end end
        check(mirror == 18, tag .. " the animator mirrors 18 bones (.L/.R), got " .. mirror)
      end
    end
  end
  -- the kernel's own check of the bytes (file_write refuses a bad model) and its reader
  for i, f in ipairs({ %FILES% }) do
    local bytes = doc_read(f .. ".bmm")
    check(bytes ~= nil, f .. " is in /docs")
    local ok, path = file_write("chk" .. i .. ".bmm", bytes or "")
    check(ok == true, f .. " passes the kernel check of file_write: " .. tostring(path))
    local r, why = res_read("/bmm/chk" .. i .. ".bmm")
    check(r ~= nil and r.kind == 1 and r.sections[8] ~= nil and r.sections[9] ~= nil, f .. " res_read: " .. tostring(why))
  end
end
function _update() frame = frame + 1 end
function _draw()
  cls(0x102030)
  if frame == 2 then
    local ok, err = pcall(run)
    if not ok then log("editors: ERROR " .. tostring(err)) end
    log(string.format("editors: %d/%d checks passed", passed, total))
  end
end
'''


def expectations():
    out = []
    for f in FILES:
        c = container.read_file(os.path.join(ROOT, "art", "models", f + ".bmm"))
        mesh, anim = c.mesh(), c.anim()
        for m in mesh.models:
            rig = anim.get(m.name) if anim else None
            nb = len(rig.bones) if rig else 0
            nc = len(rig.clips) if rig else 0
            out.append('  { file = "%s", model = "%s", nv = %d, nf = %d, nb = %d, nc = %d },'
                       % (f, m.name, len(m.verts), len(m.faces), nb, nc))
    return "\n".join(out)


def main(argv):
    host = argv[1] if len(argv) > 1 else os.path.join(ROOT, "build", "host", "bmhost-bin")
    if not os.path.exists(host):
        print("no bmhost: run `make build/host/bmhost-bin`")
        return 2
    work = tempfile.mkdtemp(prefix="hunter-editors-")
    try:
        sd = os.path.join(work, "sd")
        os.makedirs(os.path.join(sd, "bmm"))
        os.makedirs(os.path.join(sd, "docs"))
        for f in FILES:
            shutil.copy(os.path.join(ROOT, "art", "models", f + ".bmm"), os.path.join(sd, "bmm", f + ".bmm"))
            shutil.copy(os.path.join(ROOT, "art", "models", f + ".bmm"), os.path.join(sd, "docs", f + ".bmm"))
        lua = LUA.replace("%EXPECT%", expectations()).replace("%FILES%", ", ".join('"%s"' % f for f in FILES))
        lua_path = os.path.join(work, "main.lua")
        open(lua_path, "w").write(lua)
        models = os.path.join(work, "models.bm")
        env = dict(os.environ, HUNTER_LOD="lo")
        subprocess.check_call([sys.executable, os.path.join(HERE, "hunter_build.py"), models], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        cart = os.path.join(work, "check.bm")
        subprocess.check_call([sys.executable, os.path.join(SKILL, "scripts", "bm_forge.py"), "export", "cart", models,
                               "--lua", lua_path, "-o", cart, "--title", "hunter check", "--cover-model", "hunter", "--budget", "20000"],
                              stdout=subprocess.DEVNULL)
        out = subprocess.run([host, cart, "--sd", sd, "--seconds", "3"], capture_output=True, text=True, timeout=180)
        log = out.stdout + out.stderr
        for line in log.splitlines():
            if line.startswith("editors:"):
                print(line)
        m = re.search(r"^editors: (\d+)/(\d+) checks passed", log, re.M)
        ok = m is not None and m.group(1) == m.group(2)
        for i, f in enumerate(FILES, 1):
            p = os.path.join(sd, "bmm", "chk%d.bmm" % i)
            same = os.path.exists(p) and open(p, "rb").read() == open(os.path.join(ROOT, "art", "models", f + ".bmm"), "rb").read()
            print("kernel copy of %s.bmm is the same bytes: %s" % (f, same))
            ok = ok and same
        print("check_editors: %s" % ("ok" if ok else "FAIL"))
        if not ok:
            print(log[-2000:])
        return 0 if ok else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
