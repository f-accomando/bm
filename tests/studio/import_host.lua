-- The model files of the editors (src/script/bm3d.lua: T.model_files, T.import_model, T.write_bmm,
-- T.import_glb_to_bmm; APP-9) on the PC: stand-ins for ls, glb3d, res_read and res_write (a table is the
-- SD card; the checks of the console's res_write are made again here, the C ones are in
-- tests/qemu_test.py test_res_write), and checks on what the functions ask and give.
--
--   luahost tests/studio/import_host.lua ROOT        (make test-studio)

local ROOT = arg[1] or "."
local checks, fails = 0, 0
local function check(ok, msg)
  checks = checks + 1
  if not ok then fails = fails + 1; io.write("FAIL " .. msg .. "\n") end
end

----------------------------------------------------------------- the API

local E = {}
for k, v in pairs(_G) do E[k] = v end
E.SCREEN_W, E.SCREEN_H = 640, 360
setmetatable(E, { __index = function(_, k)       -- the drawing and the keys: nothing here
  if k == "_G" then return E end
  return function() end
end })
E.require = function() return setmetatable({}, { __index = function() return function() end end }) end

local card = {                          -- the SD card: folder -> { name -> data }
  ["/bmm"] = {}, ["/glb"] = {},
}
local dirs = { ["/bmm"] = { "x.bmm" } }  -- (folders inside, listed as such)
local glb_calls = {}
local writes = {}

E.ls = function(dir)
  local out = {}
  for name, data in pairs(card[dir] or {}) do out[#out + 1] = { name = name, size = #data, dir = false } end
  for _, n in ipairs(dirs[dir] or {}) do out[#out + 1] = { name = n, size = 0, dir = true } end
  return out
end

local function path_of(path)
  local dir, name = path:match("^(/[^/]+)/([^/]+)$")
  if not dir then return nil end
  for n in pairs(card[dir] or {}) do
    if n:lower() == name:lower() then return dir, n end
  end
  return dir, nil, name
end

-- a model from a .glb: the table of glb3d; the file "bad.glb" is not a .glb
E.glb3d = function(path, o)
  glb_calls[#glb_calls + 1] = { path = path, o = o }
  local dir, n = path_of(path)
  if not n then return nil, path .. ": no such file" end
  if n:lower() == "bad.glb" then return nil, path .. ": not a .glb file" end
  local E_ = E
  local item = { name = o.name, v = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } },
                 f = { { 1, 2, 3, 0x336699 }, { 2, 4, 3, -1, { 0, 0, 8 * 4, 0, 0, 8 * 4 } } } }
  local G = E_.__G
  local side = o.sheet
  local textured = n:lower() ~= "flat.glb"
  local rec = G.encode_model({ name = o.name, v = item.v, f = textured and item.f or { item.f[1], item.f[1] }, dirty = true })
  local flat = G.encode_model({ name = o.name, v = item.v, f = { item.f[1], { 2, 4, 3, 0x808080 } }, dirty = true })
  local m = { record = rec, nv = 4, nf = 2, textured = textured }
  if textured then
    m.flat = flat
    m.texture = string.rep("\200\100\50\255", side * side)
  end
  return m
end

-- the model files of the console (kind 1 only here)
E.res_write = function(path, kind, t)
  writes[#writes + 1] = path
  if kind ~= 1 and kind ~= "model" then return nil, "res_write: only kind 1" end
  local name = path:match("^/bmm/([a-z0-9_%-]+)%.bmm$")
  if not name or #name > 8 then return nil, path .. ": a model goes to /bmm/NAME.bmm" end
  if not t.sections[8] then return nil, "a model file needs its MESH section (8)" end
  for k in pairs(t.sections) do
    if k ~= 2 and k ~= 5 and k ~= 8 and k ~= 9 and k ~= 10 then error("sections: a model file holds ...") end
  end
  if t.sections[2] and t.sections[5] then return nil, "one sheet only (2 or 5)" end
  local _, have = path_of(path)
  if have and not t.replace then
    return nil, path .. ": is there already (replace = true changes it)"
  end
  if have then card["/bmm"][have] = nil end
  card["/bmm"][name .. ".bmm"] = { kind = 1, name = t.name or name, author = t.author or "", sections = t.sections }
  return true
end

E.res_read = function(path)
  local dir, n = path_of(path)
  if not n then return nil, path .. ": no such file" end
  local f = card[dir][n]
  if type(f) ~= "table" then return nil, path .. ": not a resource file" end
  local s = {}
  for k, v in pairs(f.sections) do s[k] = v end
  return { kind = f.kind, name = f.name, author = f.author, sections = s }
end

local T = assert(loadfile(ROOT .. "/src/script/bm3d.lua", "t", E))()
local G = T.G
E.__G = G

----------------------------------------------------------------- models for the card

local function tri_item(name)
  return { name = name, v = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } }, f = { { 1, 2, 3, 0x336699 } }, dirty = true }
