-- Part 5: the cartridge: ROM list, the frame loop, the screen, the save of the battery RAM.

local LINE_CYC = 190                            -- CPU cycles in a line (about 7 master clocks each)
local BTNS = { { "b", 15 }, { "y", 14 }, { "select", 13 }, { "start", 12 }, { "up", 11 }, { "down", 10 },
               { "left", 9 }, { "right", 8 }, { "a", 7 }, { "x", 6 }, { "l1", 5 }, { "r1", 4 } }

local function readpad(p)
  local v = 0
  for i = 1, 12 do
    local e = BTNS[i]
    if btn(e[1], p) then v = v | (1 << e[2]) end
  end
  if v & 0x0300 == 0x0300 then v = v & ~0x0300 end        -- left and right together: none
  if v & 0x0C00 == 0x0C00 then v = v & ~0x0C00 end
  return v
end

------------------------------------------------------------------ the ROM
local info = { title = "", mapping = "", size = 0 }

local function header_score(data, off, hi)
  if #data < off + 0x40 then return -1 end
  local function u8(i) return byte(data, off + i + 1) end
  local sc = 0
  local sum, comp = u8(0x1E) | (u8(0x1F) << 8), u8(0x1C) | (u8(0x1D) << 8)
  if (sum ~ comp) == 0xFFFF then sc = sc + 4 end
  local mm = u8(0x15)
  if mm & 0x20 ~= 0 then sc = sc + 1 end
  if ((mm & 1) == 1) == hi then sc = sc + 2 end
  if (u8(0x3C) | (u8(0x3D) << 8)) >= 0x8000 then sc = sc + 1 end
  local ok = true
  for i = 0, 20 do local c = u8(i); if c < 32 or c > 126 then ok = false end end
  if ok then sc = sc + 1 end
  return sc
end

