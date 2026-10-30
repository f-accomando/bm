do
  -- The panel and the keys (design sections 2.4 and 2.5). The panel is drawn
  -- after draw3d, on a plain dark rectangle (no alpha cost).
  local TEXT, CUR, DIM, NUM = 0xE0E0E0, 0xFFE060, 0x707880, 0x90E0A0
  local rep = {}

  -- true on the press, then again every 4 updates once the key is held longer than
  -- a serial key press can be: 10 polls (HOLD_FRAMES) of up to MAX_SKIP updates each
  -- (a slow machine runs more updates per poll, and a short press must not repeat)
  local REPEAT_AFTER = 10 * MAX_SKIP
  local function press(name)
    if btn(name) then
      local c = (rep[name] or 0) + 1
      rep[name] = c
      return c == 1 or (c > REPEAT_AFTER and c % 4 == 0)
    end
    rep[name] = 0
    return false
  end

  local keys = {}
  local function key_edge(u)
    local d = keydown(u)
    local e = d and not keys[u]
    keys[u] = d
    return e
  end

  local function set_heroes(d)
    ROWS[1].step(d)
  end

  -- The rows in groups (as Island Bench): a group line opens or closes with A, Left, Right or its digit key (1 to 4);
  -- H.cur is the line of the list shown (VIS), not the row number.
  local GROUPS = {
    { name = "Load", rows = { 1, 2 }, open = true },
    { name = "GPU and screen", rows = { 3, 4, 5, 6, 7, 8, 18, 23, 24 }, open = false },
    { name = "Light and look", rows = { 9, 25, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20 }, open = false },
    { name = "2D over the 3D", rows = { 21, 22 }, open = false },
  }
  local VIS = {}
  local function build_vis()
    local keep = VIS[H.cur]
    VIS = {}
    for gi, g in ipairs(GROUPS) do
      VIS[#VIS + 1] = { hdr = true, g = g, name = g.name }
      if g.open then
        for _, i in ipairs(g.rows) do VIS[#VIS + 1] = { row = ROWS[i], name = ROWS[i].name } end
      end
    end
    for i, v in ipairs(VIS) do
      if keep and v.name == keep.name then H.cur = i end
    end
    H.cur = math.max(1, math.min(H.cur, #VIS))
  end

  function H.input()
    local shift = keydown(0xE1) or keydown(0xE5)
    local start = btn("start")
    if #VIS == 0 then build_vis() end
    if press("up") then H.cur = (H.cur - 2) % #VIS + 1 end
    if press("down") then H.cur = H.cur % #VIS + 1 end
    local item = VIS[H.cur]
    if item.hdr then
      if press("left") or press("right") or (not start and btnp("a")) then item.g.open = not item.g.open; build_vis() end
    else
      local row = item.row
      if press("left") then row.step(-1) end
      if press("right") then row.step(1) end
      if not start and btnp("a") then row.toggle() end
    end
    -- the hero count: L1 / R1 by 1, L2 / R2 by 5, [ and ] (Shift: 5)
    if press("l1") then set_heroes(-1) end
    if press("r1") then set_heroes(1) end
    if press("l2") then set_heroes(-5) end
    if press("r2") then set_heroes(5) end
    if key_edge(0x2F) then set_heroes(shift and -5 or -1) end
    if key_edge(0x30) then set_heroes(shift and 5 or 1) end
    if key_edge(0x17) then H.n = 50; H.lod = 1; ROWS[2].i = 1 end
    -- the polygon level: X more detail, Y less (, and . on the keyboard)
    if btnp("x") or key_edge(0x36) then ROWS[2].step(-1) end
    if btnp("y") or key_edge(0x37) then ROWS[2].step(1) end
    if btnp("select") then H.cam = H.cam % 4 + 1 end
    if btnp("b") then H.panel = not H.panel end
    if key_edge(0x0B) then H.panel = not H.panel end
    for k = 1, #GROUPS do                             -- the digits open and close the groups
      if key_edge(0x1D + k) then GROUPS[k].open = not GROUPS[k].open; build_vis() end
    end
    -- Start + A: the sweep. Start alone, on release: save.
    if start then
      H.start_held = true
      if btnp("a") then H.sweep_start(); H.start_used = true end
    elseif H.start_held then
      H.start_held = false
      if not H.start_used then H.save() end
      H.start_used = false
    end
    if key_edge(0x3E) then H.sweep_start() end         -- F5
    if key_edge(0x3F) then H.save() end                -- F6
  end

  function H.panel_draw()
    if not H.panel then return end
    local W, Hh = SCREEN_W, SCREEN_H
    font("6x12")
    local L = H.last
    local on, aa, vs, ver, _, _, _, probe = H.gpu_state()
    if #VIS == 0 then build_vis() end
    local item = VIS[H.cur]
    if Hh >= 270 then
      local help = Hh >= 330 and not item.hdr and H.HELP[item.name]       -- two lines under the panel, if the screen has room
      local vis = math.min(#VIS, 20)                  -- the lines shown: a window that follows the cursor
      if H.cur < H.top then H.top = H.cur elseif H.cur >= H.top + vis then H.top = H.cur - vis + 1 end
      if H.top > #VIS - vis + 1 then H.top = math.max(1, #VIS - vis + 1) end
      rectfill(0, 0, 264, 12 * (vis + (help and 4 or 2)) + 6, 0x101820)
      print("HERO STRESS  " .. (on and "gpu" or "arm") .. " " .. H.CAMS[H.cam], 4, 4, TEXT)
      for k = 0, vis - 1 do
        local i, v = H.top + k, VIS[H.top + k]
        local y = 4 + 12 * (k + 1)
        if v.hdr then
          print((i == H.cur and ">" or " ") .. (v.g.open and "- " or "+ ") .. v.name:upper(), 4, y, i == H.cur and CUR or NUM)
        else
          local r = v.row
          local c = r.grey and DIM or (i == H.cur and CUR or TEXT)
          print((i == H.cur and ">" or " ") .. "  " .. string.format("%-14s", r.name) .. " " .. r.get(), 4, y, c)
        end
      end
      if help then
        print(help[1], 4, 4 + 12 * (vis + 1), CUR)
        print(help[2], 4, 4 + 12 * (vis + 2), CUR)
      end
      print("GPU driver bm3d " .. tostring(ver) .. (on and ("  vs probe " .. probe) or "  (ARM: no probe)"), 4,
            4 + 12 * (vis + (help and 3 or 1)), DIM)
    else
      rectfill(0, 0, 160, 16, 0x101820)
      print(string.format("%d %s %s", H.cur, item.name, item.hdr and "(group)" or item.row.get()), 4, 3, CUR)
    end
    -- the bottom bar, one line across the screen: a bar (16.7 ms is a full bar, red above) and the numbers
    local d = H.drawn or { 0, 0, 0, 0 }
    local ht = 0                                  -- triangles of the heroes drawn: the count at each level x its model
    for k = 1, 4 do ht = ht + d[k] * H.lod_tris[k] end
    local by = Hh - 14
    rectfill(0, by, W, 14, 0x101820)
    local full = 60
    local w = math.min(full * 2, math.floor(L.ms / 16.7 * full))
    rectfill(4, by + 3, full, 8, 0x202830)
    rectfill(4, by + 3, math.min(w, full), 8, 0x40C060)
    if w > full then rectfill(4 + full, by + 3, math.min(w - full, 10), 8, 0xE04040) end
    local load = H.var2d and ("2d " .. H.n2d) or ("n " .. H.n .. " lod " .. H.lod_name())
    local text = string.format("ms %s/%s fps %d skip %d|gpu %s wait %s|tri %d vtx %d lua %dk|%s|%dx%d",
      fmt(L.ms), fmt(L.max), math.floor(L.fps), L.skip or 0, fmt(L.gpu), fmt(L.wait), math.floor(L.tri),
      math.floor(L.vtx), math.floor(L.lua / 1000), load, SCREEN_W, SCREEN_H)
    local note = H.suiting() and "SUITE" or (H.sweeping() and "SWEEP" or H.sweep_note)
    if not note and H.save_note and time() - (H.save_t or 0) < 3 then note = H.save_note end
    if note then text = note .. "|" .. text end
    print(text, 70, by + 1, NUM)
  end
end
