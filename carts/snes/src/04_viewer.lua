-- Part 4: the viewer of tiles (VRAM), sprites (OAM) and colours (CGRAM) of the game that runs,
-- with an export to PNG in /docs (the folder bm Pixel exports to). The emulation is paused.
-- The pictures are made in the top left of the sprite sheet and read back with sheet_png.

local viewer_open, viewer_update, viewer_draw
do
  local view, bpp, pal, row, page = 1, 4, 0, 0, 0       -- 1 tiles, 2 sprites, 3 colours
  local stale, note, notet, base = true, "", 0, "snes"
  local VIEWS = { "Tiles (VRAM)", "Sprites (OAM)", "Colours (CGRAM)" }
  local OBJSZ = {
    [0] = { 8, 8, 16, 16 }, { 8, 8, 32, 32 }, { 8, 8, 64, 64 }, { 16, 16, 32, 32 },
    { 16, 16, 64, 64 }, { 32, 32, 64, 64 }, { 16, 32, 32, 64 }, { 16, 32, 32, 32 },
  }

  local function palbase()
    if bpp == 8 then return 0 end
    if bpp == 4 then return (pal & 15) << 4 end
    return (pal & 63) << 2
  end
  local function palcount() return bpp == 8 and 1 or (bpp == 4 and 16 or 64) end
  local function totalrows() return 65536 // (bpp * 8) // 16 end

  -- 16 tiles a row, `nr` rows from row `r0`; the picture is at (0, 0) of the sheet
  local function compose_tiles(r0, nr)
    local tb, pb = bpp * 8, palbase()
    for r = 0, nr - 1 do
      for c = 0, 15 do
        ppu_decode((((r0 + r) * 16 + c) * tb) & 0xFFFF, bpp, pb, c * 8, r * 8)
      end
    end
    return 128, nr * 8
  end

  -- the sprite `i`, as stored (flips not applied), in a cell of 64 x 64 at (cx, cy)
  local function compose_sprite(i, cx, cy)
    local o = i * 4
    local hb = OAM[512 + (i >> 2) + 1] >> ((i & 3) * 2)
    local sz = OBJSZ[(P.obsel >> 5) & 7]
    local w, h = sz[1], sz[2]
    if hb & 2 ~= 0 then w, h = sz[3], sz[4] end
    local tile, at = OAM[o + 3], OAM[o + 4]
    local ob = P.obsel
    local tb = (ob & 7) << 14
    if at & 1 ~= 0 then tb = tb + ((((ob >> 3) & 3) + 1) << 13) end
    local pb = 128 + (((at >> 1) & 7) << 4)
    for y = 0, 63 do                                  -- clear what the sprite does not cover
      for x = 0, 63 do
        if x >= w or y >= h then sset(cx + x, cy + y, 0, 0) end
      end
    end
    for ty = 0, (h >> 3) - 1 do
      for tx = 0, (w >> 3) - 1 do
        local t = ((tile + (ty << 4)) & 0xF0) | ((tile + tx) & 0x0F)
        ppu_decode((tb + t * 32) & 0xFFFF, 4, pb, cx + tx * 8, cy + ty * 8)
      end
    end
  end

  local function compose_sprites(first, count, cols)
    for k = 0, count - 1 do
      compose_sprite(first + k, (k % cols) * 64, (k // cols) * 64)
    end
    return cols * 64, ((count + cols - 1) // cols) * 64
  end

  local function compose_colours()
    for i = 0, 255 do sset(i % 16, i // 16, CG24[i + 1], 255) end
    return 16, 16
  end

  local function free_name(kind)
    local used = {}
    for _, d in ipairs(doc_list() or {}) do used[d.name:lower()] = true end
    for n = 0, 99 do
      local nm = string.format("%s%s%02d.png", base, kind, n)
      if not used[nm] then return nm end
    end
  end

  local function say(t) note = t; notet = 240 end

  local function export(kind, w, h)
    local bytes, why = sheet_png(0, 0, w, h)
    if not bytes then say("export failed: " .. tostring(why)); return end
    local name = free_name(kind)
    if not name then say("export: the names " .. base .. kind .. "00-99 are used"); return end
    local ok, err = doc_write(name, bytes)
    if ok then say("saved /docs/" .. name .. string.format("  (%dx%d)", w, h))
    else say("export failed: " .. tostring(err)) end
  end

  viewer_open = function(romname)
    base = (romname:gsub("%.[^.]*$", ""):gsub("[^%w]", ""):lower():sub(1, 4))
    if base == "" then base = "snes" end
    cache_flush()
    stale = true; note = ""
    row = math.min(row, math.max(0, totalrows() - 16))
  end

  -- returns true when the player leaves the viewer
  viewer_update = function()
    if btnp("b") then cache_flush(); return true end
    if btnp("l1") then view = (view - 2) % 3 + 1; stale = true end
    if btnp("r1") then view = view % 3 + 1; stale = true end
    if view == 1 then
      local maxrow = math.max(0, totalrows() - 16)
      if btnp("up") then row = math.max(0, row - 1); stale = true end
      if btnp("down") then row = math.min(maxrow, row + 1); stale = true end
      if btnp("l2") then row = math.max(0, row - 16); stale = true end
      if btnp("r2") then row = math.min(maxrow, row + 16); stale = true end
      if btnp("left") then pal = (pal - 1) % palcount(); stale = true end
      if btnp("right") then pal = (pal + 1) % palcount(); stale = true end
      if btnp("y") then
        bpp = (bpp == 2) and 4 or ((bpp == 4) and 8 or 2)
        pal = 0; row = math.min(row, math.max(0, totalrows() - 16)); stale = true
      end
    elseif view == 2 then
      if btnp("up") or btnp("left") then page = (page - 1) % 4; stale = true end
      if btnp("down") or btnp("right") then page = (page + 1) % 4; stale = true end
    end
    if btnp("a") then                                  -- export what is on the screen
      if view == 1 then export("t", compose_tiles(row, 16))
      elseif view == 2 then export("s", compose_sprites(page * 32, 32, 8))
      else export("p", compose_colours()) end
      stale = true
    elseif btnp("x") then                              -- export all of it
      if view == 1 then export("v", compose_tiles(0, totalrows()))
      elseif view == 2 then export("o", compose_sprites(0, 128, 16))
      else export("p", compose_colours()) end
      stale = true
    end
    if notet > 0 then notet = notet - 1 end
    return false
  end

  local sw, sh = 0, 0
  viewer_draw = function()
    cls(0x101820)
    print("bm SNES viewer: " .. VIEWS[view], 8, 6, 0xFFD050)
    if stale then
      if view == 1 then sw, sh = compose_tiles(row, 16)
      elseif view == 2 then sw, sh = compose_sprites(page * 32, 32, 8)
      else sw, sh = compose_colours() end
      stale = false
    end
    local z = 1
    if view == 1 then z = 2 elseif view == 2 then z = (SCREEN_W >= 560) and 1 or 0.5 elseif view == 3 then z = 12 end
    local dx, dy = 8, 30
    rectfill(dx, dy, sw * z, sh * z, 0x303840)
    sspr(0, 0, sw, sh, dx, dy, false, false, z)
    local info
    if view == 1 then
      info = string.format("%d bpp  palette %d  row %d/%d  tile %d..%d", bpp, pal, row, totalrows(),
        row * 16, row * 16 + 255)
      local px = dx + sw * z + 12
      for i = 0, math.min(15, (bpp == 8 and 256 or (1 << bpp)) - 1) do
        rectfill(px + (i % 4) * 18, dy + (i // 4) * 18, 16, 16, CG24[palbase() + i + 1])
      end
    elseif view == 2 then
      info = string.format("sprites %d..%d, 8 a row, as stored (no flips)", page * 32, page * 32 + 31)
    else
      info = "256 colours, 16 a row; sprite colours are 128-255"
    end
    print(info, 8, SCREEN_H - 60, 0xA0B0C0)
    print("L1/R1 view  B back  A export view  X export all", 8, SCREEN_H - 40, 0x708090)
    if view == 1 then print("arrows move/palette  L2/R2 page  Y bpp", 8, SCREEN_H - 22, 0x708090)
    elseif view == 2 then print("arrows page", 8, SCREEN_H - 22, 0x708090) end
    if notet > 0 then print(note, 8, SCREEN_H - 78, 0x80FF80) end
  end
end