local function load_rom(data, name)
  if #data % 1024 == 512 then data = data:sub(513) end             -- a copier header
  if #data < 0x8000 then return nil, "the file is too small for a SNES ROM" end
  local lo, hi = header_score(data, 0x7FC0, false), header_score(data, 0xFFC0, true)
  lorom = not (hi > lo)
  local off = lorom and 0x7FC0 or 0xFFC0
  rom, romsize = data, #data
  info.title = data:sub(off + 1, off + 21):gsub("[^%w _%-]", "?"):gsub("%s+$", "")
  info.mapping = lorom and "LoROM" or "HiROM"
  info.size = #data
  local rs = byte(data, off + 0x18 + 1) or 0
  sramsize = 0
  if rs > 0 and rs <= 7 then sramsize = 1024 << rs end
  sram = {}
  for i = 1, sramsize do sram[i] = 0 end
  sramdirty = false
  local base = name:gsub("%.[^.]*$", ""):sub(1, 8)
  info.srm = base .. ".srm"
  if sramsize > 0 then
    local s = doc_read(info.srm)
    if s then for i = 1, math.min(#s, sramsize) do sram[i] = byte(s, i) end end
  end
  for i = 1, 131072 do WRAM[i] = 0 end
  for i = 1, 65536 do VRAM[i] = 0 end
  for i = 1, 544 do OAM[i] = 0 end
  for i = 1, 512 do CG[i] = 0 end
  for i = 1, 256 do CG24[i] = 0 end
  for i = 1, 4096 do BLKV[i] = BLKV[i] + 1 end
  for i = 1, 16 do CGV[i] = CGV[i] + 1 end
  P.inidisp = 0x80; P.bgmode = 0; P.tm = 0; P.setini = 0; P.obsel = 0
  nmitimen, nmiflag, irqFlag, hdmaen = 0, 0, 0, 0
  vblank, hblank = false, false
  cyc = 0
  apu_reset()
  cpu_reset()
  cache_flush()
  return true
end

local function save_sram()
  if sramsize > 0 and sramdirty then
    local t, n = {}, 0
    for i = 1, sramsize, 4096 do
      n = n + 1
      t[n] = string.char(table.unpack(sram, i, math.min(i + 4095, sramsize)))
    end
    local ok = doc_write(info.srm, table.concat(t))
    if ok then sramdirty = false end
  end
end

------------------------------------------------------------------ one frame
local frames = 0
local function run_frame()
  ppu_frame_start()
  local lines = (P.setini & 4 ~= 0) and 239 or 224
  joy1, joy2 = readpad(1), readpad(2)
  local base = cyc
  for L = 0, 261 do
    if L == 0 then
      vblank = false; nmiflag = 0
      hdma_init()
    elseif L == lines then
      vblank = true; nmiflag = 1
      if nmitimen & 0x80 ~= 0 then nmiPending = true end
      joyshift1, joyshift2 = joy1, joy2
    end
    if L < lines then
      if hdmaen ~= 0 then hdma_line() end
      ppu_line(L)
    end
    local m = (nmitimen >> 4) & 3
    local ia
    if m == 1 or (m == 3 and L == vtime) then ia = base + (math.min(htime, 339) * LINE_CYC) // 341
    elseif m == 2 and L == vtime then ia = base end
    local hb = base + (LINE_CYC * 3) // 4
    if ia and ia < hb then cpu_run(ia); irqFlag = 1; ia = nil end
    cpu_run(hb)
    hblank = true
    if ia then cpu_run(ia); irqFlag = 1 end
    base = base + LINE_CYC
    cpu_run(base)
    hblank = false
  end
  cyc = cyc - 262 * LINE_CYC
  ppu_frame_end(lines)
  frames = frames + 1
end

------------------------------------------------------------------ the cartridge
local state = "browse"           -- browse, run, menu, viewer
local files, sel, msg = {}, 1, nil
local scr
local holdmenu = 0
local menusel = 1
local show_fps = false
local curname = ""

local function list_roms()
  files = {}
  local l, why = doc_list("sfc")
  if not l then msg = why or "the player did not allow the files"; return end
  for _, d in ipairs(l) do
    local ext = d.name:match("%.([^.]+)$")
    ext = ext and ext:lower()
    if ext == "sfc" or ext == "smc" or ext == "swc" or ext == "fig" then files[#files + 1] = d end
  end
  table.sort(files, function(a, b) return a.name < b.name end)
  if sel > #files then sel = math.max(1, #files) end
end

local function open_rom(d)
  local data, why = doc_read(d.name, "sfc")
  if not data then msg = why or "cannot read the file"; return end
  local ok, err = load_rom(data, d.name)
  if not ok then msg = err; return end
  curname = d.name
  msg = nil
  state = "run"
  frames = 0
end

function _init()
  scr = target_create(256, 240)
  frameskip(4)
  list_roms()
end

function _exit()
  save_sram()
  return true
end

function _update()
  if state == "browse" then
    if btnp("up") then sel = math.max(1, sel - 1) end
    if btnp("down") then sel = math.min(#files, sel + 1) end
    if btnp("x") then list_roms() end
    if btnp("a") and files[sel] then open_rom(files[sel]) end
  elseif state == "run" then
    if btn("l2") and btn("r2") then holdmenu = holdmenu + 1 else holdmenu = 0 end
    if holdmenu > 30 then state = "menu"; menusel = 1; holdmenu = 0; save_sram(); return end
    run_frame()
    if frames % 600 == 0 then save_sram() end
  elseif state == "menu" then
    if btnp("up") then menusel = math.max(1, menusel - 1) end
    if btnp("down") then menusel = math.min(5, menusel + 1) end
    if btnp("a") then
      if menusel == 1 then state = "run"
      elseif menusel == 2 then
        save_sram(); apu_reset(); cpu_reset(); nmitimen = 0; state = "run"
      elseif menusel == 3 then viewer_open(curname); state = "viewer"
      elseif menusel == 4 then show_fps = not show_fps
      else save_sram(); state = "browse"; list_roms() end
    end
    if btnp("b") then state = "run" end
  elseif state == "viewer" then
    if viewer_update() then state = "menu" end
  end
end

local function fit()
  local w, h = SCREEN_W, SCREEN_H
  local dh = h
  local dw = (h * 4) // 3
  if dw > w then dw = w; dh = (w * 3) // 4 end
  return (w - dw) // 2, (h - dh) // 2, dw, dh
end

function _draw()
  if state == "browse" then
    cls(0x101820)
    print("bm SNES", 16, 12, 0xFFD050, 2)
    print("Put .sfc or .smc files in the /sfc folder of the SD card.", 16, 56, 0x8090A0)
    if #files == 0 then
      print("No ROM found.", 16, 100, 0xFFFFFF)
    end
    local y = 90
    for i = 1, #files do
      local f = files[i]
      local c = (i == sel) and 0xFFFFFF or 0x90A0B0
      if i == sel then rectfill(10, y - 1, SCREEN_W - 20, 17, 0x304860) end
      print(f.name, 16, y, c)
      print(string.format("%d KB", f.size // 1024), SCREEN_W - 120, y, c)
      y = y + 18
      if y > SCREEN_H - 40 then break end
    end
    if msg then print(msg, 16, SCREEN_H - 56, 0xFF6060) end
    print("A play   X refresh", 16, SCREEN_H - 24, 0x8090A0)
    return
  end
  if state == "viewer" then viewer_draw(); return end
  local lines = (P.setini & 4 ~= 0) and 239 or 224
  ppu_render(scr, lines)
  cls(0)
  local x, y, w, h = fit()
  target_blit(scr, x, y, w, h, 0, 0, 256, lines)
  if show_fps then print(string.format("%d fps", stat(2)), 4, 4, 0x00FF00) end
  if state == "menu" then
    rectfill(SCREEN_W // 2 - 110, SCREEN_H // 2 - 84, 220, 168, 0x101820)
    rect(SCREEN_W // 2 - 110, SCREEN_H // 2 - 84, 220, 168, 0xFFD050)
    local items = { "Resume", "Reset", "Sprite viewer", show_fps and "FPS: on" or "FPS: off", "Back to the list" }
    for i = 1, 5 do
      print(items[i], SCREEN_W // 2 - 90, SCREEN_H // 2 - 72 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x8090A0)
    end
    print(info.title, SCREEN_W // 2 - 90, SCREEN_H // 2 + 60, 0x8090A0)
  end
end