end

local function name16(s) return s .. string.rep("\0", 16 - #s) end

-- a skeleton chunk (one bone, no clips) for a model of nv vertices
local function rig(name, nv)
  local vb = string.rep("\0", nv) .. string.rep("\0", ((nv + 3) & ~3) - nv)
  return name16(name) .. string.pack("<I2I2I2I2", 1, 0, nv, 0) ..
         name16("root") .. string.pack("<i2I2ffffff", -1, 0, 0, 0, 0, 0, 1, 0) .. vb
end

local SHEET = string.pack("<I2I2", 8, 8) .. string.rep("\10\20\30\255", 64)

local function put_bmm(file, name, models, anims, sheet, inset)
  local parts = {}
  for _, m in ipairs(models) do parts[#parts + 1] = G.encode_model(m) end
  local s = { [8] = string.pack("<I2I2I4", #parts, inset or 64, 0) .. table.concat(parts) }
  if anims then s[9] = string.pack("<I2I2I4", #anims, 0, 0) .. table.concat(anims) end
  if sheet then s[2] = sheet end
  card["/bmm"][file] = { kind = 1, name = name, author = "ann", sections = s }
end

put_bmm("knight.bmm", "The Knight", { tri_item("knight") }, { rig("knight", 3) }, SHEET, 32)
put_bmm("ZED.BMM", "Zed", { tri_item("zed"), tri_item("zed2") })
card["/bmm"]["notes.txt"] = "text"
card["/bmm"]["bad.bmm"] = "not a resource file"
card["/glb"]["knight.glb"] = "glTF"
card["/glb"]["Robot.GLB"] = "glTF"
card["/glb"]["a.glb"] = "glTF"
card["/glb"]["flat.glb"] = "glTF"
card["/glb"]["bad.glb"] = "glTF"
card["/glb"]["Long Model Name.glb"] = "glTF"
card["/glb"]["readme.txt"] = "text"

----------------------------------------------------------------- the list

do
  local list = T.model_files()
  local names, kinds = {}, {}
  for i, e in ipairs(list) do names[i], kinds[i] = e.name, e.kind .. (e.glb and "+glb" or "") end
  check(table.concat(names, ",") == "a,bad,flat,knight,long model name,robot,zed",
        "model_files: lower case, sorted, one entry per name (" .. table.concat(names, ",") .. ")")
  local by = {}
  for _, e in ipairs(list) do by[e.name] = e end
  check(by.knight.kind == "bmm" and by.knight.glb == true, "model_files: knight is in both: kind bmm, glb = true")
  check(by.robot.kind == "glb" and by.robot.glb == true, "model_files: robot is only a .glb: kind glb")
  check(by.zed.kind == "bmm" and not by.zed.glb, "model_files: ZED.BMM is zed, kind bmm, no .glb")
  check(by.bad.kind == "bmm" and by.bad.glb == true, "model_files: bad has both files")
  check(by.notes == nil and by.readme == nil and by.x == nil, "model_files: no text file, no folder")
end

----------------------------------------------------------------- import from /bmm

do
  local it, err = T.import_model("knight")
  check(it and not err, "import_model knight: " .. tostring(err))
  check(it.kind == "model" and it.name == "knight" and #it.v == 3 and #it.f == 1 and it.f[1][4] == 0x336699,
        "import_model: the model of the .bmm")
  check(it.ac and it.bones == 1 and it.vb and #it.vb == 3, "import_model: the skeleton comes with it")
  check(it.sheet and it.sheet.type == 2 and it.sheet.data == SHEET, "import_model: the sheet comes with it")
  check(it.inset == 0.125 and it.file.name == "The Knight" and it.file.author == "ann" and it.from == "bmm",
        "import_model: the margin and the file's name")
  local z = T.import_model("zed", { model = "zed2" })
  check(z and z.name == "zed2", "import_model: a model of the file by name")
  z = T.import_model("ZED.bmm", { model = 2, name = "My Zed!" })
  check(z and z.name == "myzed", "import_model: by number, the name of the item cleaned")
  check(select(2, T.import_model("zed", { model = "none" })) == "zed.bmm has no model none", "import_model: no such model")
  local bad, msg = T.import_model("bad")
  check(bad == nil and msg:find("not a resource file"), "import_model: a broken .bmm gives the message of res_read")
  bad, msg = T.import_model("nothing")
  check(bad == nil and msg == "no model nothing in /bmm or /glb", "import_model: no such model")
  bad, msg = T.import_model("zed", { from = "glb" })
  check(bad == nil and msg == "no zed.glb in /glb", "import_model: from glb, there is none")
end

----------------------------------------------------------------- import from /glb

do
  glb_calls = {}
  local it, err = T.import_model("robot")
  check(it and it.from == "glb" and it.name == "robot", "import_model robot: " .. tostring(err))
  local c = glb_calls[1]
  check(c and c.path == "/glb/robot.glb" and c.o.name == "robot" and c.o.height == 2 and c.o.faces == 0 and c.o.sheet == 256,
        "import_model: glb3d gets the defaults (height 2, faces 0, sheet 256)")
  check(#it.v == 4 and #it.f == 2 and it.f[2][4] ~= -1 and it.sheet == nil,
        "import_model: a .glb is flat colours, no sheet, by default")
  glb_calls = {}
  it = T.import_model("robot", { texture = true, height = 3, faces = 500, sheet = 64, name = "Big Robot" })
  c = glb_calls[1]
  check(c.o.height == 3 and c.o.faces == 500 and c.o.sheet == 64 and c.o.name == "bigrobot",
        "import_model: the options go to glb3d")
  check(it.sheet and it.sheet.type == 2 and #it.sheet.data == 4 + 64 * 64 * 4 and it.sheet.data:sub(1, 4) == "\64\0\64\0" and
        it.f[2][4] == -1, "import_model: texture = true: the texture is the item's sheet, the faces are textured")
  it = T.import_model("flat", { texture = true })
  check(it.sheet == nil, "import_model: a .glb with no texture has no sheet")
  local bad, msg = T.import_model("bad", { from = "glb" })
  check(bad == nil and msg:find("not a .glb file"), "import_model: the message of glb3d")
end

----------------------------------------------------------------- write

do
  local it = T.import_model("knight")
  writes = {}
  local ok, path = T.write_bmm(it, "Copy.bmm")
  check(ok == true and path == "/bmm/copy.bmm", "write_bmm: the name in lower case: " .. tostring(path))
  local f = card["/bmm"]["copy.bmm"]
  check(f and f.name == "The Knight" and f.author == "ann", "write_bmm: the name and the author of the file stay")
  check(f.sections[2] == SHEET and f.sections[9] and f.sections[5] == nil, "write_bmm: the sheet and the skeleton stay")
  check(f.sections[8]:sub(1, 8) == string.pack("<I2I2I4", 1, 32, 0), "write_bmm: one model, the margin stays")
  local back = T.import_model("copy")
  check(back and G.encode_model(back) == G.encode_model(it) and back.ac == it.ac and back.inset == it.inset,
        "write_bmm, import_model: the same model, the same skeleton")
  local ok2, msg = T.write_bmm(it, "copy")
  check(ok2 == nil and msg:find("is there already"), "write_bmm: a file that is there is not changed")
  check(T.write_bmm(it, "copy", { replace = true, title = "Again", author = "bob" }) == true and
        card["/bmm"]["copy.bmm"].name == "Again" and card["/bmm"]["copy.bmm"].author == "bob", "write_bmm: replace, title, author")
  for _, n in ipairs({ "toolongname", "a/b", "", "sp ace", "Q!" }) do
    local o, m = T.write_bmm(it, n)
    check(o == nil and m:find("1 to 8"), "write_bmm: the name '" .. n .. "' is refused")
  end
  -- renamed item: the skeleton follows the name
  it.name = "renamed"
  T.write_bmm(it, "renamed")
  local rr = T.import_model("renamed")
  check(rr and rr.name == "renamed" and rr.ac and rr.ac:sub(1, 16) == name16("renamed"), "write_bmm: a new name for the model and its skeleton")
  -- the vertices changed: no skeleton that does not fit
  local ed = T.import_model("knight")
  G.add_vertex(ed, { 2, 2, 2 })
  ed.dirty = true
  T.write_bmm(ed, "edited")
  check(card["/bmm"]["edited.bmm"].sections[9] == nil and card["/bmm"]["edited.bmm"].sections[2] == SHEET,
        "write_bmm: a skeleton for another number of vertices is left out, the sheet stays")
  -- a model of no sheet, no skeleton
  local plain = tri_item("plain")
  check(T.write_bmm(plain, "plain") == true and next(card["/bmm"]["plain.bmm"].sections, next(card["/bmm"]["plain.bmm"].sections)) == nil
        and card["/bmm"]["plain.bmm"].name == "plain", "write_bmm: MESH only, the item's name shown")
  -- limits and empty models
  local huge = { name = "huge", v = {}, f = {{ 1, 1, 1, 0 }} }
  for i = 1, 65536 do huge.v[i] = { 0, 0, 0 } end
  local o, m = T.write_bmm(huge, "huge")
  check(o == nil and m:find("65536 vertices") and m:find("65535"), "write_bmm: over 65535 vertices: the format limit")
  local manyf = { name = "manyf", v = { { 0, 0, 0 } }, f = {} }
  for i = 1, 65536 do manyf.f[i] = { 1, 1, 1, 0 } end
  o, m = T.write_bmm(manyf, "manyf")
  check(o == nil and m:find("65536 triangles"), "write_bmm: over 65535 triangles: the format limit")
  o, m = T.write_bmm({ name = "e", v = {}, f = {} }, "e")
  check(o == nil and m == "the model is empty", "write_bmm: an empty model")
  check(card["/bmm"]["huge.bmm"] == nil and card["/bmm"]["manyf.bmm"] == nil and card["/bmm"]["e.bmm"] == nil, "write_bmm: nothing written for those")
end

----------------------------------------------------------------- .glb to .bmm

do
  local before = card["/glb"]["Robot.GLB"]
  writes = {}
  local ok, path, item = T.import_glb_to_bmm("robot")
  check(ok == true and path == "/bmm/robot.bmm" and item and item.from == "glb", "import_glb_to_bmm: " .. tostring(path))
  local f = card["/bmm"]["robot.bmm"]
  check(f and f.sections[2] == nil and f.sections[9] == nil and f.name == "robot", "import_glb_to_bmm: flat colours, no sheet")
  local back = T.import_model("robot")
  check(back and back.from == "bmm" and #back.v == 4 and #back.f == 2, "import_glb_to_bmm: the .bmm is listed and opens")
  local by = {}
  for _, e in ipairs(T.model_files()) do by[e.name] = e end
  check(by.robot.kind == "bmm" and by.robot.glb == true, "import_glb_to_bmm: robot is now in both")
  local o, m = T.import_glb_to_bmm("robot")
  check(o == nil and m:find("is there already"), "import_glb_to_bmm: no overwrite")
  glb_calls = {}
  o, m = T.import_glb_to_bmm("robot")
  check(#glb_calls == 0, "import_glb_to_bmm: no .glb read when the .bmm is there")
  o = T.import_glb_to_bmm("robot", { replace = true, texture = true, sheet = 32 })
  local tf = card["/bmm"]["robot.bmm"]
  check(o == true and tf.sections[2] and #tf.sections[2] == 4 + 32 * 32 * 4, "import_glb_to_bmm: replace, with the texture as the sheet")
  o, m = T.import_glb_to_bmm("long model name")
  check(o == nil and m:find("opts.to"), "import_glb_to_bmm: a long name asks for opts.to")
  o, path = T.import_glb_to_bmm("long model name", { to = "longname" })
  check(o == true and path == "/bmm/longname.bmm", "import_glb_to_bmm: opts.to")
  o, m = T.import_glb_to_bmm("nothing")
  check(o == nil and m == "no nothing.glb in /bmm or /glb" or (o == nil and m:find("nothing.glb")), "import_glb_to_bmm: no such .glb")
  o, m = T.import_glb_to_bmm("zed")
  check(o == nil and m == "no zed.glb in /glb", "import_glb_to_bmm: a name with no .glb")
  o, m = T.import_glb_to_bmm("bad", { to = "badnew" })
  check(o == nil and m:find("not a .glb file") and card["/bmm"]["badnew.bmm"] == nil, "import_glb_to_bmm: the message of glb3d, nothing written")
  local only_bmm = true
  for _, p in ipairs(writes) do only_bmm = only_bmm and p:match("^/bmm/") ~= nil end
  check(only_bmm and card["/glb"]["Robot.GLB"] == before, "import_glb_to_bmm: only /bmm is written, the .glb is as it was")
  local n = 0
  for _ in pairs(card["/glb"]) do n = n + 1 end
  check(n == 7, "import_glb_to_bmm: no file added to /glb")
end

-- animations from another model (APP-27): T.retarget on the humanoid walk, onto a copy with other names
do
  local hum = G.humanoid("man")
  local src = T.decode_rig(hum.ac)
  local function pose(a)                -- the walk: the legs and arms swing, the pelvis moves
    local p = {}
    for i = 1, #src.bones do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
    local function swing(name, deg)
      for i, b in ipairs(src.bones) do
        if b.name == name then p[i].q = { math.sin(math.rad(deg) / 2), 0, 0, math.cos(math.rad(deg) / 2) } end
      end
    end
    swing("uleg.L", a); swing("uleg.R", -a); swing("uarm.L", -a); swing("uarm.R", a)
    p[1].t = { 0, 0.1, 0.2 }
    return p
  end
  src.clips = { { name = "walk", mode = 1, loop = true, length = 0.75,
                  keys = { { t = 0, pose = pose(30) }, { t = 0.375, pose = pose(-30) }, { t = 0.75, pose = pose(30) } } },
                { name = "stand", mode = 2, loop = false, length = 1, keys = { { t = 0, pose = pose(0) } } } }
  -- the copy: names with _ and lower case, twice as tall, one bone with a name of its own
  local dst = { bones = {}, clips = {} }
  local renamed = { ["uarm.L"] = "uarm_l", ["larm.L"] = "larm_l", ["hand.L"] = "hand_l", ["uarm.R"] = "UArm-R",
                    ["uleg.L"] = "uleg_l", ["uleg.R"] = "uleg_r", ["foot.R"] = "toe" }
  for i, b in ipairs(src.bones) do
    local h, t = {}, {}
    for k = 1, 3 do h[k], t[k] = b.head[k] * 2, b.tail[k] * 2 end
    dst.bones[i] = { name = renamed[b.name] or b.name, parent = b.parent, head = h, tail = t }
  end
  check(math.abs(T.rig_height(src) - 1.825) < 1e-6 and math.abs(T.rig_height(dst) - 3.65) < 1e-6,
        "retarget: the height of a rig is the y span of heads and tails")
  local clips, info = T.retarget(src, dst)
  check(#clips == 2 and clips[1].name == "walk" and clips[1].length == 0.75 and clips[1].loop == true and clips[1].mode == 1,
        "retarget: the clips come with name, length, loop and mode")
  check(clips[2].loop == false and clips[2].mode == 2 and #clips[1].keys == 3 and clips[1].keys[2].t == 0.375,
        "retarget: the same keys, the same times")
  check(info.matched == 15 and #info.missing == 1 and info.missing[1] == "toe", "retarget: 15 bones match, 'toe' is missing")
  local function idx(rig, n) for i, b in ipairs(rig.bones) do if b.name == n then return i end end end
  local k1 = clips[1].keys[1].pose
  local ul = k1[idx(dst, "uleg_l")].q
  check(math.abs(ul[1] - math.sin(math.rad(15))) < 1e-6, "retarget: uleg.L -> uleg_l (the rotation is copied)")
  local ua = k1[idx(dst, "UArm-R")].q
  check(math.abs(ua[1] - math.sin(math.rad(15))) < 1e-6, "retarget: uarm.R -> UArm-R (lower case, no . _ -)")
  local ft = k1[idx(dst, "toe")]
  check(ft.q[4] == 1 and ft.q[1] == 0 and ft.t[1] == 0 and ft.t[2] == 0, "retarget: a bone with no match gets the rest pose")
  local pt = k1[1].t
  check(math.abs(pt[2] - 0.2) < 1e-6 and math.abs(pt[3] - 0.4) < 1e-6, "retarget: the translation is scaled by the ratio of the heights (2)")
  check(src.clips[1].keys[1].pose[1].t[2] == 0.1 and clips[1].keys[1].pose ~= src.clips[1].keys[1].pose, "retarget: the source is not changed")
  local back = T.retarget({ bones = dst.bones, clips = clips }, src)
  local _, i2 = T.retarget({ bones = dst.bones, clips = clips }, src)
  check(#back == 2 and i2.matched == 15 and i2.missing[1] == "foot.R", "retarget: the way back, the missing list follows the target")
  local c3, i3 = T.retarget({ bones = { { name = "x", parent = 0, head = { 0, 0, 0 }, tail = { 0, 1, 0 } } }, clips = {} }, dst)
  check(#c3 == 0 and i3.matched == 0 and #i3.missing == 16, "retarget: no clips, no match")
end

-- two-bone IK (APP-29): T.ik2, T.fk, T.ik_pole, T.ik_chain, pure
do
  local V, Q = T.V, T.Q
  local function len(a) return math.sqrt(V.dot(a, a)) end
  local function near(a, b, e) return len(V.sub(a, b)) < (e or 1e-4) end
  local root, l1, l2 = { 0.3, 1.2, -0.1 }, 0.5, 0.4
  local pole = { 0, 0, 1 }
  -- reachable targets in every direction: the end gets there, the lengths stay, the joint leans to the pole
  local seed, worst, lenbad, leanbad, qbad = 7, 0, 0, 0, 0
  local function rnd() seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648 end
  for _ = 1, 200 do
    local dir = V.norm({ rnd() - 0.5, rnd() - 0.5, rnd() - 0.5 })
    local d = 0.1 + rnd() * 0.8                      -- |l1 - l2| = 0.1 .. l1 + l2 = 0.9
    local tg = V.add(root, V.scale(dir, d))
    local r = T.ik2(root, l1, l2, tg, pole)
    worst = math.max(worst, len(V.sub(r.tip, tg)))
    lenbad = math.max(lenbad, math.abs(len(V.sub(r.mid, root)) - l1), math.abs(len(V.sub(r.tip, r.mid)) - l2))
    if not r.reached then worst = 1 end
    local m = V.sub(r.mid, root)
    local across = V.sub(m, V.scale(dir, V.dot(m, dir)))
    local pa = V.sub(pole, V.scale(dir, V.dot(pole, dir)))
    if V.dot(across, pa) < -1e-9 then leanbad = leanbad + 1 end
    local a1, a2 = Q.rotate(r.q1, { 0, 1, 0 }), Q.rotate(r.q2, { 0, 1, 0 })
    qbad = math.max(qbad, len(V.sub(a1, r.d1)), len(V.sub(a2, r.d2)))
  end
  check(worst < 1e-4, "ik2: 200 reachable targets are reached within 1e-4 (worst " .. worst .. ")")
  check(lenbad < 1e-9, "ik2: the two lengths stay (worst " .. lenbad .. ")")
  check(leanbad == 0, "ik2: the joint leans to the pole side")
  check(qbad < 1e-9, "ik2: q1 and q2 turn +y onto the two directions")
  -- out of reach: straight to the target, lengths kept, reached = false
  local far = V.add(root, { 2, 0, 0 })
  local r = T.ik2(root, l1, l2, far, pole)
  check(not r.reached and near(r.tip, { root[1] + 0.9, root[2], root[3] }, 1e-9) and near(r.mid, { root[1] + 0.5, root[2], root[3] }, 1e-9)
        and near(r.d1, { 1, 0, 0 }, 1e-9) and near(r.d2, { 1, 0, 0 }, 1e-9), "ik2: out of reach: straight to the target, no stretch")
  -- fully stretched (on the limit) and just inside / outside
  for _, dd in ipairs({ 0.9, 0.9 - 1e-9, 0.9 + 1e-9, 0.9 - 1e-6 }) do
    local rr = T.ik2(root, l1, l2, V.add(root, { 0, dd, 0 }), pole)
    check(near(rr.tip, V.add(root, { 0, math.min(dd, 0.9), 0 }), 1e-6) and math.abs(len(V.sub(rr.mid, root)) - l1) < 1e-9
          and rr.d1[1] == rr.d1[1] and rr.q1[4] == rr.q1[4], "ik2: fully stretched, d = " .. dd .. ": stable")
  end
  -- folded: nearer than |l1 - l2|, and on the root itself
  local rf = T.ik2(root, l1, l2, V.add(root, { 0, 0.05, 0 }), pole)
  check(not rf.reached and near(rf.tip, V.add(root, { 0, 0.1, 0 }), 1e-6) and math.abs(len(V.sub(rf.tip, rf.mid)) - l2) < 1e-9,
        "ik2: nearer than |len1 - len2|: folded flat, lengths kept")
  local r0 = T.ik2(root, l1, l2, root, pole)
  check(r0.d1[1] == r0.d1[1] and math.abs(len(r0.d1) - 1) < 1e-9 and math.abs(len(V.sub(r0.mid, root)) - l1) < 1e-9
        and math.abs(len(V.sub(r0.tip, r0.mid)) - l2) < 1e-6, "ik2: the target on the root: no NaN, lengths kept")
  local re = T.ik2(root, 0.5, 0.5, root, pole)
  check(re.d1[1] == re.d1[1] and near(re.tip, root, 1e-4) and math.abs(len(V.sub(re.mid, root)) - 0.5) < 1e-9,
        "ik2: equal lengths, folded to the root: the end comes back to the root")
  -- the pole flips the bend
  local tg = V.add(root, { 0.5, -0.3, 0 })
  local pa, pb = T.ik2(root, l1, l2, tg, { 0, 0, 1 }), T.ik2(root, l1, l2, tg, { 0, 0, -1 })
  check(near(pa.tip, tg) and near(pb.tip, tg) and pa.mid[3] > root[3] + 0.01 and pb.mid[3] < root[3] - 0.01
        and math.abs((pa.mid[3] - root[3]) + (pb.mid[3] - root[3])) < 1e-9, "ik2: the pole flips the bend, the end stays")
  local pp = T.ik2(root, l1, l2, V.add(root, { 0, -0.6, 0 }), { 0, 1, 0 })
  check(near(pp.tip, V.add(root, { 0, -0.6, 0 })) and pp.mid[1] == pp.mid[1] and len(V.sub(pp.mid, root)) > 0.49,
        "ik2: a pole exactly along the axis is fine")

  -- a rig: hips > thigh > shin > foot (bent knee at rest, toward +z), a second thigh
  local function bone(name, par, h, t) return { name = name, parent = par, head = h, tail = t } end
  local rig = { bones = {
    bone("hips", 0, { 0, 1, 0 }, { 0, 1.2, 0 }),
    bone("thigh", 1, { 0.1, 1, 0 }, { 0.1, 0.55, 0.06 }),
    bone("shin", 2, { 0.1, 0.55, 0.06 }, { 0.1, 0.1, 0 }),
    bone("foot", 3, { 0.1, 0.1, 0 }, { 0.1, 0.05, 0.2 }),
    bone("thigh2", 1, { -0.1, 1, 0 }, { -0.1, 0.5, 0 }),
  }, clips = {} }
  local function rest_pose()
    local p = {}
    for i = 1, #rig.bones do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
    return p
  end
  local rest = rest_pose()
  local pl = T.ik_pole(rig, 2, 3, 4, rest)
  check(pl[3] > 0.99 and math.abs(pl[1]) < 1e-9, "ik_pole: the rest bend of the leg is toward +z")
  local srig = { bones = { bone("a", 0, { 0, 1, 0 }, { 0, 0.5, 0 }), bone("b", 1, { 0, 0.5, 0 }, { 0, 0.25, 0 }),
                           bone("c", 2, { 0, 0, 0 }, { 0, -0.2, 0 }) } }
  check(T.ik_pole(srig, 1, 2, 3, nil)[3] == 1, "ik_pole: a straight rest limb with no pose: +z")
  -- fk: the rest pose is the rest heads; a turned bone moves its children only
  local fk = T.fk(rig, rest)
  check(near(fk.head[4], rig.bones[4].head, 1e-9) and near(fk.tail[4], rig.bones[4].tail, 1e-9), "fk: the rest pose is the rest")
  local turned = rest_pose()
  turned[2].q = Q.axis({ 1, 0, 0 }, math.rad(-30))
  local f2 = T.fk(rig, turned)
  check(near(f2.head[2], rig.bones[2].head, 1e-9) and not near(f2.head[3], rig.bones[3].head, 1e-3) and near(f2.head[5], rig.bones[5].head, 1e-9)
        and math.abs(len(V.sub(f2.head[3], f2.head[2])) - len(V.sub(rig.bones[3].head, rig.bones[2].head))) < 1e-9,
        "fk: a bone turns round its head and carries its children")
  -- ik_chain: reachable targets from a posed body (hips turned and moved, the foot turned)
  local body = rest_pose()
  body[1].q = Q.norm(Q.mul(Q.axis({ 0, 1, 0 }, math.rad(70)), Q.axis({ 1, 0, 0 }, math.rad(15))))
  body[1].t = { 0.05, -0.1, 0.02 }
  body[4].q = Q.axis({ 1, 0, 0 }, math.rad(20))
  local f0 = T.fk(rig, body)
  local ok_reach, ok_len, ok_foot, ok_other, ok_pose = true, true, true, true, true
  local seed2 = 3
  local function r2() seed2 = (seed2 * 1103515245 + 12345) % 2147483648; return seed2 / 2147483648 end
  local nreach = 0
  for _ = 1, 60 do
    local tg2 = V.add(f0.head[2], V.scale(V.norm({ r2() - 0.5, -r2() - 0.2, r2() - 0.5 }), 0.15 + r2() * 0.8))
    local np, info = T.ik_chain(rig, body, 2, 3, 4, tg2, pl)
    local fk3 = T.fk(rig, np)
    if info.reached then nreach = nreach + 1; ok_reach = ok_reach and near(fk3.head[4], tg2, 1e-4) end
    ok_len = ok_len and math.abs(len(V.sub(fk3.head[3], fk3.head[2])) - len(V.sub(f0.head[3], f0.head[2]))) < 1e-9
             and math.abs(len(V.sub(fk3.head[4], fk3.head[3])) - len(V.sub(f0.head[4], f0.head[3]))) < 1e-9
    local dq = Q.mul(fk3.rot[4], Q.conj(f0.rot[4]))
    ok_foot = ok_foot and math.abs(math.abs(dq[4]) - 1) < 1e-9          -- the foot keeps its turn in the world
    ok_other = ok_other and np[1].q[4] == body[1].q[4] and np[5].q[4] == 1 and np[1].t[2] == -0.1
    ok_pose = ok_pose and np ~= body and body[2].q[4] == 1
  end
  check(nreach > 20 and ok_reach, "ik_chain: a posed leg: the foot reaches every reachable target (" .. nreach .. " of 60)")
  check(ok_len, "ik_chain: the bone lengths stay")
  check(ok_foot, "ik_chain: the foot keeps its rotation in the world")
  check(ok_other and ok_pose, "ik_chain: the other bones are not touched, the pose given is not changed")
  -- out of reach: straight, pointing at the target
  local tg3 = V.add(f0.head[2], { 0.3, -3, 0 })
  local np, info = T.ik_chain(rig, body, 2, 3, 4, tg3, pl)
  local f3 = T.fk(rig, np)
  local ax = V.norm(V.sub(tg3, f0.head[2]))
  check(not info.reached and near(V.norm(V.sub(f3.head[3], f3.head[2])), ax, 1e-6) and near(V.norm(V.sub(f3.head[4], f3.head[3])), ax, 1e-6)
        and near(info.eff, f3.head[4], 1e-9), "ik_chain: out of reach: the leg is straight to the target")
  -- the pole turns with the hips: the knee leans where the body faces
  local tg4 = V.add(f0.head[2], V.scale(V.norm({ 0.2, -1, 0.1 }), 0.8))
  local f4 = T.fk(rig, (T.ik_chain(rig, body, 2, 3, 4, tg4, pl)))
  local axis4 = V.norm(V.sub(f4.head[4], f0.head[2]))
  local lean = V.sub(f4.head[3], f0.head[2])
  lean = V.sub(lean, V.scale(axis4, V.dot(lean, axis4)))
  check(V.dot(lean, Q.rotate(f0.rot[1], pl)) > 0.01, "ik_chain: the knee leans in the direction of the hips' turn")
end

-- the humanoid base (APP-29): a straight leg and a straight arm; the knee leans forward (-z, the toes), the elbow back
do
  local V = T.V
  local hum = G.humanoid("man")
  local hr = T.decode_rig(hum.ac)
  local function len(a) return math.sqrt(V.dot(a, a)) end
  local function pose0()
    local p = {}
    for i = 1, #hr.bones do p[i] = { q = { 0, 0, 0, 1 }, t = { 0, 0, 0 } } end
    return p
  end
  local function idx(n) for i, b in ipairs(hr.bones) do if b.name == n then return i end end end
  for _, c in ipairs({ { "uleg.L", "lleg.L", "foot.L", -1, "the leg" }, { "uarm.R", "larm.R", "hand.R", 1, "the arm" } }) do
    local g, p, b = idx(c[1]), idx(c[2]), idx(c[3])
    local pl = T.ik_pole(hr, g, p, b, pose0())
    check(pl[3] * c[4] > 0.99, "humanoid: " .. c[5] .. " leans " .. (c[4] < 0 and "forward (-z)" or "back (+z)") .. " when straight")
    local f0 = T.fk(hr, pose0())
    local worst, kneeok, nr = 0, true, 0
    -- 12 reachable targets: up, in, forward, back, sideways
    local dirs = { { 0, 1, 0 }, { 0, 0.7, -0.7 }, { 0, 0.7, 0.7 }, { 0.5, 0.8, 0 }, { -0.5, 0.8, 0 }, { 0, 0.5, -0.9 } }
    for _, dd in ipairs(dirs) do
      for _, amount in ipairs({ 0.15, 0.3 }) do
        local tg = V.add(f0.head[b], V.scale(V.norm(dd), amount))
        local np, info = T.ik_chain(hr, pose0(), g, p, b, tg, pl)
        local fk = T.fk(hr, np)
        if info.reached then
          nr = nr + 1
          worst = math.max(worst, len(V.sub(fk.head[b], tg)))
          local ax = V.norm(V.sub(fk.head[b], fk.head[g]))
          local m = V.sub(fk.head[p], fk.head[g])
          local across = V.sub(m, V.scale(ax, V.dot(m, ax)))
          if len(across) > 1e-3 and V.dot(across, pl) < 0 then kneeok = false end
        end
      end
    end
    check(nr >= 6 and worst < 1e-4, "humanoid: " .. c[5] .. ": " .. nr .. " reachable targets reached (worst " .. worst .. ")")
    check(kneeok, "humanoid: " .. c[5] .. ": the joint bends toward the pole")
    local far = V.add(f0.head[g], { 0, -3, 0 })
    local np, info = T.ik_chain(hr, pose0(), g, p, b, far, pl)
    local fk = T.fk(hr, np)
    check(not info.reached and len(V.sub(fk.head[b], f0.head[b])) < 1e-4, "humanoid: " .. c[5] .. ": the target at the full stretch: no stretch, no move")
  end
end

io.write(string.format("model files: %d/%d checks passed\n", checks - fails, checks))
os.exit(fails == 0 and 0 or 1)
