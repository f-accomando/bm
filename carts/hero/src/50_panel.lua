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

  function H.input()
    local shift = keydown(0xE1) or keydown(0xE5)
    local start = btn("start")
    if press("up") then H.cur = (H.cur - 2) % #ROWS + 1 end
    if press("down") then H.cur = H.cur % #ROWS + 1 end
    local row = ROWS[H.cur]
    if press("left") then row.step(-1) end
    if press("right") then row.step(1) end
    if not start and btnp("a") then row.toggle() end
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
    for k = 1, 10 do                                  -- the digits jump to the rows 1 to 10
      if key_edge(0x1D + k) and ROWS[k] then H.cur = k end
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
    -- the bar: 16.7 ms is a full bar, green below, red above (it decides nothing)
    local bx = W - 220
    local full = 200
    local w = math.min(full * 2, math.floor(L.ms / 16.7 * full))
    rectfill(bx, 4, full, 8, 0x202830)
    rectfill(bx, 4, math.min(w, full), 8, 0x40C060)
    if w > full then rectfill(bx + full, 4, math.min(w - full, 20), 8, 0xE04040) end
    local on, aa, vs, ver, _, _, _, probe = H.gpu_state()
    if Hh >= 270 then
      local help = Hh >= 330 and H.HELP[ROWS[H.cur].name]       -- two lines under the panel, if the screen has room
      local vis = math.min(#ROWS, 20)                 -- the rows shown: a window that follows the cursor
      if H.cur < H.top then H.top = H.cur elseif H.cur >= H.top + vis then H.top = H.cur - vis + 1 end
      rectfill(0, 0, 264, 12 * (vis + (help and 5 or 2)) + 6, 0x101820)
      print("HERO STRESS  " .. (on and "gpu" or "arm") .. " " .. H.CAMS[H.cam] .. "  " .. H.top .. "-" .. (H.top + vis - 1) .. "/" .. #ROWS, 4, 4, TEXT)
      for k = 0, vis - 1 do
        local i, r = H.top + k, ROWS[H.top + k]
        local y = 4 + 12 * (k + 1)
        local c = r.grey and DIM or (i == H.cur and CUR or TEXT)
        print((i == H.cur and ">" or " ") .. string.format("%-14s", r.name) .. " " .. r.get(), 4, y, c)
      end
      local drv = on and ("  vs probe " .. probe) or "  (ARM: no probe)"
      print("GPU driver bm3d " .. tostring(ver) .. drv, 4, 4 + 12 * (vis + 1), DIM)
      if help then
        print("(console options: Settings)", 4, 4 + 12 * (vis + 2), DIM)
        print(help[1], 4, 4 + 12 * (vis + 3), CUR)
        print(help[2], 4, 4 + 12 * (vis + 4), CUR)
      end
    else
      local r = ROWS[H.cur]
      rectfill(0, 0, 160, 16, 0x101820)
      print(string.format("%d %s %s", H.cur, r.name, r.get()), 4, 3, CUR)
    end
    -- the numbers, bottom right
    local d = H.drawn or { 0, 0, 0, 0 }
    local ht = 0                                  -- triangles of the heroes drawn: the count at each level x its model
    for k = 1, 4 do ht = ht + d[k] * H.lod_tris[k] end
    local lines = {
      string.format("ms %s  max %s  fps %d  skip %d", fmt(L.ms), fmt(L.max), math.floor(L.fps), L.skip or 0),
      string.format("gpu %s  wait %s  anim %s", fmt(L.gpu), fmt(L.wait), fmt(L.anim)),
      string.format("tri %d  vtx %d  lua %dk", math.floor(L.tri), math.floor(L.vtx), math.floor(L.lua / 1000)),
      string.format("n %d  lod %s (0:%d 1:%d 2:%d 3:%d)", H.n, H.lod_name(), d[1], d[2], d[3], d[4]),
      string.format("hero tris %d (n %d)", ht, H.n),
      string.format("%dx%d  map %s tri", SCREEN_W, SCREEN_H, H.map_tris and tostring(math.floor(H.map_tris)) or "-"),
    }
    local note = H.sweeping() and "SWEEP RUNNING" or H.sweep_note
    if not note and H.save_note and time() - (H.save_t or 0) < 3 then note = H.save_note end
    if note then lines[#lines + 1] = note end
    local y0 = Hh - 4 - 12 * #lines
    rectfill(W - 232, y0 - 3, 232, 12 * #lines + 6, 0x101820)
    for i, s in ipairs(lines) do print(s, W - 228, y0 + 12 * (i - 1), NUM) end
  end
end
