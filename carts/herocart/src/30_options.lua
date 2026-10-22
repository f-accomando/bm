do
  -- The option rows, in the order of the design (section 2.3). A row has a name,
  -- labels, the index in force and a function that applies it. The rows 4 to 8
  -- call gpu3d once per change and show what the console really gave.
  -- on, aa, vs (asked), version, queue, impostor, later, probe (the level the console's boot test saw)
  local function gpu_state()
    local on, aa, vs, ver, q, imp, later, probe = gpu3d()
    if vs == true then vs = 2 end
    return on, aa, vs, ver, q, imp, later, probe or 0
  end
  H.gpu_state = gpu_state

  local function choice(name, labels, init, apply, actual)
    local r = { name = name, labels = labels, i = init }
    function r.get()
      local s = labels[r.i]
      if actual then
        local a = actual()
        if a and a:sub(1, 1) == "~" then s = s .. " " .. a:sub(2)
        elseif a and a ~= s then s = s .. "->" .. a end
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

  -- screen() takes these; the label shows the mode really in force (SCREEN_W x SCREEN_H) after "->"
  local RES = { "320x180", "384x216", "480x270", "640x360", "960x540", "1280x720", "1920x1080" }
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
  -- a GPU-only row asked while the ARM draws: the console keeps the value but it does nothing
  local function ignored(r)
    local on = gpu_state()
    return not on and r.i > 1 and "~ignored (ARM)" or nil
  end
  local aa
  aa = choice("AA", { "off", "4x" }, 1, function(i) gpu3d(nil, i == 2) end,
    function() local on, a = gpu_state(); return ignored(aa) or (a and "4x" or "off") end)
  -- Vertex shader: asked (the label), applied = the least of it and the probe's level
  local vs
  vs = choice("Vertex shader", { "off", "1", "2" }, 1, function(i) gpu3d(nil, nil, i == 1 and false or i - 1) end,
    function()
      local on, _, v, _, _, _, _, probe = gpu_state()
      if not on then return ignored(vs) end
      local applied = math.min(v or 0, probe)
      if applied ~= (v or 0) then return "~-> " .. applied .. " (probe " .. probe .. ")" end
      return v and tostring(v) or "off"
    end)
  local queue
  queue = choice("Queue", { "0", "1", "2" }, 1, function(i) gpu3d(nil, nil, nil, i == 1 and false or i - 1) end,
    function() local _, _, _, _, q = gpu_state(); return ignored(queue) or (q and tostring(q) or "0") end)
  local later
  later = choice("Frame later", { "off", "on" }, 1, function(i) gpu3d(nil, nil, nil, nil, nil, i == 2) end,
    function() local _, _, _, _, _, _, l = gpu_state(); return ignored(later) or (l and "on" or "off") end)
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
  -- the outline: draw3d flag 512 (the inverted hull made by the console, cel3d's colour and width)
  -- or the baked hull models (stage 1), to compare their cost
  local outline, cel
  H.cel = 0
  local function apply_cel()
    cel3d(H.cel, 0.35, outline and outline.i == 3 and 0x14141E or nil, 0.02)
  end
  outline = choice("Outline", { "off", "hull (baked)", "flag 512" }, 1, apply_cel)
  -- the ARM raster's dither (dither3d) and the cel light (cel3d, draw3d flag 256, heroes only)
  local dither = choice("Dither", { "on", "off" }, 1, function(i) dither3d(i == 1) end)
  local CEL = { "off", "2", "3", "4", "5", "6", "8" }
  cel = choice("Cel bands", CEL, 1, function(i)
    H.cel = i == 1 and 0 or tonumber(CEL[i])
    apply_cel()
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
    return { cel = H.cel > 0, noz = zt.i == 2, shadow = shadow.i == 2, hull = outline.i == 2, flag512 = outline.i == 3 }
  end

  -- the options in force, for the printed line
  function H.opt_string()
    local on, a, v, _, q, p, l, probe = gpu_state()
    return table.concat({
      on and "gpu" or "arm", "vs" .. (v or 0) .. "p" .. probe, "q" .. (q and (tonumber(q) or 1) or 0), "lat" .. (l and 1 or 0),
      "aa" .. (a and 1 or 0), "imp" .. (p and p > 0 and p or 0),
      tex.i == 1 and "tex" or "flat", ({ "unlit", "flat", "smooth" })[light.i], zt.i == 1 and "z" or "noz", dither.i == 1 and "dith" or "nodith",
      "fog" .. (fog.i - 1), "shd" .. (shadow.i - 1), "lamp" .. (lamp.i - 1) * 2, "sky" .. (sky.i - 1), "cel" .. H.cel,
      "out" .. (outline.i - 1) }, ",")
  end

  -- Help under the panel for the row the cursor is on: what the option should do and what to
  -- expect in the numbers. At most two lines of 42 characters. Edit the texts here.
  H.HELP = {
    ["Heroes"] = { "Heroes drawn (all 64 still move).", "tri rises with the count." },
    ["LOD"] = { "Polygon level: 0 full ... 3 fewest.", "tri falls 0 to 3; auto: by distance." },
    ["Backend"] = { "ARM or GPU draws the 3D.", "ARM ignores AA, VS, Queue, Frame later." },
    ["AA"] = { "4x MSAA, GPU only.", "gpu ms rises; tri stays." },
    ["Vertex shader"] = { "GPU places vertices: tri at 60 fps rises.", "Needs GPU. Shows asked->applied (probe)." },
    ["Queue"] = { "Timing only: ms and wait change.", "tri does not change." },
    ["Frame later"] = { "Each frame shown one frame later.", "Needs Queue 2. Timing only." },
    ["Impostor px"] = { "Far small heroes drawn as one quad.", "tri falls; picture changes a little." },
    ["Texture"] = { "Plaza texture on or flat colours.", "Heroes are untextured." },
    ["Lighting"] = { "none, flat or smooth (per vertex) light.", "Smooth is the normal look." },
    ["Z test"] = { "off: draw far to near, no depth test.", "Wrong pictures possible; ms changes." },
    ["Dither"] = { "Dither of the ARM raster, 5-6-5 colour.", "No change with Backend GPU." },
    ["Fog"] = { "Fog by distance over plaza and heroes.", "tri stays." },
    ["Sky and rim"] = { "Sky light, rim and highlight on heroes.", "tri stays; shader work rises." },
    ["Lamps"] = { "0, 2 or 4 point lights in the plaza.", "tri stays; light work rises." },
    ["Shadows"] = { "A flat shadow under each hero.", "One more draw a hero: gpu ms rises." },
    ["Alpha"] = { "Not in this cart (needs a cape model).", "" },
    ["Resolution"] = { "screen() mode. 1920x1080: use Backend GPU.", "Label shows the mode in force after ->." },
    ["Outline"] = { "Dark outline: baked hull model or flag 512.", "tri about doubles; compare the two." },
    ["Cel bands"] = { "Toon bands on the heroes (flag 256).", "VS 2 cuts them in the vertex program." },
  }

  -- the sweep and the keys use these
  H.rows_obj = { heroes = heroes, lod = lod }

  function H.lod_name()
    return lod.labels[H.lod]
  end
end
