-- The model files of the console for the host tests of the editors (APP-9): /bmm and /glb as tables, and
-- stand-ins for ls, glb3d, res_read and res_write. The checks of the C code are in
-- tests/qemu_test.py (test_glb3d, test_res_write, test_mesh_import); here only what the editors ask.
--
--   local MF = dofile(ROOT .. "/tests/studio/model_card.lua")
--   MF.install(E)        -- in the env of a cart; E.ls is wrapped, the others are added
--   MF.card["/glb"]["robot.glb"] = "glTF"; MF.calls (glb3d paths), MF.writes (res_write paths)

local MF = { card = { ["/bmm"] = {}, ["/glb"] = {} }, calls = {}, writes = {} }

local function name16(s) return s .. string.rep("\0", 16 - #s) end

-- a MESH record: four vertices, `nf` triangles (1..4); the first one textured if `textured`
function MF.record(name, nf, scale, textured)
  scale = scale or 1
  local v = { { 0, 0, 0 }, { scale, 0, 0 }, { 0, scale, 0 }, { 0, 0, scale } }
  local tris = { { 0, 1, 2 }, { 0, 1, 3 }, { 0, 2, 3 }, { 1, 2, 3 } }
  local out = { name16(name), string.pack("<I2I2I4", 4, nf, 0) }
  for _, p in ipairs(v) do out[#out + 1] = string.pack("<fff", p[1], p[2], p[3]) end
  for i = 1, nf do
    local t = tris[i]
    if textured and i == 1 then
      out[#out + 1] = string.pack("<I2I2I2I2I4I2I2I2I2I2I2", t[1], t[2], t[3], 0, 0x80000000, 0, 0, 64, 0, 0, 64)
    else
      out[#out + 1] = string.pack("<I2I2I2I2I4I2I2I2I2I2I2", t[1], t[2], t[3], 0, 0x336600 + i * 16, 0, 0, 0, 0, 0, 0)
    end
  end
  return table.concat(out)
end

-- a skeleton chunk (one bone "root", no animation) for a model of four vertices
function MF.rig(name)
  return name16(name) .. string.pack("<I2I2I2I2", 1, 0, 4, 0) .. name16("root") ..
         string.pack("<i2I2ffffff", -1, 0, 0, 0, 0, 0, 1, 0) .. string.rep("\0", 4)
end

-- a .bmm (kind 1) with a one-model MESH section (and a sheet, a skeleton, if asked)
function MF.put_bmm(file, title, name, nf, opts)
  opts = opts or {}
  local s = { [8] = string.pack("<I2I2I4", 1, 64, 0) .. MF.record(name, nf, 1, opts.textured) }
  if opts.sheet then s[2] = opts.sheet end
  if opts.rig then s[9] = string.pack("<I2I2I4", 1, 0, 0) .. MF.rig(name) end
  MF.card["/bmm"][file] = { kind = 1, name = title, author = "tests", sections = s }
end

local function path_of(path)
  local dir, name = path:match("^(/[^/]+)/([^/]+)$")
  if not dir then return nil end
  for n in pairs(MF.card[dir] or {}) do
    if n:lower() == name:lower() then return dir, n end
  end
  return dir, nil
end

function MF.install(E)
  local ls = E.ls
  E.ls = function(dir)
    if dir == "/bmm" or dir == "/glb" then
      local out = {}
      for name, data in pairs(MF.card[dir]) do out[#out + 1] = { name = name, size = #data, dir = false } end
      return out
    end
    return ls and ls(dir) or {}
  end
  E.glb3d = function(path, o)
    MF.calls[#MF.calls + 1] = path
    local _, n = path_of(path)
    if not n then return nil, path .. ": no such file" end
    if n:lower() == "bad.glb" then return nil, path .. ": not a .glb file" end
    if n:lower() == "huge.glb" then return nil, path .. ": 70000 vertices: the format holds 65535" end
    local rec = MF.record(o.name, 3, 2)
    return { record = rec, flat = rec, nv = 4, nf = 3, textured = false, name = o.name }
  end
  E.res_write = function(path, kind, t)
    MF.writes[#MF.writes + 1] = path
    local name = path:match("^/bmm/([a-z0-9_%-]+)%.bmm$")
    if kind ~= 1 or not name or #name > 8 then return nil, path .. ": a model goes to /bmm/NAME.bmm" end
    local _, have = path_of(path)
    if have and not t.replace then return nil, path .. ": is there already (replace = true changes it)" end
    if have then MF.card["/bmm"][have] = nil end
    MF.card["/bmm"][name .. ".bmm"] = { kind = 1, name = t.name or name, author = t.author or "", sections = t.sections }
    return true
  end
  E.res_read = function(path)
    local dir, n = path_of(path)
    if not n then return nil, path .. ": no such file" end
    local f = MF.card[dir][n]
    if type(f) ~= "table" then return nil, path .. ": not a resource file" end
    return { kind = f.kind, name = f.name, author = f.author, sections = f.sections }
  end
end

return MF
