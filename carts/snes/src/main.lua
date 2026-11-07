-- bm SNES: the user interface of the Super Nintendo emulator. The machine itself is in C (src/bm/snes_*.c); this
-- cartridge lists the ROMs of /sfc, loads one, passes the pads to it every frame, draws its picture, keeps the
-- battery RAM in /docs and has the menu. Original code.

local SW, SH = 256, 240

-- the SNES buttons as bits (snes.h): B Y Select Start Up Down Left Right A X L R
local BTNS = { { "b", 0x8000 }, { "y", 0x4000 }, { "select", 0x2000 }, { "start", 0x1000 }, { "up", 0x0800 },
               { "down", 0x0400 }, { "left", 0x0200 }, { "right", 0x0100 }, { "a", 0x0080 }, { "x", 0x0040 },
               { "l1", 0x0020 }, { "r1", 0x0010 } }
-- The SNES buttons sit as A right, B bottom, X top, Y left. On the RGB30 (and Nintendo pads) bm's names are the
-- same. On a keyboard or an Xbox pad bm's A is the bottom button and X the left one: swap them.
local SWAP = { a = "b", b = "a", x = "y", y = "x" }
local swap = false
local function pad_layout()
  local c = controller and controller(1)
  swap = not (c and c.layout == "nintendo")
end

local function readpad(p)
  local v = 0
  for i = 1, #BTNS do
    local e = BTNS[i]
    local name = swap and SWAP[e[1]] or e[1]
    if btn(name, p) then v = v | e[2] end
  end
  if v & 0x0300 == 0x0300 then v = v & ~0x0300 end          -- left and right together: none
  if v & 0x0C00 == 0x0C00 then v = v & ~0x0C00 end
  return v
end

local state = "browse"           -- browse, run, menu
local files, sel, msg = {}, 1, nil
local scr
local holdmenu, menusel = 0, 1
local show_fps = false
local frames = 0
local srm = ""

local function list_roms()
  files = {}
  msg = nil
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

local function save_sram()
  local s = snes_sram()
  if s and srm ~= "" then doc_write(srm, s) end
end

local function open_rom(d)
  local data, why = doc_read(d.name, "sfc")
  if not data then msg = why or "cannot read the file"; return end
  local ok, err = snes_load(data)
  if not ok then msg = err; return end
  local info = snes_info()
  local base = d.name:gsub("%.[^.]*$", ""):gsub("[^%w_%-]", ""):sub(1, 8)
  if base == "" then base = "rom" end
  srm = info.sram > 0 and (base .. ".srm") or ""
  if srm ~= "" then
    local s = doc_read(srm)
    if s then snes_sram(s) end
  end
  msg = nil
  state = "run"
  frames = 0
end

function _init()
  scr = target_create(SW, SH)
  pad_layout()
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
    if (btn("l2") and btn("r2")) or (btn("l1") and btn("r1")) then holdmenu = holdmenu + 1 else holdmenu = 0 end
    if holdmenu > 30 then state = "menu"; menusel = 1; holdmenu = 0; save_sram(); return end
    snes_frame(readpad(1), readpad(2))
    frames = frames + 1
    if frames % 120 == 0 then pad_layout() end
    if frames % 600 == 0 then save_sram() end
  elseif state == "menu" then
    if btnp("up") then menusel = math.max(1, menusel - 1) end
    if btnp("down") then menusel = math.min(4, menusel + 1) end
    if btnp("a") then
      if menusel == 1 then state = "run"
      elseif menusel == 2 then save_sram(); snes_reset(); state = "run"
      elseif menusel == 3 then show_fps = not show_fps
      else save_sram(); state = "browse"; list_roms() end
    end
    if btnp("b") then state = "run" end
  end
end

local function fit(lines)
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
    if #files == 0 then print("No ROM found.", 16, 100, 0xFFFFFF) end
    local y = 90
    for i = 1, #files do
      local f = files[i]
      local c = (i == sel) and 0xFFFFFF or 0x90A0B0
      if i == sel then rectfill(10, y - 1, SCREEN_W - 20, 17, 0x304860) end
      print(f.name:sub(1, 48), 16, y, c)
      print(string.format("%d KB", f.size // 1024), SCREEN_W - 120, y, c)
      y = y + 18
      if y > SCREEN_H - 40 then break end
    end
    if msg then print(msg, 16, SCREEN_H - 56, 0xFF6060) end
    print("A play   X refresh   in game: hold L1+R1 (keyboard: Q+E) for the menu", 16, SCREEN_H - 24, 0x8090A0)
    return
  end
  local lines = snes_draw(scr)
  cls(0)
  local x, y, w, h = fit(lines)
  target_blit(scr, x, y, w, h, 0, 0, SW, lines)
  local d = snes_debug()
  if d.inidisp & 0x80 ~= 0 and frames > 180 and state == "run" then      -- a game that keeps the screen off: say why
    local io = {}
    for i = 1, 6 do io[i] = string.format("%04X", d.io[i]) end
    print(string.format("screen off: PC %06X%s%s  nmi %02X  sound %s %04X", d.pc, d.wai and " WAI" or "",
      d.stp and " STP" or "", d.nmi, d.spc, d.spc_pc), 4, SCREEN_H - 32, 0xFF8040)
    print("reads: " .. table.concat(io, " ") .. "  " .. snes_info().mapping .. " " .. snes_info().title, 4,
      SCREEN_H - 16, 0xFF8040)
  end
  if show_fps then
    print(string.format("%d fps  blank %02X  tm %02X  sound %s %04X", stat(2), d.inidisp, d.tm, d.spc, d.spc_pc), 4, 4, 0x00FF00)
  end
  if state == "menu" then
    rectfill(SCREEN_W // 2 - 110, SCREEN_H // 2 - 70, 220, 140, 0x101820)
    rect(SCREEN_W // 2 - 110, SCREEN_H // 2 - 70, 220, 140, 0xFFD050)
    local items = { "Resume", "Reset", show_fps and "FPS: on" or "FPS: off", "Back to the list" }
    for i = 1, 4 do
      print(items[i], SCREEN_W // 2 - 90, SCREEN_H // 2 - 58 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x8090A0)
    end
    print(snes_info().title, SCREEN_W // 2 - 90, SCREEN_H // 2 + 46, 0x8090A0)
  end
end
