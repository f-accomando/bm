do
  -- The option rows, in the order of the design (section 2.3). A row has a name,
  -- labels, the index in force and a function that applies it. The rows 4 to 8
  -- call gpu3d once per change and show what the console really gave.
  local function gpu_state()
    local on, aa, vs, ver, q, imp, later = gpu3d()
    if vs == true then vs = 2 end
    return on, aa, vs, ver, q, imp, later
  end
  H.gpu_state = gpu_state

  local function choice(name, labels, init, apply, actual)
    local r = { name = name, labels = labels, i = init }
    function r.get()
      local s = labels[r.i]
      if actual then
        local a = actual()
        if a and a ~= s then s = s .. "->" .. a end
      end
      return s
    end
    function r.step(d)
      r.i = (r.i - 1 + d) % #labels + 1
      apply(r.i)
    end
    function r.toggle()
      r.i = r.i == 1 and #labels or 1
      apply(r.i)
    end
    return r
  end

  local function grey(name, why)
    local r = { name = name, grey = true, i = 1 }
    function r.get() return why end
    function r.step() end
    function r.toggle() end
    return r
  end

  local function lamps(k)
    lamp3d()
    local P = { { -3, 1.6, -3, 0xFFB060 }, { 3, 1.6, 3, 0xFFB060 }, { 3, 1.6, -3, 0x60B0FF }, { -3, 1.6, 3, 0x60B0FF } }
    for i = 1, k do
      local p = P[i]
      lamp3d(i, CX + p[1], p[2], CZ + p[3], 7, 1.4, p[4])
    end
  end

  local RES = { "320x180", "384x216", "480x270", "640x360", "960x540", "1280x720" }
  local function find(list, v, default)
    for i, x in ipairs(list) do if x == v then return i end end
    return default
  end

  -- the heroes row: the count is the value
  local heroes = { name = "Heroes" }
  function heroes.get() return tostring(H.n) end
  function heroes.step(d) H.n = math.max(0, math.min(H.maxh, H.n + d)) end
  function heroes.toggle() H.n = H.n == 0 and 20 or 0 end

  local lod = choice("LOD", { "0", "1", "2", "3", "auto" }, 5, function(i) H.lod = i end)

  local backend = choice("Backend", { "ARM", "GPU" }, 1, function(i) gpu3d(i == 2) end,
    function() return (gpu_state()) and "GPU" or "ARM" end)
  local aa = choice("AA", { "off", "4x" }, 1, function(i) gpu3d(nil, i == 2) end,
    function() local _, a = gpu_state(); return a and "4x" or "off" end)
  local vs = choice("Vertex shader", { "off", "1", "2" }, 1, function(i) gpu3d(nil, nil, i == 1 and false or i - 1) end,
    function() local _, _, v = gpu_state(); return v and tostring(v) or "off" end)
  local queue = choice("Queue", { "0", "1", "2" }, 1, function(i) gpu3d(nil, nil, nil, i == 1 and false or i - 1) end,
    function() local _, _, _, _, q = gpu_state(); return q and tostring(q) or "0" end)
  local later = choice("Frame later", { "off", "on" }, 1, function(i) gpu3d(nil, nil, nil, nil, nil, i == 2) end,
    function() local _, _, _, _, _, _, l = gpu_state(); return l and "on" or "off" end)
  local IMP = { "off", "8", "16", "32" }
  local imp = choice("Impostor px", IMP, 1, function(i) gpu3d(nil, nil, nil, nil, i == 1 and 0 or tonumber(IMP[i])) end,
    function() local _, _, _, _, _, p = gpu_state(); return p and p > 0 and tostring(p) or "off" end)

  local tex = choice("Texture", { "on", "flat colours" }, 1, function(i) textures3d(i == 1) end)
  local light = choice("Lighting", { "none", "flat", "smooth" }, 3, function() end)
  local zt = choice("Z test", { "on", "off" }, 1, function() end)
  local fog = choice("Fog", { "off", "on" }, 1, function(i)
    if i == 2 then fog3d(SKY, 14, 34) else fog3d() end
  end)
  local sky = choice("Sky and rim", { "off", "on" }, 1, function(i)
    if i == 2 then sky3d(0xFFF0D8, 0x8CB4FF, 0x504838); shine3d(0.6, 16, 0.5) else sky3d(); shine3d(0, 16, 0) end
  end)
  local lamp = choice("Lamps", { "0", "2", "4" }, 1, function(i) lamps((i - 1) * 2) end)
  local shadow = choice("Shadows", { "off", "on" }, 1, function() end)
  local res = choice("Resolution", RES, 4, function(i)
    local w, h = RES[i]:match("(%d+)x(%d+)")
    screen(tonumber(w), tonumber(h))
  end, function() return SCREEN_W .. "x" .. SCREEN_H end)
  local outline = choice("Outline hull", { "off", "on" }, 1, function() end)
  -- the ARM raster's dither (dither3d) and the cel light (cel3d, draw3d flag 256, heroes only)
  local dither = choice("Dither", { "on", "off" }, 1, function(i) if dither3d then dither3d(i == 1) end end)
  local CEL = { "off", "2", "3", "4", "5", "6", "8" }
  H.cel = 0
  local cel = choice("Cel bands", CEL, 1, function(i)
    H.cel = i == 1 and 0 or tonumber(CEL[i])
    if cel3d then cel3d(H.cel, 0.35) end
  end)

  ROWS = {
    heroes, lod, backend, aa, vs, queue, later, imp, tex, light, zt,
    dither, fog, sky, lamp, shadow,
    grey("Alpha", "n/a (needs a cape)"), res, outline, cel,
  }
  H.ROW = { heroes = 1, lod = 2, backend = 3, aa = 4, vs = 5, queue = 6, later = 7, imp = 8, tex = 9, light = 10,
            zt = 11, fog = 13, sky = 14, lamp = 15, shadow = 16, res = 18, outline = 19 }

  -- the index of each gpu row from what the console really gave (at the start)
  function H.options_sync()
    local on, a, v, _, q, p, l = gpu_state()
    backend.i = on and 2 or 1
    aa.i = a and 2 or 1
    vs.i = v and v + 1 or 1
    queue.i = q and (tonumber(q) or 1) + 1 or 1
    later.i = l and 2 or 1
    imp.i = find(IMP, p and p > 0 and tostring(p) or "off", 1)
    res.i = find(RES, SCREEN_W .. "x" .. SCREEN_H, 4)
  end

  -- what the draw reads
  function H.draw_flags()
    return ({ 2, 0, 4 })[light.i]
  end
  function H.draw_opts()
    return { cel = H.cel > 0 and cel3d ~= nil, noz = zt.i == 2, shadow = shadow.i == 2, outline = outline.i == 2 }
  end

  -- the options in force, for the printed line
  function H.opt_string()
    local on, a, v, _, q, p, l = gpu_state()
    return table.concat({
      on and "gpu" or "arm", "vs" .. (v or 0), "q" .. (q and (tonumber(q) or 1) or 0), "lat" .. (l and 1 or 0),
      "aa" .. (a and 1 or 0), "imp" .. (p and p > 0 and p or 0),
      tex.i == 1 and "tex" or "flat", ({ "unlit", "flat", "smooth" })[light.i], zt.i == 1 and "z" or "noz", dither.i == 1 and "dith" or "nodith",
      "fog" .. (fog.i - 1), "shd" .. (shadow.i - 1), "lamp" .. (lamp.i - 1) * 2, "sky" .. (sky.i - 1), "cel" .. H.cel,
      "out" .. (outline.i - 1) }, ",")
  end

  -- the sweep and the keys use these
  H.rows_obj = { heroes = heroes, lod = lod }

  function H.lod_name()
    return lod.labels[H.lod]
  end
end
