-- bm SNES: the user interface of the Super Nintendo emulator. The machine itself is in C (src/bm/snes_*.c); this
-- cartridge lists the ROMs of /sfc, loads one, passes the pads to it every frame, draws its picture, keeps the
-- battery RAM in /docs and has the menu. Original code.

local SW, SH = 256, 240
local AUTO = false            -- a test build opens the first ROM by itself (the bridge cannot press keys)

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

local frames = 0

-- the test build plays by itself (the bridge cannot press keys): through the title and the file select, then to the right
local function auto_pad(f)
  if f < 300 then return 0 end
  local t = f - 300
  if t < 2400 then                                              -- taps of Start, A and B in turn: the title, the file, the messages
    if t % 40 < 6 then
      if t < 400 then return ({ 0x1000, 0x0080, 0x8000 })[(t // 40) % 3 + 1] end    -- Start only at the title (in a level it is the pause)
      return ({ 0x0080, 0x8000 })[(t // 40) % 2 + 1]
    end
    return 0
  end
  return 0x0100 | ((t % 60) < 25 and 0x8000 or 0) | 0x4000      -- right, running (Y), jumping (B) now and then
end

local function readpad(p)
  if AUTO and p == 1 then return auto_pad(frames) end
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
local draw_ms = { 0, 0 }
local acc = { n = 0, f = 0, p = 0, s = 0, n2 = 0, tf = 0, bg = 0, ob = 0, wi = 0, co = 0, mi = 0 }
local snap                                         -- the test build's kept lines

-- How often a picture is made: every `every` emulated frames (1 = 60 pictures a second). The machine runs 60 frames a second whatever the picture costs
-- (frameskip), so the cart makes more pictures when the console keeps up and fewer when it does not. A window of 90 updates is timed: under 57 updates a
-- second, a picture less often (and no try for a picture more often for 15 seconds); three windows in a row at 58.8 or more, a picture more often.
local every, win_n, win_t, win_last, cooldown, good = 3, 0, 0, nil, 0, 0
local MINI, last_ups = false, 0                     -- a test build with a one-line speed readout
local function adapt()
  local t = time()
  if win_last then win_t = win_t + (t - win_last); win_n = win_n + 1 end
  win_last = t
  if cooldown > 0 then cooldown = cooldown - 1 end
  if win_n >= 90 then
    local ups = win_n / win_t                          -- _update a second
    last_ups = ups
    if ups < 57 then
      good = 0
      if every < 4 then every = every + 1; cooldown = 900 end
    elseif ups >= 58.8 then
      good = good + 1
      if good >= 3 and every > 1 and cooldown == 0 then every = every - 1; good = 0 end
    else
      good = 0
    end
    win_n, win_t = 0, 0
  end
end
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

local dx, dy, dw, dh = 0, 0, SW, SH            -- where and how big the picture is on the screen
local mode = "sharp"                           -- "sharp": a whole-number scale (1x on a 360-line screen), all pixels the same size; "fit": 4:3 on the whole screen
local cur_lines = 224                          -- lines of the picture now: 224, or 239 (PAL and overscan)

-- Where the picture goes for `lines` lines. A new size makes a new render target: the machine fills it whole.
local function layout(lines)
  local w, h
  if mode == "fit" then
    w, h = (SCREEN_H * 4) // 3, SCREEN_H
    if w > SCREEN_W then w = SCREEN_W; h = (SCREEN_W * 3) // 4 end
  else
    local s = math.max(1, math.floor(math.min(SCREEN_H / lines, SCREEN_W / 256)))   -- a whole number: every pixel the same size
    w, h = 256 * s, lines * s
  end
  if w ~= dw or h ~= dh or not scr then
    dw, dh = w, h
    scr = target_create(dw, dh)             -- the machine scales its picture into it: the kernel's own scaling is slow
  end
  dx, dy = (SCREEN_W - dw) // 2, (SCREEN_H - dh) // 2
end

function _init()
  layout(cur_lines)
  pad_layout()
  frameskip(4)
  list_roms()
  if AUTO and files[1] then open_rom(files[1]) end
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
    local bench = AUTO and frames >= 600 and frames < 3200      -- the test build: every frame in this range is drawn, and the time of the parts is summed: the same frames in every run
    if not (AUTO and frames >= 3200) then                                -- the test build stops at frame 3200: the same picture in every run, to compare two kernels
      local tf = time()
      if not AUTO then adapt() end
      snes_frame(readpad(1), readpad(2), bench or frames % (AUTO and 4 or every) == 0)      -- a picture every `every` frames (the test build: every fourth)
      if bench then acc.tf = acc.tf + (time() - tf) end
    end
    if bench then
      local bg, ob, wi, co, mi = string.match(snes_debug().us_ppu, "bg (%d+) obj (%d+) win (%d+) comp (%d+) mix (%d+)")
      if bg then acc.n2 = acc.n2 + 1; acc.bg = acc.bg + bg; acc.ob = acc.ob + ob; acc.wi = acc.wi + wi; acc.co = acc.co + co; acc.mi = acc.mi + mi end
    end
    frames = frames + 1
    if frames % 120 == 0 then pad_layout() end
    if frames % 600 == 0 then save_sram() end
  elseif state == "menu" then
    if btnp("up") then menusel = math.max(1, menusel - 1) end
    if btnp("down") then menusel = math.min(5, menusel + 1) end
    if btnp("a") then
      if menusel == 1 then state = "run"
      elseif menusel == 2 then save_sram(); snes_reset(); state = "run"
      elseif menusel == 3 then show_fps = not show_fps
      elseif menusel == 4 then mode = (mode == "sharp") and "fit" or "sharp"; layout(cur_lines)
      else save_sram(); state = "browse"; list_roms() end
    end
    if btnp("b") then state = "run" end
  end
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
  local t0 = time()
  local lines = snes_draw(scr, dw, dh)
  local t1 = time()
  if lines ~= cur_lines then cur_lines = lines; layout(lines) end   -- the next picture has the new size
  if dx > 0 then                                   -- the screen has two buffers: the margins are cleared every frame
    rectfill(0, 0, dx, SCREEN_H, 0)
    rectfill(dx + dw, 0, SCREEN_W - dx - dw, SCREEN_H, 0)
  end
  if dy > 0 then
    rectfill(0, 0, SCREEN_W, dy, 0)
    rectfill(0, dy + dh, SCREEN_W, SCREEN_H - dy - dh, 0)
  end
  scr:draw(dx, dy)
  draw_ms = { (t1 - t0) * 1000, (time() - t1) * 1000 }
  local d = snes_debug(0xCFC0)
  if d.inidisp & 0x80 ~= 0 and frames > 180 and not AUTO then      -- a game that keeps the screen off: say why
    local io = {}
    for i = 1, 6 do io[i] = string.format("%04X", d.io[i]) end
    print(string.format("screen off: PC %06X%s%s  nmi %02X  sound %s %04X", d.pc, d.wai and " WAI" or "",
      d.stp and " STP" or "", d.nmi, d.spc, d.spc_pc), 4, SCREEN_H - 32, 0xFF8040)
    print("reads: " .. table.concat(io, " ") .. "  " .. snes_info().mapping .. " " .. snes_info().title, 4,
      SCREEN_H - 16, 0xFF8040)
    print("cpu " .. d.cpu_code .. "  spc " .. d.spc_code, 4, SCREEN_H - 48, 0xFF8040)
    print("ports " .. d.ports, 4, SCREEN_H - 64, 0xFF8040)
  end
  if AUTO then                                           -- the test build: the mean cost of the parts over the same game frames (600 to 3200), to compare two kernels
    local f, p, sp = string.match(d.us, "us frame (%d+) ppu (%d+) spc (%d+)")
    if f and d.frames >= 600 and d.frames < 3300 then
      acc.n = acc.n + 1; acc.f = acc.f + f; acc.p = acc.p + p; acc.s = acc.s + sp
    end
    if acc.n > 0 then print(string.format("mean of %d draws, frames 600-%d: frame %d ppu %d spc %d us", acc.n, math.min(d.frames, 3300), acc.f // acc.n, acc.p // acc.n, acc.s // acc.n), 4, 90, 0xFFFF00) end
  end
  if AUTO and show_fps then                              -- the test build: the PPU registers, the first sprites, the other sprites, a tile and a palette; the overworld's values are kept
    local o, first, rest = d.oam, {}, {}
    for i = 0, 127 do
      local b = i * 4
      local y = o:byte(b + 2)
      if y < 224 then
        local hi = (o:byte(513 + (i >> 2)) >> ((i & 3) * 2)) & 3
        local t = string.format("%d:%d,%d t%02X a%02X s%d", i, o:byte(b + 1) + (hi & 1) * 256, y, o:byte(b + 3), o:byte(b + 4), hi >> 1)
        if #first < 4 then first[#first + 1] = t end
        if o:byte(b + 3) ~= 0x7E and #rest < 7 then rest[#rest + 1] = t end
      end
    end
    local hx, cg = {}, {}
    for i = 1, 16 do hx[i] = string.format("%02X", d.vram:byte(i)) end
    for i = 1, 8 do cg[i] = string.format("%02X%02X", d.cgram:byte(i * 2), d.cgram:byte(i * 2 - 1)) end
    local lines = { d.regs, "first: " .. table.concat(first, "  "), (acc.n2 > 0) and string.format("%d fr %.2f ms: bg %d obj %d win %d comp %d mix %d = %d us", acc.n2, acc.tf * 1000 / acc.n2, acc.bg // acc.n2, acc.ob // acc.n2, acc.wi // acc.n2, acc.co // acc.n2, acc.mi // acc.n2, (acc.bg + acc.ob + acc.wi + acc.co + acc.mi) // acc.n2) or ("other: " .. table.concat(rest, "  ")), "t7E " .. table.concat(hx, "", 1, 8) .. " vm " .. d.vmap:sub(193, 256) }
    if not snap and d.regs:find("hofs 1003") and #first > 0 and d.inidisp == 0x0F then snap = lines end       -- the overworld: keep it
    local out = snap or lines
    for i = 1, #out do print(out[i], 4, 282 + i * 16, snap and 0xFFC080 or 0x80FF80) end
  end
  if show_fps then
    rectfill(0, 0, 400, 78, 0)                          -- a black bar under the lines, to read them
    print(string.format("%d fps  pic 1/%d  blank %02X  tm %02X  sound %s %04X", stat(2), every, d.inidisp, d.tm, d.spc, d.spc_pc), 4, 4, 0x00FF00)
    print(d.us_ppu, 4, 36, 0x00FF00)
    print(d.audio, 4, 52, 0x00FF00)
    print(string.format("PC %06X%s%s  nmi %02X  reads %04X %04X %04X  frames %d", d.pc, d.wai and " WAI" or "", d.stp and " STP" or "", d.nmi, d.io[1], d.io[3], d.io[6], d.frames), 4, 68, 0x00FF00)
    print(d.us .. string.format("  copy %.1f blit %.1f ms  lua %.0f ms", draw_ms[1], draw_ms[2], stat(1)), 4, 20, 0x00FF00)
  end
  if MINI then print(string.format("%d fps  %.1f upd/s  pic 1/%d", stat(2), last_ups, every), 4, 4, 0x00FF00) end
  if state == "menu" then
    rectfill(SCREEN_W // 2 - 110, SCREEN_H // 2 - 70, 220, 164, 0x101820)
    rect(SCREEN_W // 2 - 110, SCREEN_H // 2 - 70, 220, 164, 0xFFD050)
    local items = { "Resume", "Reset", show_fps and "FPS: on" or "FPS: off",
                    mode == "sharp" and "Scale: sharp" or "Scale: fit 4:3", "Back to the list" }
    for i = 1, 5 do
      print(items[i], SCREEN_W // 2 - 90, SCREEN_H // 2 - 58 + (i - 1) * 24, i == menusel and 0xFFFFFF or 0x8090A0)
    end
    print(snes_info().title, SCREEN_W // 2 - 90, SCREEN_H // 2 + 66, 0x8090A0)
  end
end
